/*
*  linux/fs/nfsd/nfs4state.c
*
*  Copyright (c) 2001 The Regents of the University of Michigan.
*  All rights reserved.
*
*  Kendrick Smith <kmsmith@umich.edu>
*  Andy Adamson <kandros@umich.edu>
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*  1. Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*  2. Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in the
*     documentation and/or other materials provided with the distribution.
*  3. Neither the name of the University nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
*  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
*  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
*  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
*  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
*  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
*  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
*  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
*  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
*  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
*  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/

#include <linux/param.h>
#include <linux/major.h>
#include <linux/slab.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/mount.h>
#include <linux/workqueue.h>
#include <linux/smp_lock.h>
#include <linux/kthread.h>
#include <linux/nfs4.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/xdr4.h>

#define NFSDDBG_FACILITY                NFSDDBG_PROC

/* Globals */
static time_t lease_time = 90;     /* default lease time */
static time_t old_lease_time = 90; /* past incarnation lease time */
static u32 nfs4_reclaim_init = 0;
time_t boot_time;
static time_t grace_end = 0;
static u32 current_clientid = 1;
static u32 current_ownerid = 1;
static u32 current_fileid = 1;
static u32 current_delegid = 1;
static u32 nfs4_init;
stateid_t zerostateid;             /* bits all 0 */
stateid_t onestateid;              /* bits all 1 */

/* debug counters */
u32 list_add_perfile = 0; 
u32 list_del_perfile = 0;
u32 add_perclient = 0;
u32 del_perclient = 0;
u32 alloc_file = 0;
u32 free_file = 0;
u32 vfsopen = 0;
u32 vfsclose = 0;
u32 alloc_delegation= 0;
u32 free_delegation= 0;

/* forward declarations */
struct nfs4_stateid * find_stateid(stateid_t *stid, int flags);
static struct nfs4_delegation * find_delegation_stateid(struct inode *ino, stateid_t *stid);
static void release_stateid_lockowners(struct nfs4_stateid *open_stp);

/* Locking:
 *
 * client_sema: 
 * 	protects clientid_hashtbl[], clientstr_hashtbl[],
 * 	unconfstr_hashtbl[], uncofid_hashtbl[].
 */
static DECLARE_MUTEX(client_sema);

void
nfs4_lock_state(void)
{
	down(&client_sema);
}

void
nfs4_unlock_state(void)
{
	up(&client_sema);
}

static inline u32
opaque_hashval(const void *ptr, int nbytes)
{
	unsigned char *cptr = (unsigned char *) ptr;

	u32 x = 0;
	while (nbytes--) {
		x *= 37;
		x += *cptr++;
	}
	return x;
}

/* forward declarations */
static void release_stateowner(struct nfs4_stateowner *sop);
static void release_stateid(struct nfs4_stateid *stp, int flags);
static void release_file(struct nfs4_file *fp);

/*
 * Delegation state
 */

/* recall_lock protects the del_recall_lru */
spinlock_t recall_lock;
static struct list_head del_recall_lru;

static struct nfs4_delegation *
alloc_init_deleg(struct nfs4_client *clp, struct nfs4_stateid *stp, struct svc_fh *current_fh, u32 type)
{
	struct nfs4_delegation *dp;
	struct nfs4_file *fp = stp->st_file;
	struct nfs4_callback *cb = &stp->st_stateowner->so_client->cl_callback;

	dprintk("NFSD alloc_init_deleg\n");
	if ((dp = kmalloc(sizeof(struct nfs4_delegation),
		GFP_KERNEL)) == NULL)
		return dp;
	INIT_LIST_HEAD(&dp->dl_del_perfile);
	INIT_LIST_HEAD(&dp->dl_del_perclnt);
	INIT_LIST_HEAD(&dp->dl_recall_lru);
	dp->dl_client = clp;
	dp->dl_file = fp;
	dp->dl_flock = NULL;
	get_file(stp->st_vfs_file);
	dp->dl_vfs_file = stp->st_vfs_file;
	dp->dl_type = type;
	dp->dl_recall.cbr_dp = NULL;
	dp->dl_recall.cbr_ident = cb->cb_ident;
	dp->dl_recall.cbr_trunc = 0;
	dp->dl_stateid.si_boot = boot_time;
	dp->dl_stateid.si_stateownerid = current_delegid++;
	dp->dl_stateid.si_fileid = 0;
	dp->dl_stateid.si_generation = 0;
	dp->dl_fhlen = current_fh->fh_handle.fh_size;
	memcpy(dp->dl_fhval, &current_fh->fh_handle.fh_base,
		        current_fh->fh_handle.fh_size);
	dp->dl_time = 0;
	atomic_set(&dp->dl_count, 1);
	list_add(&dp->dl_del_perfile, &fp->fi_del_perfile);
	list_add(&dp->dl_del_perclnt, &clp->cl_del_perclnt);
	alloc_delegation++;
	return dp;
}

void
nfs4_put_delegation(struct nfs4_delegation *dp)
{
	if (atomic_dec_and_test(&dp->dl_count)) {
		dprintk("NFSD: freeing dp %p\n",dp);
		kfree(dp);
		free_delegation++;
	}
}

/* Remove the associated file_lock first, then remove the delegation.
 * lease_modify() is called to remove the FS_LEASE file_lock from
 * the i_flock list, eventually calling nfsd's lock_manager
 * fl_release_callback.
 */
static void
nfs4_close_delegation(struct nfs4_delegation *dp)
{
	struct file *filp = dp->dl_vfs_file;

	dprintk("NFSD: close_delegation dp %p\n",dp);
	dp->dl_vfs_file = NULL;
	/* The following nfsd_close may not actually close the file,
	 * but we want to remove the lease in any case. */
	setlease(filp, F_UNLCK, &dp->dl_flock);
	nfsd_close(filp);
	vfsclose++;
}

/* Called under the state lock. */
static void
unhash_delegation(struct nfs4_delegation *dp)
{
	list_del_init(&dp->dl_del_perfile);
	list_del_init(&dp->dl_del_perclnt);
	spin_lock(&recall_lock);
	list_del_init(&dp->dl_recall_lru);
	spin_unlock(&recall_lock);
	nfs4_close_delegation(dp);
	nfs4_put_delegation(dp);
}

/* 
 * SETCLIENTID state 
 */

/* Hash tables for nfs4_clientid state */
#define CLIENT_HASH_BITS                 4
#define CLIENT_HASH_SIZE                (1 << CLIENT_HASH_BITS)
#define CLIENT_HASH_MASK                (CLIENT_HASH_SIZE - 1)

#define clientid_hashval(id) \
	((id) & CLIENT_HASH_MASK)
#define clientstr_hashval(name, namelen) \
	(opaque_hashval((name), (namelen)) & CLIENT_HASH_MASK)
/*
 * reclaim_str_hashtbl[] holds known client info from previous reset/reboot
 * used in reboot/reset lease grace period processing
 *
 * conf_id_hashtbl[], and conf_str_hashtbl[] hold confirmed
 * setclientid_confirmed info. 
 *
 * unconf_str_hastbl[] and unconf_id_hashtbl[] hold unconfirmed 
 * setclientid info.
 *
 * client_lru holds client queue ordered by nfs4_client.cl_time
 * for lease renewal.
 *
 * close_lru holds (open) stateowner queue ordered by nfs4_stateowner.so_time
 * for last close replay.
 */
static struct list_head	reclaim_str_hashtbl[CLIENT_HASH_SIZE];
static int reclaim_str_hashtbl_size = 0;
static struct list_head	conf_id_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	conf_str_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	unconf_str_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	unconf_id_hashtbl[CLIENT_HASH_SIZE];
static struct list_head client_lru;
static struct list_head close_lru;

static inline void
renew_client(struct nfs4_client *clp)
{
	/*
	* Move client to the end to the LRU list.
	*/
	dprintk("renewing client (clientid %08x/%08x)\n", 
			clp->cl_clientid.cl_boot, 
			clp->cl_clientid.cl_id);
	list_move_tail(&clp->cl_lru, &client_lru);
	clp->cl_time = get_seconds();
}

/* SETCLIENTID and SETCLIENTID_CONFIRM Helper functions */
static int
STALE_CLIENTID(clientid_t *clid)
{
	if (clid->cl_boot == boot_time)
		return 0;
	dprintk("NFSD stale clientid (%08x/%08x)\n", 
			clid->cl_boot, clid->cl_id);
	return 1;
}

/* 
 * XXX Should we use a slab cache ?
 * This type of memory management is somewhat inefficient, but we use it
 * anyway since SETCLIENTID is not a common operation.
 */
static inline struct nfs4_client *
alloc_client(struct xdr_netobj name)
{
	struct nfs4_client *clp;

	if ((clp = kmalloc(sizeof(struct nfs4_client), GFP_KERNEL))!= NULL) {
		memset(clp, 0, sizeof(*clp));
		if ((clp->cl_name.data = kmalloc(name.len, GFP_KERNEL)) != NULL) {
			memcpy(clp->cl_name.data, name.data, name.len);
			clp->cl_name.len = name.len;
		}
		else {
			kfree(clp);
			clp = NULL;
		}
	}
	return clp;
}

static inline void
free_client(struct nfs4_client *clp)
{
	if (clp->cl_cred.cr_group_info)
		put_group_info(clp->cl_cred.cr_group_info);
	kfree(clp->cl_name.data);
	kfree(clp);
}

void
put_nfs4_client(struct nfs4_client *clp)
{
	if (atomic_dec_and_test(&clp->cl_count))
		free_client(clp);
}

static void
expire_client(struct nfs4_client *clp)
{
	struct nfs4_stateowner *sop;
	struct nfs4_delegation *dp;
	struct nfs4_callback *cb = &clp->cl_callback;
	struct rpc_clnt *clnt = clp->cl_callback.cb_client;
	struct list_head reaplist;

	dprintk("NFSD: expire_client cl_count %d\n",
	                    atomic_read(&clp->cl_count));

	/* shutdown rpc client, ending any outstanding recall rpcs */
	if (atomic_read(&cb->cb_set) == 1 && clnt) {
		rpc_shutdown_client(clnt);
		clnt = clp->cl_callback.cb_client = NULL;
	}

	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	while (!list_empty(&clp->cl_del_perclnt)) {
		dp = list_entry(clp->cl_del_perclnt.next, struct nfs4_delegation, dl_del_perclnt);
		dprintk("NFSD: expire client. dp %p, fp %p\n", dp,
				dp->dl_flock);
		list_del_init(&dp->dl_del_perclnt);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	while (!list_empty(&reaplist)) {
		dp = list_entry(reaplist.next, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		unhash_delegation(dp);
	}
	list_del(&clp->cl_idhash);
	list_del(&clp->cl_strhash);
	list_del(&clp->cl_lru);
	while (!list_empty(&clp->cl_perclient)) {
		sop = list_entry(clp->cl_perclient.next, struct nfs4_stateowner, so_perclient);
		release_stateowner(sop);
	}
	put_nfs4_client(clp);
}

static struct nfs4_client *
create_client(struct xdr_netobj name) {
	struct nfs4_client *clp;

	if (!(clp = alloc_client(name)))
		goto out;
	atomic_set(&clp->cl_count, 1);
	atomic_set(&clp->cl_callback.cb_set, 0);
	clp->cl_callback.cb_parsed = 0;
	INIT_LIST_HEAD(&clp->cl_idhash);
	INIT_LIST_HEAD(&clp->cl_strhash);
	INIT_LIST_HEAD(&clp->cl_perclient);
	INIT_LIST_HEAD(&clp->cl_del_perclnt);
	INIT_LIST_HEAD(&clp->cl_lru);
out:
	return clp;
}

static void
copy_verf(struct nfs4_client *target, nfs4_verifier *source) {
	memcpy(target->cl_verifier.data, source->data, sizeof(target->cl_verifier.data));
}

static void
copy_clid(struct nfs4_client *target, struct nfs4_client *source) {
	target->cl_clientid.cl_boot = source->cl_clientid.cl_boot; 
	target->cl_clientid.cl_id = source->cl_clientid.cl_id; 
}

static void
copy_cred(struct svc_cred *target, struct svc_cred *source) {

	target->cr_uid = source->cr_uid;
	target->cr_gid = source->cr_gid;
	target->cr_group_info = source->cr_group_info;
	get_group_info(target->cr_group_info);
}

static int
cmp_name(struct xdr_netobj *n1, struct xdr_netobj *n2) {
	if (!n1 || !n2)
		return 0;
	return((n1->len == n2->len) && !memcmp(n1->data, n2->data, n2->len));
}

static int
cmp_verf(nfs4_verifier *v1, nfs4_verifier *v2) {
	return(!memcmp(v1->data,v2->data,sizeof(v1->data)));
}

static int
cmp_clid(clientid_t * cl1, clientid_t * cl2) {
	return((cl1->cl_boot == cl2->cl_boot) &&
	   	(cl1->cl_id == cl2->cl_id));
}

/* XXX what about NGROUP */
static int
cmp_creds(struct svc_cred *cr1, struct svc_cred *cr2){
	return(cr1->cr_uid == cr2->cr_uid);

}

static void
gen_clid(struct nfs4_client *clp) {
	clp->cl_clientid.cl_boot = boot_time;
	clp->cl_clientid.cl_id = current_clientid++; 
}

static void
gen_confirm(struct nfs4_client *clp) {
	struct timespec 	tv;
	u32 *			p;

	tv = CURRENT_TIME;
	p = (u32 *)clp->cl_confirm.data;
	*p++ = tv.tv_sec;
	*p++ = tv.tv_nsec;
}

static int
check_name(struct xdr_netobj name) {

	if (name.len == 0) 
		return 0;
	if (name.len > NFS4_OPAQUE_LIMIT) {
		printk("NFSD: check_name: name too long(%d)!\n", name.len);
		return 0;
	}
	return 1;
}

void
add_to_unconfirmed(struct nfs4_client *clp, unsigned int strhashval)
{
	unsigned int idhashval;

	list_add(&clp->cl_strhash, &unconf_str_hashtbl[strhashval]);
	idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	list_add(&clp->cl_idhash, &unconf_id_hashtbl[idhashval]);
	list_add_tail(&clp->cl_lru, &client_lru);
	clp->cl_time = get_seconds();
}

void
move_to_confirmed(struct nfs4_client *clp)
{
	unsigned int idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	unsigned int strhashval;

	dprintk("NFSD: move_to_confirm nfs4_client %p\n", clp);
	list_del_init(&clp->cl_strhash);
	list_del_init(&clp->cl_idhash);
	list_add(&clp->cl_idhash, &conf_id_hashtbl[idhashval]);
	strhashval = clientstr_hashval(clp->cl_name.data, 
			clp->cl_name.len);
	list_add(&clp->cl_strhash, &conf_str_hashtbl[strhashval]);
	renew_client(clp);
}

static struct nfs4_client *
find_confirmed_client(clientid_t *clid)
{
	struct nfs4_client *clp;
	unsigned int idhashval = clientid_hashval(clid->cl_id);

	list_for_each_entry(clp, &conf_id_hashtbl[idhashval], cl_idhash) {
		if (cmp_clid(&clp->cl_clientid, clid))
			return clp;
	}
	return NULL;
}

static struct nfs4_client *
find_unconfirmed_client(clientid_t *clid)
{
	struct nfs4_client *clp;
	unsigned int idhashval = clientid_hashval(clid->cl_id);

	list_for_each_entry(clp, &unconf_id_hashtbl[idhashval], cl_idhash) {
		if (cmp_clid(&clp->cl_clientid, clid))
			return clp;
	}
	return NULL;
}

/* a helper function for parse_callback */
static int
parse_octet(unsigned int *lenp, char **addrp)
{
	unsigned int len = *lenp;
	char *p = *addrp;
	int n = -1;
	char c;

	for (;;) {
		if (!len)
			break;
		len--;
		c = *p++;
		if (c == '.')
			break;
		if ((c < '0') || (c > '9')) {
			n = -1;
			break;
		}
		if (n < 0)
			n = 0;
		n = (n * 10) + (c - '0');
		if (n > 255) {
			n = -1;
			break;
		}
	}
	*lenp = len;
	*addrp = p;
	return n;
}

/* parse and set the setclientid ipv4 callback address */
int
parse_ipv4(unsigned int addr_len, char *addr_val, unsigned int *cbaddrp, unsigned short *cbportp)
{
	int temp = 0;
	u32 cbaddr = 0;
	u16 cbport = 0;
	u32 addrlen = addr_len;
	char *addr = addr_val;
	int i, shift;

	/* ipaddress */
	shift = 24;
	for(i = 4; i > 0  ; i--) {
		if ((temp = parse_octet(&addrlen, &addr)) < 0) {
			return 0;
		}
		cbaddr |= (temp << shift);
		if (shift > 0)
		shift -= 8;
	}
	*cbaddrp = cbaddr;

	/* port */
	shift = 8;
	for(i = 2; i > 0  ; i--) {
		if ((temp = parse_octet(&addrlen, &addr)) < 0) {
			return 0;
		}
		cbport |= (temp << shift);
		if (shift > 0)
			shift -= 8;
	}
	*cbportp = cbport;
	return 1;
}

void
gen_callback(struct nfs4_client *clp, struct nfsd4_setclientid *se)
{
	struct nfs4_callback *cb = &clp->cl_callback;

	/* Currently, we only support tcp for the callback channel */
	if ((se->se_callback_netid_len != 3) || memcmp((char *)se->se_callback_netid_val, "tcp", 3))
		goto out_err;

	if ( !(parse_ipv4(se->se_callback_addr_len, se->se_callback_addr_val,
	                 &cb->cb_addr, &cb->cb_port)))
		goto out_err;
	cb->cb_prog = se->se_callback_prog;
	cb->cb_ident = se->se_callback_ident;
	cb->cb_parsed = 1;
	return;
out_err:
	printk(KERN_INFO "NFSD: this client (clientid %08x/%08x) "
		"will not receive delegations\n",
		clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);

	cb->cb_parsed = 0;
	return;
}

/*
 * RFC 3010 has a complex implmentation description of processing a 
 * SETCLIENTID request consisting of 5 bullets, labeled as 
 * CASE0 - CASE4 below.
 *
 * NOTES:
 * 	callback information will be processed in a future patch
 *
 *	an unconfirmed record is added when:
 *      NORMAL (part of CASE 4): there is no confirmed nor unconfirmed record.
 *	CASE 1: confirmed record found with matching name, principal,
 *		verifier, and clientid.
 *	CASE 2: confirmed record found with matching name, principal,
 *		and there is no unconfirmed record with matching
 *		name and principal
 *
 *      an unconfirmed record is replaced when:
 *	CASE 3: confirmed record found with matching name, principal,
 *		and an unconfirmed record is found with matching 
 *		name, principal, and with clientid and
 *		confirm that does not match the confirmed record.
 *	CASE 4: there is no confirmed record with matching name and 
 *		principal. there is an unconfirmed record with 
 *		matching name, principal.
 *
 *	an unconfirmed record is deleted when:
 *	CASE 1: an unconfirmed record that matches input name, verifier,
 *		and confirmed clientid.
 *	CASE 4: any unconfirmed records with matching name and principal
 *		that exist after an unconfirmed record has been replaced
 *		as described above.
 *
 */
int
nfsd4_setclientid(struct svc_rqst *rqstp, struct nfsd4_setclientid *setclid)
{
	u32 			ip_addr = rqstp->rq_addr.sin_addr.s_addr;
	struct xdr_netobj 	clname = { 
		.len = setclid->se_namelen,
		.data = setclid->se_name,
	};
	nfs4_verifier		clverifier = setclid->se_verf;
	unsigned int 		strhashval;
	struct nfs4_client *	conf, * unconf, * new, * clp;
	int 			status;
	
	status = nfserr_inval;
	if (!check_name(clname))
		goto out;

	/* 
	 * XXX The Duplicate Request Cache (DRC) has been checked (??)
	 * We get here on a DRC miss.
	 */

	strhashval = clientstr_hashval(clname.data, clname.len);

	conf = NULL;
	nfs4_lock_state();
	list_for_each_entry(clp, &conf_str_hashtbl[strhashval], cl_strhash) {
		if (!cmp_name(&clp->cl_name, &clname))
			continue;
		/* 
		 * CASE 0:
		 * clname match, confirmed, different principal
		 * or different ip_address
		 */
		status = nfserr_clid_inuse;
		if (!cmp_creds(&clp->cl_cred,&rqstp->rq_cred)) {
			printk("NFSD: setclientid: string in use by client"
			"(clientid %08x/%08x)\n",
			clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);
			goto out;
		}
		if (clp->cl_addr != ip_addr) { 
			printk("NFSD: setclientid: string in use by client"
			"(clientid %08x/%08x)\n",
			clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);
			goto out;
		}

		/* 
	 	 * cl_name match from a previous SETCLIENTID operation
	 	 * XXX check for additional matches?
		 */
		conf = clp;
		break;
	}
	unconf = NULL;
	list_for_each_entry(clp, &unconf_str_hashtbl[strhashval], cl_strhash) {
		if (!cmp_name(&clp->cl_name, &clname))
			continue;
		/* cl_name match from a previous SETCLIENTID operation */
		unconf = clp;
		break;
	}
	status = nfserr_resource;
	if (!conf) {
		/* 
		 * CASE 4:
		 * placed first, because it is the normal case.
		 */
		if (unconf)
			expire_client(unconf);
		if (!(new = create_client(clname)))
			goto out;
		copy_verf(new, &clverifier);
		new->cl_addr = ip_addr;
		copy_cred(&new->cl_cred,&rqstp->rq_cred);
		gen_clid(new);
		gen_confirm(new);
		gen_callback(new, setclid);
		add_to_unconfirmed(new, strhashval);
	} else if (cmp_verf(&conf->cl_verifier, &clverifier)) {
		/*
		 * CASE 1:
		 * cl_name match, confirmed, principal match
		 * verifier match: probable callback update
		 *
		 * remove any unconfirmed nfs4_client with 
		 * matching cl_name, cl_verifier, and cl_clientid
		 *
		 * create and insert an unconfirmed nfs4_client with same 
		 * cl_name, cl_verifier, and cl_clientid as existing 
		 * nfs4_client,  but with the new callback info and a 
		 * new cl_confirm
		 */
		if ((unconf) && 
		    cmp_verf(&unconf->cl_verifier, &conf->cl_verifier) &&
		     cmp_clid(&unconf->cl_clientid, &conf->cl_clientid)) {
				expire_client(unconf);
		}
		if (!(new = create_client(clname)))
			goto out;
		copy_verf(new,&conf->cl_verifier);
		new->cl_addr = ip_addr;
		copy_cred(&new->cl_cred,&rqstp->rq_cred);
		copy_clid(new, conf);
		gen_confirm(new);
		gen_callback(new, setclid);
		add_to_unconfirmed(new,strhashval);
	} else if (!unconf) {
		/*
		 * CASE 2:
		 * clname match, confirmed, principal match
		 * verfier does not match
		 * no unconfirmed. create a new unconfirmed nfs4_client
		 * using input clverifier, clname, and callback info
		 * and generate a new cl_clientid and cl_confirm.
		 */
		if (!(new = create_client(clname)))
			goto out;
		copy_verf(new,&clverifier);
		new->cl_addr = ip_addr;
		copy_cred(&new->cl_cred,&rqstp->rq_cred);
		gen_clid(new);
		gen_confirm(new);
		gen_callback(new, setclid);
		add_to_unconfirmed(new, strhashval);
	} else if (!cmp_verf(&conf->cl_confirm, &unconf->cl_confirm)) {
		/*	
		 * CASE3:
		 * confirmed found (name, principal match)
		 * confirmed verifier does not match input clverifier
		 *
		 * unconfirmed found (name match)
		 * confirmed->cl_confirm != unconfirmed->cl_confirm
		 *
		 * remove unconfirmed.
		 *
		 * create an unconfirmed nfs4_client 
		 * with same cl_name as existing confirmed nfs4_client, 
		 * but with new callback info, new cl_clientid,
		 * new cl_verifier and a new cl_confirm
		 */
		expire_client(unconf);
		if (!(new = create_client(clname)))
			goto out;
		copy_verf(new,&clverifier);
		new->cl_addr = ip_addr;
		copy_cred(&new->cl_cred,&rqstp->rq_cred);
		gen_clid(new);
		gen_confirm(new);
		gen_callback(new, setclid);
		add_to_unconfirmed(new, strhashval);
	} else {
		/* No cases hit !!! */
		status = nfserr_inval;
		goto out;

	}
	setclid->se_clientid.cl_boot = new->cl_clientid.cl_boot;
	setclid->se_clientid.cl_id = new->cl_clientid.cl_id;
	memcpy(setclid->se_confirm.data, new->cl_confirm.data, sizeof(setclid->se_confirm.data));
	status = nfs_ok;
out:
	nfs4_unlock_state();
	return status;
}


/*
 * RFC 3010 has a complex implmentation description of processing a 
 * SETCLIENTID_CONFIRM request consisting of 4 bullets describing
 * processing on a DRC miss, labeled as CASE1 - CASE4 below.
 *
 * NOTE: callback information will be processed here in a future patch
 */
int
nfsd4_setclientid_confirm(struct svc_rqst *rqstp, struct nfsd4_setclientid_confirm *setclientid_confirm)
{
	u32 ip_addr = rqstp->rq_addr.sin_addr.s_addr;
	struct nfs4_client *clp, *conf = NULL, *unconf = NULL;
	nfs4_verifier confirm = setclientid_confirm->sc_confirm; 
	clientid_t * clid = &setclientid_confirm->sc_clientid;
	int status;

	if (STALE_CLIENTID(clid))
		return nfserr_stale_clientid;
	/* 
	 * XXX The Duplicate Request Cache (DRC) has been checked (??)
	 * We get here on a DRC miss.
	 */

	nfs4_lock_state();
	clp = find_confirmed_client(clid);
	if (clp) {
		status = nfserr_inval;
		/* 
		 * Found a record for this clientid. If the IP addresses
		 * don't match, return ERR_INVAL just as if the record had
		 * not been found.
		 */
		if (clp->cl_addr != ip_addr) { 
			printk("NFSD: setclientid: string in use by client"
			"(clientid %08x/%08x)\n",
			clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);
			goto out;
		}
		conf = clp;
	}
	clp = find_unconfirmed_client(clid);
	if (clp) {
		status = nfserr_inval;
		if (clp->cl_addr != ip_addr) { 
			printk("NFSD: setclientid: string in use by client"
			"(clientid %08x/%08x)\n",
			clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);
			goto out;
		}
		unconf = clp;
	}
	/* CASE 1: 
	* unconf record that matches input clientid and input confirm.
	* conf record that matches input clientid.
	* conf  and unconf records match names, verifiers 
	*/
	if ((conf && unconf) && 
	    (cmp_verf(&unconf->cl_confirm, &confirm)) &&
	    (cmp_verf(&conf->cl_verifier, &unconf->cl_verifier)) &&
	    (cmp_name(&conf->cl_name,&unconf->cl_name))  &&
	    (!cmp_verf(&conf->cl_confirm, &unconf->cl_confirm))) {
		if (!cmp_creds(&conf->cl_cred, &unconf->cl_cred)) 
			status = nfserr_clid_inuse;
		else {
			expire_client(conf);
			clp = unconf;
			move_to_confirmed(unconf);
			status = nfs_ok;
		}
		goto out;
	} 
	/* CASE 2:
	 * conf record that matches input clientid.
	 * if unconf record that matches input clientid, then unconf->cl_name
	 * or unconf->cl_verifier don't match the conf record.
	 */
	if ((conf && !unconf) || 
	    ((conf && unconf) && 
	     (!cmp_verf(&conf->cl_verifier, &unconf->cl_verifier) ||
	      !cmp_name(&conf->cl_name, &unconf->cl_name)))) {
		if (!cmp_creds(&conf->cl_cred,&rqstp->rq_cred)) {
			status = nfserr_clid_inuse;
		} else {
			clp = conf;
			status = nfs_ok;
		}
		goto out;
	}
	/* CASE 3:
	 * conf record not found.
	 * unconf record found. 
	 * unconf->cl_confirm matches input confirm
	 */ 
	if (!conf && unconf && cmp_verf(&unconf->cl_confirm, &confirm)) {
		if (!cmp_creds(&unconf->cl_cred, &rqstp->rq_cred)) {
			status = nfserr_clid_inuse;
		} else {
			status = nfs_ok;
			clp = unconf;
			move_to_confirmed(unconf);
		}
		goto out;
	}
	/* CASE 4:
	 * conf record not found, or if conf, then conf->cl_confirm does not
	 * match input confirm.
	 * unconf record not found, or if unconf, then unconf->cl_confirm 
	 * does not match input confirm.
	 */
	if ((!conf || (conf && !cmp_verf(&conf->cl_confirm, &confirm))) &&
	    (!unconf || (unconf && !cmp_verf(&unconf->cl_confirm, &confirm)))) {
		status = nfserr_stale_clientid;
		goto out;
	}
	/* check that we have hit one of the cases...*/
	status = nfserr_inval;
	goto out;
out:
	if (!status)
		nfsd4_probe_callback(clp);
	nfs4_unlock_state();
	return status;
}

/* 
 * Open owner state (share locks)
 */

/* hash tables for nfs4_stateowner */
#define OWNER_HASH_BITS              8
#define OWNER_HASH_SIZE             (1 << OWNER_HASH_BITS)
#define OWNER_HASH_MASK             (OWNER_HASH_SIZE - 1)

#define ownerid_hashval(id) \
        ((id) & OWNER_HASH_MASK)
#define ownerstr_hashval(clientid, ownername) \
        (((clientid) + opaque_hashval((ownername.data), (ownername.len))) & OWNER_HASH_MASK)

static struct list_head	ownerid_hashtbl[OWNER_HASH_SIZE];
static struct list_head	ownerstr_hashtbl[OWNER_HASH_SIZE];

/* hash table for nfs4_file */
#define FILE_HASH_BITS                   8
#define FILE_HASH_SIZE                  (1 << FILE_HASH_BITS)
#define FILE_HASH_MASK                  (FILE_HASH_SIZE - 1)
/* hash table for (open)nfs4_stateid */
#define STATEID_HASH_BITS              10
#define STATEID_HASH_SIZE              (1 << STATEID_HASH_BITS)
#define STATEID_HASH_MASK              (STATEID_HASH_SIZE - 1)

#define file_hashval(x) \
        hash_ptr(x, FILE_HASH_BITS)
#define stateid_hashval(owner_id, file_id)  \
        (((owner_id) + (file_id)) & STATEID_HASH_MASK)

static struct list_head file_hashtbl[FILE_HASH_SIZE];
static struct list_head stateid_hashtbl[STATEID_HASH_SIZE];

/* OPEN Share state helper functions */
static inline struct nfs4_file *
alloc_init_file(struct inode *ino)
{
	struct nfs4_file *fp;
	unsigned int hashval = file_hashval(ino);

	if ((fp = kmalloc(sizeof(struct nfs4_file),GFP_KERNEL))) {
		INIT_LIST_HEAD(&fp->fi_hash);
		INIT_LIST_HEAD(&fp->fi_perfile);
		INIT_LIST_HEAD(&fp->fi_del_perfile);
		list_add(&fp->fi_hash, &file_hashtbl[hashval]);
		fp->fi_inode = igrab(ino);
		fp->fi_id = current_fileid++;
		alloc_file++;
		return fp;
	}
	return NULL;
}

static void
release_all_files(void)
{
	int i;
	struct nfs4_file *fp;

	for (i=0;i<FILE_HASH_SIZE;i++) {
		while (!list_empty(&file_hashtbl[i])) {
			fp = list_entry(file_hashtbl[i].next, struct nfs4_file, fi_hash);
			/* this should never be more than once... */
			if (!list_empty(&fp->fi_perfile) || !list_empty(&fp->fi_del_perfile)) {
				printk("ERROR: release_all_files: file %p is open, creating dangling state !!!\n",fp);
			}
			release_file(fp);
		}
	}
}

kmem_cache_t *stateowner_slab = NULL;

static int
nfsd4_init_slabs(void)
{
	stateowner_slab = kmem_cache_create("nfsd4_stateowners",
			sizeof(struct nfs4_stateowner), 0, 0, NULL, NULL);
	if (stateowner_slab == NULL) {
		dprintk("nfsd4: out of memory while initializing nfsv4\n");
		return -ENOMEM;
	}
	return 0;
}

static void
nfsd4_free_slabs(void)
{
	int status = 0;

	if (stateowner_slab)
		status = kmem_cache_destroy(stateowner_slab);
	stateowner_slab = NULL;
	BUG_ON(status);
}

void
nfs4_free_stateowner(struct kref *kref)
{
	struct nfs4_stateowner *sop =
		container_of(kref, struct nfs4_stateowner, so_ref);
	kfree(sop->so_owner.data);
	kmem_cache_free(stateowner_slab, sop);
}

static inline struct nfs4_stateowner *
alloc_stateowner(struct xdr_netobj *owner)
{
	struct nfs4_stateowner *sop;

	if ((sop = kmem_cache_alloc(stateowner_slab, GFP_KERNEL))) {
		if ((sop->so_owner.data = kmalloc(owner->len, GFP_KERNEL))) {
			memcpy(sop->so_owner.data, owner->data, owner->len);
			sop->so_owner.len = owner->len;
			kref_init(&sop->so_ref);
			return sop;
		} 
		kmem_cache_free(stateowner_slab, sop);
	}
	return NULL;
}

static struct nfs4_stateowner *
alloc_init_open_stateowner(unsigned int strhashval, struct nfs4_client *clp, struct nfsd4_open *open) {
	struct nfs4_stateowner *sop;
	struct nfs4_replay *rp;
	unsigned int idhashval;

	if (!(sop = alloc_stateowner(&open->op_owner)))
		return NULL;
	idhashval = ownerid_hashval(current_ownerid);
	INIT_LIST_HEAD(&sop->so_idhash);
	INIT_LIST_HEAD(&sop->so_strhash);
	INIT_LIST_HEAD(&sop->so_perclient);
	INIT_LIST_HEAD(&sop->so_perfilestate);
	INIT_LIST_HEAD(&sop->so_perlockowner);  /* not used */
	INIT_LIST_HEAD(&sop->so_close_lru);
	sop->so_time = 0;
	list_add(&sop->so_idhash, &ownerid_hashtbl[idhashval]);
	list_add(&sop->so_strhash, &ownerstr_hashtbl[strhashval]);
	list_add(&sop->so_perclient, &clp->cl_perclient);
	add_perclient++;
	sop->so_is_open_owner = 1;
	sop->so_id = current_ownerid++;
	sop->so_client = clp;
	sop->so_seqid = open->op_seqid;
	sop->so_confirmed = 0;
	rp = &sop->so_replay;
	rp->rp_status = NFSERR_SERVERFAULT;
	rp->rp_buflen = 0;
	rp->rp_buf = rp->rp_ibuf;
	return sop;
}

static void
release_stateid_lockowners(struct nfs4_stateid *open_stp)
{
	struct nfs4_stateowner *lock_sop;

	while (!list_empty(&open_stp->st_perlockowner)) {
		lock_sop = list_entry(open_stp->st_perlockowner.next,
				struct nfs4_stateowner, so_perlockowner);
		/* list_del(&open_stp->st_perlockowner);  */
		BUG_ON(lock_sop->so_is_open_owner);
		release_stateowner(lock_sop);
	}
}

static void
unhash_stateowner(struct nfs4_stateowner *sop)
{
	struct nfs4_stateid *stp;

	list_del(&sop->so_idhash);
	list_del(&sop->so_strhash);
	if (sop->so_is_open_owner) {
		list_del(&sop->so_perclient);
		del_perclient++;
	}
	list_del(&sop->so_perlockowner);
	while (!list_empty(&sop->so_perfilestate)) {
		stp = list_entry(sop->so_perfilestate.next, 
			struct nfs4_stateid, st_perfilestate);
		if (sop->so_is_open_owner)
			release_stateid(stp, OPEN_STATE);
		else
			release_stateid(stp, LOCK_STATE);
	}
}

static void
release_stateowner(struct nfs4_stateowner *sop)
{
	unhash_stateowner(sop);
	list_del(&sop->so_close_lru);
	nfs4_put_stateowner(sop);
}

static inline void
init_stateid(struct nfs4_stateid *stp, struct nfs4_file *fp, struct nfsd4_open *open) {
	struct nfs4_stateowner *sop = open->op_stateowner;
	unsigned int hashval = stateid_hashval(sop->so_id, fp->fi_id);

	INIT_LIST_HEAD(&stp->st_hash);
	INIT_LIST_HEAD(&stp->st_perfilestate);
	INIT_LIST_HEAD(&stp->st_perlockowner);
	INIT_LIST_HEAD(&stp->st_perfile);
	list_add(&stp->st_hash, &stateid_hashtbl[hashval]);
	list_add(&stp->st_perfilestate, &sop->so_perfilestate);
	list_add_perfile++;
	list_add(&stp->st_perfile, &fp->fi_perfile);
	stp->st_stateowner = sop;
	stp->st_file = fp;
	stp->st_stateid.si_boot = boot_time;
	stp->st_stateid.si_stateownerid = sop->so_id;
	stp->st_stateid.si_fileid = fp->fi_id;
	stp->st_stateid.si_generation = 0;
	stp->st_access_bmap = 0;
	stp->st_deny_bmap = 0;
	__set_bit(open->op_share_access, &stp->st_access_bmap);
	__set_bit(open->op_share_deny, &stp->st_deny_bmap);
}

static void
release_stateid(struct nfs4_stateid *stp, int flags)
{
	struct file *filp = stp->st_vfs_file;

	list_del(&stp->st_hash);
	list_del_perfile++;
	list_del(&stp->st_perfile);
	list_del(&stp->st_perfilestate);
	if (flags & OPEN_STATE) {
		release_stateid_lockowners(stp);
		stp->st_vfs_file = NULL;
		nfsd_close(filp);
		vfsclose++;
	} else if (flags & LOCK_STATE)
		locks_remove_posix(filp, (fl_owner_t) stp->st_stateowner);
	kfree(stp);
	stp = NULL;
}

static void
release_file(struct nfs4_file *fp)
{
	free_file++;
	list_del(&fp->fi_hash);
	iput(fp->fi_inode);
	kfree(fp);
}	

void
move_to_close_lru(struct nfs4_stateowner *sop)
{
	dprintk("NFSD: move_to_close_lru nfs4_stateowner %p\n", sop);

	unhash_stateowner(sop);
	list_add_tail(&sop->so_close_lru, &close_lru);
	sop->so_time = get_seconds();
}

void
release_state_owner(struct nfs4_stateid *stp, int flag)
{
	struct nfs4_stateowner *sop = stp->st_stateowner;
	struct nfs4_file *fp = stp->st_file;

	dprintk("NFSD: release_state_owner\n");
	release_stateid(stp, flag);

	/* place unused nfs4_stateowners on so_close_lru list to be
	 * released by the laundromat service after the lease period
	 * to enable us to handle CLOSE replay
	 */
	if (sop->so_confirmed && list_empty(&sop->so_perfilestate))
		move_to_close_lru(sop);
	/* unused nfs4_file's are releseed. XXX slab cache? */
	if (list_empty(&fp->fi_perfile) && list_empty(&fp->fi_del_perfile)) {
		release_file(fp);
	}
}

static int
cmp_owner_str(struct nfs4_stateowner *sop, struct xdr_netobj *owner, clientid_t *clid) {
	return ((sop->so_owner.len == owner->len) && 
	 !memcmp(sop->so_owner.data, owner->data, owner->len) && 
	  (sop->so_client->cl_clientid.cl_id == clid->cl_id));
}

static struct nfs4_stateowner *
find_openstateowner_str(unsigned int hashval, struct nfsd4_open *open)
{
	struct nfs4_stateowner *so = NULL;

	list_for_each_entry(so, &ownerstr_hashtbl[hashval], so_strhash) {
		if (cmp_owner_str(so, &open->op_owner, &open->op_clientid))
			return so;
	}
	return NULL;
}

/* search file_hashtbl[] for file */
static struct nfs4_file *
find_file(struct inode *ino)
{
	unsigned int hashval = file_hashval(ino);
	struct nfs4_file *fp;

	list_for_each_entry(fp, &file_hashtbl[hashval], fi_hash) {
		if (fp->fi_inode == ino)
			return fp;
	}
	return NULL;
}

#define TEST_ACCESS(x) ((x > 0 || x < 4)?1:0)
#define TEST_DENY(x) ((x >= 0 || x < 5)?1:0)

void
set_access(unsigned int *access, unsigned long bmap) {
	int i;

	*access = 0;
	for (i = 1; i < 4; i++) {
		if (test_bit(i, &bmap))
			*access |= i;
	}
}

void
set_deny(unsigned int *deny, unsigned long bmap) {
	int i;

	*deny = 0;
	for (i = 0; i < 4; i++) {
		if (test_bit(i, &bmap))
			*deny |= i ;
	}
}

static int
test_share(struct nfs4_stateid *stp, struct nfsd4_open *open) {
	unsigned int access, deny;

	set_access(&access, stp->st_access_bmap);
	set_deny(&deny, stp->st_deny_bmap);
	if ((access & open->op_share_deny) || (deny & open->op_share_access))
		return 0;
	return 1;
}

/*
 * Called to check deny when READ with all zero stateid or
 * WRITE with all zero or all one stateid
 */
int
nfs4_share_conflict(struct svc_fh *current_fh, unsigned int deny_type)
{
	struct inode *ino = current_fh->fh_dentry->d_inode;
	struct nfs4_file *fp;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfs4_share_conflict\n");

	fp = find_file(ino);
	if (fp) {
	/* Search for conflicting share reservations */
		list_for_each_entry(stp, &fp->fi_perfile, st_perfile) {
			if (test_bit(deny_type, &stp->st_deny_bmap) ||
			    test_bit(NFS4_SHARE_DENY_BOTH, &stp->st_deny_bmap))
				return nfserr_share_denied;
		}
	}
	return nfs_ok;
}

static inline void
nfs4_file_downgrade(struct file *filp, unsigned int share_access)
{
	if (share_access & NFS4_SHARE_ACCESS_WRITE) {
		put_write_access(filp->f_dentry->d_inode);
		filp->f_mode = (filp->f_mode | FMODE_READ) & ~FMODE_WRITE;
	}
}

/*
 * Recall a delegation
 */
static int
do_recall(void *__dp)
{
	struct nfs4_delegation *dp = __dp;

	daemonize("nfsv4-recall");

	nfsd4_cb_recall(dp);
	return 0;
}

/*
 * Spawn a thread to perform a recall on the delegation represented
 * by the lease (file_lock)
 *
 * Called from break_lease() with lock_kernel() held.
 * Note: we assume break_lease will only call this *once* for any given
 * lease.
 */
static
void nfsd_break_deleg_cb(struct file_lock *fl)
{
	struct nfs4_delegation *dp=  (struct nfs4_delegation *)fl->fl_owner;
	struct task_struct *t;

	dprintk("NFSD nfsd_break_deleg_cb: dp %p fl %p\n",dp,fl);
	if (!dp)
		return;

	/* We're assuming the state code never drops its reference
	 * without first removing the lease.  Since we're in this lease
	 * callback (and since the lease code is serialized by the kernel
	 * lock) we know the server hasn't removed the lease yet, we know
	 * it's safe to take a reference: */
	atomic_inc(&dp->dl_count);

	spin_lock(&recall_lock);
	list_add_tail(&dp->dl_recall_lru, &del_recall_lru);
	spin_unlock(&recall_lock);

	/* only place dl_time is set. protected by lock_kernel*/
	dp->dl_time = get_seconds();

	/* XXX need to merge NFSD_LEASE_TIME with fs/locks.c:lease_break_time */
	fl->fl_break_time = jiffies + NFSD_LEASE_TIME * HZ;

	t = kthread_run(do_recall, dp, "%s", "nfs4_cb_recall");
	if (IS_ERR(t)) {
		struct nfs4_client *clp = dp->dl_client;

		printk(KERN_INFO "NFSD: Callback thread failed for "
			"for client (clientid %08x/%08x)\n",
			clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);
		nfs4_put_delegation(dp);
	}
}

/*
 * The file_lock is being reapd.
 *
 * Called by locks_free_lock() with lock_kernel() held.
 */
static
void nfsd_release_deleg_cb(struct file_lock *fl)
{
	struct nfs4_delegation *dp = (struct nfs4_delegation *)fl->fl_owner;

	dprintk("NFSD nfsd_release_deleg_cb: fl %p dp %p dl_count %d\n", fl,dp, atomic_read(&dp->dl_count));

	if (!(fl->fl_flags & FL_LEASE) || !dp)
		return;
	dp->dl_flock = NULL;
}

/*
 * Set the delegation file_lock back pointer.
 *
 * Called from __setlease() with lock_kernel() held.
 */
static
void nfsd_copy_lock_deleg_cb(struct file_lock *new, struct file_lock *fl)
{
	struct nfs4_delegation *dp = (struct nfs4_delegation *)new->fl_owner;

	dprintk("NFSD: nfsd_copy_lock_deleg_cb: new fl %p dp %p\n", new, dp);
	if (!dp)
		return;
	dp->dl_flock = new;
}

/*
 * Called from __setlease() with lock_kernel() held
 */
static
int nfsd_same_client_deleg_cb(struct file_lock *onlist, struct file_lock *try)
{
	struct nfs4_delegation *onlistd =
		(struct nfs4_delegation *)onlist->fl_owner;
	struct nfs4_delegation *tryd =
		(struct nfs4_delegation *)try->fl_owner;

	if (onlist->fl_lmops != try->fl_lmops)
		return 0;

	return onlistd->dl_client == tryd->dl_client;
}


static
int nfsd_change_deleg_cb(struct file_lock **onlist, int arg)
{
	if (arg & F_UNLCK)
		return lease_modify(onlist, arg);
	else
		return -EAGAIN;
}

struct lock_manager_operations nfsd_lease_mng_ops = {
	.fl_break = nfsd_break_deleg_cb,
	.fl_release_private = nfsd_release_deleg_cb,
	.fl_copy_lock = nfsd_copy_lock_deleg_cb,
	.fl_mylease = nfsd_same_client_deleg_cb,
	.fl_change = nfsd_change_deleg_cb,
};


/*
 * nfsd4_process_open1()
 * 	lookup stateowner.
 * 		found:
 * 			check confirmed 
 * 				confirmed:
 * 					check seqid
 * 				not confirmed:
 * 					delete owner
 * 					create new owner
 * 		notfound:
 * 			verify clientid
 * 			create new owner
 *
 * called with nfs4_lock_state() held.
 */
int
nfsd4_process_open1(struct nfsd4_open *open)
{
	int status;
	clientid_t *clientid = &open->op_clientid;
	struct nfs4_client *clp = NULL;
	unsigned int strhashval;
	struct nfs4_stateowner *sop = NULL;

	status = nfserr_inval;
	if (!check_name(open->op_owner))
		goto out;

	if (STALE_CLIENTID(&open->op_clientid))
		return nfserr_stale_clientid;

	strhashval = ownerstr_hashval(clientid->cl_id, open->op_owner);
	sop = find_openstateowner_str(strhashval, open);
	if (sop) {
		open->op_stateowner = sop;
		/* check for replay */
		if (open->op_seqid == sop->so_seqid){
			if (sop->so_replay.rp_buflen)
				return NFSERR_REPLAY_ME;
			else {
				/* The original OPEN failed so spectacularly
				 * that we don't even have replay data saved!
				 * Therefore, we have no choice but to continue
				 * processing this OPEN; presumably, we'll
				 * fail again for the same reason.
				 */
				dprintk("nfsd4_process_open1:"
					" replay with no replay cache\n");
				goto renew;
			}
		} else if (sop->so_confirmed) {
			if (open->op_seqid == sop->so_seqid + 1)
				goto renew;
			status = nfserr_bad_seqid;
			goto out;
		} else {
			/* If we get here, we received an OPEN for an
			 * unconfirmed nfs4_stateowner. Since the seqid's are
			 * different, purge the existing nfs4_stateowner, and
			 * instantiate a new one.
			 */
			clp = sop->so_client;
			release_stateowner(sop);
		}
	} else {
		/* nfs4_stateowner not found.
		 * Verify clientid and instantiate new nfs4_stateowner.
		 * If verify fails this is presumably the result of the
		 * client's lease expiring.
		 */
		status = nfserr_expired;
		clp = find_confirmed_client(clientid);
		if (clp == NULL)
			goto out;
	}
	status = nfserr_resource;
	sop = alloc_init_open_stateowner(strhashval, clp, open);
	if (sop == NULL)
		goto out;
	open->op_stateowner = sop;
renew:
	status = nfs_ok;
	renew_client(sop->so_client);
out:
	if (status && open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS)
		status = nfserr_reclaim_bad;
	return status;
}

static int
nfs4_check_open(struct nfs4_file *fp, struct nfsd4_open *open, struct nfs4_stateid **stpp)
{
	struct nfs4_stateid *local;
	int status = nfserr_share_denied;
	struct nfs4_stateowner *sop = open->op_stateowner;

	list_for_each_entry(local, &fp->fi_perfile, st_perfile) {
		/* ignore lock owners */
		if (local->st_stateowner->so_is_open_owner == 0)
			continue;
		/* remember if we have seen this open owner */
		if (local->st_stateowner == sop)
			*stpp = local;
		/* check for conflicting share reservations */
		if (!test_share(local, open))
			goto out;
	}
	status = 0;
out:
	return status;
}

static int
nfs4_new_open(struct svc_rqst *rqstp, struct nfs4_stateid **stpp,
		struct svc_fh *cur_fh, int flags)
{
	struct nfs4_stateid *stp;
	int status;

	stp = kmalloc(sizeof(struct nfs4_stateid), GFP_KERNEL);
	if (stp == NULL)
		return nfserr_resource;

	status = nfsd_open(rqstp, cur_fh, S_IFREG, flags, &stp->st_vfs_file);
	if (status) {
		if (status == nfserr_dropit)
			status = nfserr_jukebox;
		kfree(stp);
		return status;
	}
	vfsopen++;
	*stpp = stp;
	return 0;
}

static inline int
nfsd4_truncate(struct svc_rqst *rqstp, struct svc_fh *fh,
		struct nfsd4_open *open)
{
	struct iattr iattr = {
		.ia_valid = ATTR_SIZE,
		.ia_size = 0,
	};
	if (!open->op_truncate)
		return 0;
	if (!(open->op_share_access & NFS4_SHARE_ACCESS_WRITE))
		return -EINVAL;
	return nfsd_setattr(rqstp, fh, &iattr, 0, (time_t)0);
}

static int
nfs4_upgrade_open(struct svc_rqst *rqstp, struct svc_fh *cur_fh, struct nfs4_stateid *stp, struct nfsd4_open *open)
{
	struct file *filp = stp->st_vfs_file;
	struct inode *inode = filp->f_dentry->d_inode;
	unsigned int share_access;
	int status;

	set_access(&share_access, stp->st_access_bmap);
	share_access = ~share_access;
	share_access &= open->op_share_access;

	if (!(share_access & NFS4_SHARE_ACCESS_WRITE))
		return nfsd4_truncate(rqstp, cur_fh, open);

	status = get_write_access(inode);
	if (status)
		return nfserrno(status);
	status = nfsd4_truncate(rqstp, cur_fh, open);
	if (status) {
		put_write_access(inode);
		return status;
	}
	/* remember the open */
	filp->f_mode = (filp->f_mode | FMODE_WRITE) & ~FMODE_READ;
	set_bit(open->op_share_access, &stp->st_access_bmap);
	set_bit(open->op_share_deny, &stp->st_deny_bmap);

	return nfs_ok;
}


/* decrement seqid on successful reclaim, it will be bumped in encode_open */
static void
nfs4_set_claim_prev(struct nfsd4_open *open, int *status)
{
	if (open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS) {
		if (*status)
			*status = nfserr_reclaim_bad;
		else {
			open->op_stateowner->so_confirmed = 1;
			open->op_stateowner->so_seqid--;
		}
	}
}

/*
 * Attempt to hand out a delegation.
 */
static void
nfs4_open_delegation(struct svc_fh *fh, struct nfsd4_open *open, struct nfs4_stateid *stp)
{
	struct nfs4_delegation *dp;
	struct nfs4_stateowner *sop = stp->st_stateowner;
	struct nfs4_callback *cb = &sop->so_client->cl_callback;
	struct file_lock fl, *flp = &fl;
	int status, flag = 0;

	flag = NFS4_OPEN_DELEGATE_NONE;
	if (open->op_claim_type != NFS4_OPEN_CLAIM_NULL
	     || !atomic_read(&cb->cb_set) || !sop->so_confirmed)
		goto out;

	if (open->op_share_access & NFS4_SHARE_ACCESS_WRITE)
		flag = NFS4_OPEN_DELEGATE_WRITE;
	else
		flag = NFS4_OPEN_DELEGATE_READ;

	dp = alloc_init_deleg(sop->so_client, stp, fh, flag);
	if (dp == NULL) {
		flag = NFS4_OPEN_DELEGATE_NONE;
		goto out;
	}
	locks_init_lock(&fl);
	fl.fl_lmops = &nfsd_lease_mng_ops;
	fl.fl_flags = FL_LEASE;
	fl.fl_end = OFFSET_MAX;
	fl.fl_owner =  (fl_owner_t)dp;
	fl.fl_file = stp->st_vfs_file;
	fl.fl_pid = current->tgid;

	/* setlease checks to see if delegation should be handed out.
	 * the lock_manager callbacks fl_mylease and fl_change are used
	 */
	if ((status = setlease(stp->st_vfs_file,
		flag == NFS4_OPEN_DELEGATE_READ? F_RDLCK: F_WRLCK, &flp))) {
		dprintk("NFSD: setlease failed [%d], no delegation\n", status);
		list_del(&dp->dl_del_perfile);
		list_del(&dp->dl_del_perclnt);
		nfs4_put_delegation(dp);
		free_delegation++;
		flag = NFS4_OPEN_DELEGATE_NONE;
		goto out;
	}

	memcpy(&open->op_delegate_stateid, &dp->dl_stateid, sizeof(dp->dl_stateid));

	dprintk("NFSD: delegation stateid=(%08x/%08x/%08x/%08x)\n\n",
	             dp->dl_stateid.si_boot,
	             dp->dl_stateid.si_stateownerid,
	             dp->dl_stateid.si_fileid,
	             dp->dl_stateid.si_generation);
out:
	open->op_delegate_type = flag;
}

/*
 * called with nfs4_lock_state() held.
 */
int
nfsd4_process_open2(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open)
{
	struct nfs4_file *fp = NULL;
	struct inode *ino = current_fh->fh_dentry->d_inode;
	struct nfs4_stateid *stp = NULL;
	int status;

	status = nfserr_inval;
	if (!TEST_ACCESS(open->op_share_access) || !TEST_DENY(open->op_share_deny))
		goto out;
	/*
	 * Lookup file; if found, lookup stateid and check open request,
	 * and check for delegations in the process of being recalled.
	 * If not found, create the nfs4_file struct
	 */
	fp = find_file(ino);
	if (fp) {
		if ((status = nfs4_check_open(fp, open, &stp)))
			goto out;
	} else {
		status = nfserr_resource;
		fp = alloc_init_file(ino);
		if (fp == NULL)
			goto out;
	}

	/*
	 * OPEN the file, or upgrade an existing OPEN.
	 * If truncate fails, the OPEN fails.
	 */
	if (stp) {
		/* Stateid was found, this is an OPEN upgrade */
		status = nfs4_upgrade_open(rqstp, current_fh, stp, open);
		if (status)
			goto out;
	} else {
		/* Stateid was not found, this is a new OPEN */
		int flags = 0;
		if (open->op_share_access & NFS4_SHARE_ACCESS_WRITE)
			flags = MAY_WRITE;
		else
			flags = MAY_READ;
		if ((status = nfs4_new_open(rqstp, &stp, current_fh, flags)))
			goto out;
		init_stateid(stp, fp, open);
		status = nfsd4_truncate(rqstp, current_fh, open);
		if (status) {
			release_stateid(stp, OPEN_STATE);
			goto out;
		}
	}
	memcpy(&open->op_stateid, &stp->st_stateid, sizeof(stateid_t));

	/*
	* Attempt to hand out a delegation. No error return, because the
	* OPEN succeeds even if we fail.
	*/
	nfs4_open_delegation(current_fh, open, stp);

	status = nfs_ok;

	dprintk("nfs4_process_open2: stateid=(%08x/%08x/%08x/%08x)\n",
	            stp->st_stateid.si_boot, stp->st_stateid.si_stateownerid,
	            stp->st_stateid.si_fileid, stp->st_stateid.si_generation);
out:
	/* take the opportunity to clean up unused state */
	if (fp && list_empty(&fp->fi_perfile) && list_empty(&fp->fi_del_perfile))
		release_file(fp);

	/* CLAIM_PREVIOUS has different error returns */
	nfs4_set_claim_prev(open, &status);
	/*
	* To finish the open response, we just need to set the rflags.
	*/
	open->op_rflags = NFS4_OPEN_RESULT_LOCKTYPE_POSIX;
	if (!open->op_stateowner->so_confirmed)
		open->op_rflags |= NFS4_OPEN_RESULT_CONFIRM;

	return status;
}

static struct work_struct laundromat_work;
static void laundromat_main(void *);
static DECLARE_WORK(laundromat_work, laundromat_main, NULL);

int 
nfsd4_renew(clientid_t *clid)
{
	struct nfs4_client *clp;
	int status;

	nfs4_lock_state();
	dprintk("process_renew(%08x/%08x): starting\n", 
			clid->cl_boot, clid->cl_id);
	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(clid))
		goto out;
	clp = find_confirmed_client(clid);
	status = nfserr_expired;
	if (clp == NULL) {
		/* We assume the client took too long to RENEW. */
		dprintk("nfsd4_renew: clientid not found!\n");
		goto out;
	}
	renew_client(clp);
	status = nfserr_cb_path_down;
	if (!list_empty(&clp->cl_del_perclnt)
			&& !atomic_read(&clp->cl_callback.cb_set))
		goto out;
	status = nfs_ok;
out:
	nfs4_unlock_state();
	return status;
}

time_t
nfs4_laundromat(void)
{
	struct nfs4_client *clp;
	struct nfs4_stateowner *sop;
	struct nfs4_delegation *dp;
	struct list_head *pos, *next, reaplist;
	time_t cutoff = get_seconds() - NFSD_LEASE_TIME;
	time_t t, clientid_val = NFSD_LEASE_TIME;
	time_t u, test_val = NFSD_LEASE_TIME;

	nfs4_lock_state();

	dprintk("NFSD: laundromat service - starting\n");
	list_for_each_safe(pos, next, &client_lru) {
		clp = list_entry(pos, struct nfs4_client, cl_lru);
		if (time_after((unsigned long)clp->cl_time, (unsigned long)cutoff)) {
			t = clp->cl_time - cutoff;
			if (clientid_val > t)
				clientid_val = t;
			break;
		}
		dprintk("NFSD: purging unused client (clientid %08x)\n",
			clp->cl_clientid.cl_id);
		expire_client(clp);
	}
	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	list_for_each_safe(pos, next, &del_recall_lru) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		if (time_after((unsigned long)dp->dl_time, (unsigned long)cutoff)) {
			u = dp->dl_time - cutoff;
			if (test_val > u)
				test_val = u;
			break;
		}
		dprintk("NFSD: purging unused delegation dp %p, fp %p\n",
			            dp, dp->dl_flock);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	list_for_each_safe(pos, next, &reaplist) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		unhash_delegation(dp);
	}
	test_val = NFSD_LEASE_TIME;
	list_for_each_safe(pos, next, &close_lru) {
		sop = list_entry(pos, struct nfs4_stateowner, so_close_lru);
		if (time_after((unsigned long)sop->so_time, (unsigned long)cutoff)) {
			u = sop->so_time - cutoff;
			if (test_val > u)
				test_val = u;
			break;
		}
		dprintk("NFSD: purging unused open stateowner (so_id %d)\n",
			sop->so_id);
		list_del(&sop->so_close_lru);
		nfs4_put_stateowner(sop);
	}
	if (clientid_val < NFSD_LAUNDROMAT_MINTIMEOUT)
		clientid_val = NFSD_LAUNDROMAT_MINTIMEOUT;
	nfs4_unlock_state();
	return clientid_val;
}

void
laundromat_main(void *not_used)
{
	time_t t;

	t = nfs4_laundromat();
	dprintk("NFSD: laundromat_main - sleeping for %ld seconds\n", t);
	schedule_delayed_work(&laundromat_work, t*HZ);
}

/* search ownerid_hashtbl[] and close_lru for stateid owner
 * (stateid->si_stateownerid)
 */
struct nfs4_stateowner *
find_openstateowner_id(u32 st_id, int flags) {
	struct nfs4_stateowner *local = NULL;

	dprintk("NFSD: find_openstateowner_id %d\n", st_id);
	if (flags & CLOSE_STATE) {
		list_for_each_entry(local, &close_lru, so_close_lru) {
			if (local->so_id == st_id)
				return local;
		}
	}
	return NULL;
}

static inline int
nfs4_check_fh(struct svc_fh *fhp, struct nfs4_stateid *stp)
{
	return fhp->fh_dentry->d_inode != stp->st_vfs_file->f_dentry->d_inode;
}

static int
STALE_STATEID(stateid_t *stateid)
{
	if (stateid->si_boot == boot_time)
		return 0;
	printk("NFSD: stale stateid (%08x/%08x/%08x/%08x)!\n",
		stateid->si_boot, stateid->si_stateownerid, stateid->si_fileid,
		stateid->si_generation);
	return 1;
}

static inline int
access_permit_read(unsigned long access_bmap)
{
	return test_bit(NFS4_SHARE_ACCESS_READ, &access_bmap) ||
		test_bit(NFS4_SHARE_ACCESS_BOTH, &access_bmap) ||
		test_bit(NFS4_SHARE_ACCESS_WRITE, &access_bmap);
}

static inline int
access_permit_write(unsigned long access_bmap)
{
	return test_bit(NFS4_SHARE_ACCESS_WRITE, &access_bmap) ||
		test_bit(NFS4_SHARE_ACCESS_BOTH, &access_bmap);
}

static
int nfs4_check_openmode(struct nfs4_stateid *stp, int flags)
{
        int status = nfserr_openmode;

	if ((flags & WR_STATE) && (!access_permit_write(stp->st_access_bmap)))
                goto out;
	if ((flags & RD_STATE) && (!access_permit_read(stp->st_access_bmap)))
                goto out;
	status = nfs_ok;
out:
	return status;
}

static inline int
nfs4_check_delegmode(struct nfs4_delegation *dp, int flags)
{
	if ((flags & WR_STATE) && (dp->dl_type == NFS4_OPEN_DELEGATE_READ))
		return nfserr_openmode;
	else
		return nfs_ok;
}

static inline int
check_special_stateids(svc_fh *current_fh, stateid_t *stateid, int flags)
{
	/* Trying to call delegreturn with a special stateid? Yuch: */
	if (!(flags & (RD_STATE | WR_STATE)))
		return nfserr_bad_stateid;
	else if (ONE_STATEID(stateid) && (flags & RD_STATE))
		return nfs_ok;
	else if (nfs4_in_grace()) {
		/* Answer in remaining cases depends on existance of
		 * conflicting state; so we must wait out the grace period. */
		return nfserr_grace;
	} else if (flags & WR_STATE)
		return nfs4_share_conflict(current_fh,
				NFS4_SHARE_DENY_WRITE);
	else /* (flags & RD_STATE) && ZERO_STATEID(stateid) */
		return nfs4_share_conflict(current_fh,
				NFS4_SHARE_DENY_READ);
}

/*
 * Allow READ/WRITE during grace period on recovered state only for files
 * that are not able to provide mandatory locking.
 */
static inline int
io_during_grace_disallowed(struct inode *inode, int flags)
{
	return nfs4_in_grace() && (flags & (RD_STATE | WR_STATE))
		&& MANDATORY_LOCK(inode);
}

/*
* Checks for stateid operations
*/
int
nfs4_preprocess_stateid_op(struct svc_fh *current_fh, stateid_t *stateid, int flags, struct file **filpp)
{
	struct nfs4_stateid *stp = NULL;
	struct nfs4_delegation *dp = NULL;
	stateid_t *stidp;
	struct inode *ino = current_fh->fh_dentry->d_inode;
	int status;

	dprintk("NFSD: preprocess_stateid_op: stateid = (%08x/%08x/%08x/%08x)\n",
		stateid->si_boot, stateid->si_stateownerid, 
		stateid->si_fileid, stateid->si_generation); 
	if (filpp)
		*filpp = NULL;

	if (io_during_grace_disallowed(ino, flags))
		return nfserr_grace;

	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid))
		return check_special_stateids(current_fh, stateid, flags);

	/* STALE STATEID */
	status = nfserr_stale_stateid;
	if (STALE_STATEID(stateid)) 
		goto out;

	/* BAD STATEID */
	status = nfserr_bad_stateid;
	if (!stateid->si_fileid) { /* delegation stateid */
		if(!(dp = find_delegation_stateid(ino, stateid))) {
			dprintk("NFSD: delegation stateid not found\n");
			if (nfs4_in_grace())
				status = nfserr_grace;
			goto out;
		}
		stidp = &dp->dl_stateid;
	} else { /* open or lock stateid */
		if (!(stp = find_stateid(stateid, flags))) {
			dprintk("NFSD: open or lock stateid not found\n");
			if (nfs4_in_grace())
				status = nfserr_grace;
			goto out;
		}
		if ((flags & CHECK_FH) && nfs4_check_fh(current_fh, stp))
			goto out;
		if (!stp->st_stateowner->so_confirmed)
			goto out;
		stidp = &stp->st_stateid;
	}
	if (stateid->si_generation > stidp->si_generation)
		goto out;

	/* OLD STATEID */
	status = nfserr_old_stateid;
	if (stateid->si_generation < stidp->si_generation)
		goto out;
	if (stp) {
		if ((status = nfs4_check_openmode(stp,flags)))
			goto out;
		renew_client(stp->st_stateowner->so_client);
		if (filpp)
			*filpp = stp->st_vfs_file;
	} else if (dp) {
		if ((status = nfs4_check_delegmode(dp, flags)))
			goto out;
		renew_client(dp->dl_client);
		if (flags & DELEG_RET)
			unhash_delegation(dp);
		if (filpp)
			*filpp = dp->dl_vfs_file;
	}
	status = nfs_ok;
out:
	return status;
}


/* 
 * Checks for sequence id mutating operations. 
 */
int
nfs4_preprocess_seqid_op(struct svc_fh *current_fh, u32 seqid, stateid_t *stateid, int flags, struct nfs4_stateowner **sopp, struct nfs4_stateid **stpp, clientid_t *lockclid)
{
	int status;
	struct nfs4_stateid *stp;
	struct nfs4_stateowner *sop;

	dprintk("NFSD: preprocess_seqid_op: seqid=%d " 
			"stateid = (%08x/%08x/%08x/%08x)\n", seqid,
		stateid->si_boot, stateid->si_stateownerid, stateid->si_fileid,
		stateid->si_generation);
			        
	*stpp = NULL;
	*sopp = NULL;

	status = nfserr_bad_stateid;
	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid)) {
		printk("NFSD: preprocess_seqid_op: magic stateid!\n");
		goto out;
	}

	status = nfserr_stale_stateid;
	if (STALE_STATEID(stateid))
		goto out;
	/*
	* We return BAD_STATEID if filehandle doesn't match stateid, 
	* the confirmed flag is incorrecly set, or the generation 
	* number is incorrect.  
	* If there is no entry in the openfile table for this id, 
	* we can't always return BAD_STATEID;
	* this might be a retransmitted CLOSE which has arrived after 
	* the openfile has been released.
	*/
	if (!(stp = find_stateid(stateid, flags)))
		goto no_nfs4_stateid;

	status = nfserr_bad_stateid;

	/* for new lock stateowners:
	 * check that the lock->v.new.open_stateid
	 * refers to an open stateowner
	 *
	 * check that the lockclid (nfs4_lock->v.new.clientid) is the same
	 * as the open_stateid->st_stateowner->so_client->clientid
	 */
	if (lockclid) {
		struct nfs4_stateowner *sop = stp->st_stateowner;
		struct nfs4_client *clp = sop->so_client;

		if (!sop->so_is_open_owner)
			goto out;
		if (!cmp_clid(&clp->cl_clientid, lockclid))
			goto out;
	}

	if ((flags & CHECK_FH) && nfs4_check_fh(current_fh, stp)) {
		printk("NFSD: preprocess_seqid_op: fh-stateid mismatch!\n");
		goto out;
	}

	*stpp = stp;
	*sopp = sop = stp->st_stateowner;

	/*
	*  We now validate the seqid and stateid generation numbers.
	*  For the moment, we ignore the possibility of 
	*  generation number wraparound.
	*/
	if (seqid != sop->so_seqid + 1)
		goto check_replay;

	if (sop->so_confirmed) {
		if (flags & CONFIRM) {
			printk("NFSD: preprocess_seqid_op: expected unconfirmed stateowner!\n");
			goto out;
		}
	}
	else {
		if (!(flags & CONFIRM)) {
			printk("NFSD: preprocess_seqid_op: stateowner not confirmed yet!\n");
			goto out;
		}
	}
	if (stateid->si_generation > stp->st_stateid.si_generation) {
		printk("NFSD: preprocess_seqid_op: future stateid?!\n");
		goto out;
	}

	status = nfserr_old_stateid;
	if (stateid->si_generation < stp->st_stateid.si_generation) {
		printk("NFSD: preprocess_seqid_op: old stateid!\n");
		goto out;
	}
	/* XXX renew the client lease here */
	status = nfs_ok;

out:
	return status;

no_nfs4_stateid:

	/*
	* We determine whether this is a bad stateid or a replay, 
	* starting by trying to look up the stateowner.
	* If stateowner is not found - stateid is bad.
	*/
	if (!(sop = find_openstateowner_id(stateid->si_stateownerid, flags))) {
		printk("NFSD: preprocess_seqid_op: no stateowner or nfs4_stateid!\n");
		status = nfserr_bad_stateid;
		goto out;
	}
	*sopp = sop;

check_replay:
	if (seqid == sop->so_seqid) {
		printk("NFSD: preprocess_seqid_op: retransmission?\n");
		/* indicate replay to calling function */
		status = NFSERR_REPLAY_ME;
	} else  {
		printk("NFSD: preprocess_seqid_op: bad seqid (expected %d, got %d\n", sop->so_seqid +1, seqid);

		*sopp = NULL;
		status = nfserr_bad_seqid;
	}
	goto out;
}

int
nfsd4_open_confirm(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open_confirm *oc)
{
	int status;
	struct nfs4_stateowner *sop;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfsd4_open_confirm on file %.*s\n",
			(int)current_fh->fh_dentry->d_name.len,
			current_fh->fh_dentry->d_name.name);

	if ((status = fh_verify(rqstp, current_fh, S_IFREG, 0)))
		goto out;

	nfs4_lock_state();

	if ((status = nfs4_preprocess_seqid_op(current_fh, oc->oc_seqid,
					&oc->oc_req_stateid,
					CHECK_FH | CONFIRM | OPEN_STATE,
					&oc->oc_stateowner, &stp, NULL)))
		goto out; 

	sop = oc->oc_stateowner;
	sop->so_confirmed = 1;
	update_stateid(&stp->st_stateid);
	memcpy(&oc->oc_resp_stateid, &stp->st_stateid, sizeof(stateid_t));
	dprintk("NFSD: nfsd4_open_confirm: success, seqid=%d " 
		"stateid=(%08x/%08x/%08x/%08x)\n", oc->oc_seqid,
		         stp->st_stateid.si_boot,
		         stp->st_stateid.si_stateownerid,
		         stp->st_stateid.si_fileid,
		         stp->st_stateid.si_generation);
out:
	if (oc->oc_stateowner)
		nfs4_get_stateowner(oc->oc_stateowner);
	nfs4_unlock_state();
	return status;
}


/*
 * unset all bits in union bitmap (bmap) that
 * do not exist in share (from successful OPEN_DOWNGRADE)
 */
static void
reset_union_bmap_access(unsigned long access, unsigned long *bmap)
{
	int i;
	for (i = 1; i < 4; i++) {
		if ((i & access) != i)
			__clear_bit(i, bmap);
	}
}

static void
reset_union_bmap_deny(unsigned long deny, unsigned long *bmap)
{
	int i;
	for (i = 0; i < 4; i++) {
		if ((i & deny) != i)
			__clear_bit(i, bmap);
	}
}

int
nfsd4_open_downgrade(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open_downgrade *od)
{
	int status;
	struct nfs4_stateid *stp;
	unsigned int share_access;

	dprintk("NFSD: nfsd4_open_downgrade on file %.*s\n", 
			(int)current_fh->fh_dentry->d_name.len,
			current_fh->fh_dentry->d_name.name);

	if (!TEST_ACCESS(od->od_share_access) || !TEST_DENY(od->od_share_deny))
		return nfserr_inval;

	nfs4_lock_state();
	if ((status = nfs4_preprocess_seqid_op(current_fh, od->od_seqid, 
					&od->od_stateid, 
					CHECK_FH | OPEN_STATE, 
					&od->od_stateowner, &stp, NULL)))
		goto out; 

	status = nfserr_inval;
	if (!test_bit(od->od_share_access, &stp->st_access_bmap)) {
		dprintk("NFSD:access not a subset current bitmap: 0x%lx, input access=%08x\n",
			stp->st_access_bmap, od->od_share_access);
		goto out;
	}
	if (!test_bit(od->od_share_deny, &stp->st_deny_bmap)) {
		dprintk("NFSD:deny not a subset current bitmap: 0x%lx, input deny=%08x\n",
			stp->st_deny_bmap, od->od_share_deny);
		goto out;
	}
	set_access(&share_access, stp->st_access_bmap);
	nfs4_file_downgrade(stp->st_vfs_file,
	                    share_access & ~od->od_share_access);

	reset_union_bmap_access(od->od_share_access, &stp->st_access_bmap);
	reset_union_bmap_deny(od->od_share_deny, &stp->st_deny_bmap);

	update_stateid(&stp->st_stateid);
	memcpy(&od->od_stateid, &stp->st_stateid, sizeof(stateid_t));
	status = nfs_ok;
out:
	if (od->od_stateowner)
		nfs4_get_stateowner(od->od_stateowner);
	nfs4_unlock_state();
	return status;
}

/*
 * nfs4_unlock_state() called after encode
 */
int
nfsd4_close(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_close *close)
{
	int status;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfsd4_close on file %.*s\n", 
			(int)current_fh->fh_dentry->d_name.len,
			current_fh->fh_dentry->d_name.name);

	nfs4_lock_state();
	/* check close_lru for replay */
	if ((status = nfs4_preprocess_seqid_op(current_fh, close->cl_seqid, 
					&close->cl_stateid, 
					CHECK_FH | OPEN_STATE | CLOSE_STATE,
					&close->cl_stateowner, &stp, NULL)))
		goto out; 
	/*
	*  Return success, but first update the stateid.
	*/
	status = nfs_ok;
	update_stateid(&stp->st_stateid);
	memcpy(&close->cl_stateid, &stp->st_stateid, sizeof(stateid_t));

	/* release_state_owner() calls nfsd_close() if needed */
	release_state_owner(stp, OPEN_STATE);
out:
	if (close->cl_stateowner)
		nfs4_get_stateowner(close->cl_stateowner);
	nfs4_unlock_state();
	return status;
}

int
nfsd4_delegreturn(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_delegreturn *dr)
{
	int status;

	if ((status = fh_verify(rqstp, current_fh, S_IFREG, 0)))
		goto out;

	nfs4_lock_state();
	status = nfs4_preprocess_stateid_op(current_fh, &dr->dr_stateid, DELEG_RET, NULL);
	nfs4_unlock_state();
out:
	return status;
}


/* 
 * Lock owner state (byte-range locks)
 */
#define LOFF_OVERFLOW(start, len)      ((u64)(len) > ~(u64)(start))
#define LOCK_HASH_BITS              8
#define LOCK_HASH_SIZE             (1 << LOCK_HASH_BITS)
#define LOCK_HASH_MASK             (LOCK_HASH_SIZE - 1)

#define lockownerid_hashval(id) \
        ((id) & LOCK_HASH_MASK)

static inline unsigned int
lock_ownerstr_hashval(struct inode *inode, u32 cl_id,
		struct xdr_netobj *ownername)
{
	return (file_hashval(inode) + cl_id
			+ opaque_hashval(ownername->data, ownername->len))
		& LOCK_HASH_MASK;
}

static struct list_head lock_ownerid_hashtbl[LOCK_HASH_SIZE];
static struct list_head	lock_ownerstr_hashtbl[LOCK_HASH_SIZE];
static struct list_head lockstateid_hashtbl[STATEID_HASH_SIZE];

struct nfs4_stateid *
find_stateid(stateid_t *stid, int flags)
{
	struct nfs4_stateid *local = NULL;
	u32 st_id = stid->si_stateownerid;
	u32 f_id = stid->si_fileid;
	unsigned int hashval;

	dprintk("NFSD: find_stateid flags 0x%x\n",flags);
	if ((flags & LOCK_STATE) || (flags & RD_STATE) || (flags & WR_STATE)) {
		hashval = stateid_hashval(st_id, f_id);
		list_for_each_entry(local, &lockstateid_hashtbl[hashval], st_hash) {
			if ((local->st_stateid.si_stateownerid == st_id) &&
			    (local->st_stateid.si_fileid == f_id))
				return local;
		}
	} 
	if ((flags & OPEN_STATE) || (flags & RD_STATE) || (flags & WR_STATE)) {
		hashval = stateid_hashval(st_id, f_id);
		list_for_each_entry(local, &stateid_hashtbl[hashval], st_hash) {
			if ((local->st_stateid.si_stateownerid == st_id) &&
			    (local->st_stateid.si_fileid == f_id))
				return local;
		}
	} else
		printk("NFSD: find_stateid: ERROR: no state flag\n");
	return NULL;
}

static struct nfs4_delegation *
find_delegation_stateid(struct inode *ino, stateid_t *stid)
{
	struct nfs4_delegation *dp = NULL;
	struct nfs4_file *fp = NULL;
	u32 st_id;

	dprintk("NFSD:find_delegation_stateid stateid=(%08x/%08x/%08x/%08x)\n",
                    stid->si_boot, stid->si_stateownerid,
                    stid->si_fileid, stid->si_generation);

	st_id = stid->si_stateownerid;
	fp = find_file(ino);
	if (fp) {
		list_for_each_entry(dp, &fp->fi_del_perfile, dl_del_perfile) {
			if(dp->dl_stateid.si_stateownerid == st_id) {
				dprintk("NFSD: find_delegation dp %p\n",dp);
				return dp;
			}
		}
	}
	return NULL;
}

/*
 * TODO: Linux file offsets are _signed_ 64-bit quantities, which means that
 * we can't properly handle lock requests that go beyond the (2^63 - 1)-th
 * byte, because of sign extension problems.  Since NFSv4 calls for 64-bit
 * locking, this prevents us from being completely protocol-compliant.  The
 * real solution to this problem is to start using unsigned file offsets in
 * the VFS, but this is a very deep change!
 */
static inline void
nfs4_transform_lock_offset(struct file_lock *lock)
{
	if (lock->fl_start < 0)
		lock->fl_start = OFFSET_MAX;
	if (lock->fl_end < 0)
		lock->fl_end = OFFSET_MAX;
}

int
nfs4_verify_lock_stateowner(struct nfs4_stateowner *sop, unsigned int hashval)
{
	struct nfs4_stateowner *local = NULL;
	int status = 0;
			        
	if (hashval >= LOCK_HASH_SIZE)
		goto out;
	list_for_each_entry(local, &lock_ownerid_hashtbl[hashval], so_idhash) {
		if (local == sop) {
			status = 1;
			goto out;
		}
	}
out:
	return status;
}


static inline void
nfs4_set_lock_denied(struct file_lock *fl, struct nfsd4_lock_denied *deny)
{
	struct nfs4_stateowner *sop = (struct nfs4_stateowner *) fl->fl_owner;
	unsigned int hval = lockownerid_hashval(sop->so_id);

	deny->ld_sop = NULL;
	if (nfs4_verify_lock_stateowner(sop, hval)) {
		kref_get(&sop->so_ref);
		deny->ld_sop = sop;
		deny->ld_clientid = sop->so_client->cl_clientid;
	}
	deny->ld_start = fl->fl_start;
	deny->ld_length = ~(u64)0;
	if (fl->fl_end != ~(u64)0)
		deny->ld_length = fl->fl_end - fl->fl_start + 1;        
	deny->ld_type = NFS4_READ_LT;
	if (fl->fl_type != F_RDLCK)
		deny->ld_type = NFS4_WRITE_LT;
}

static struct nfs4_stateowner *
find_lockstateowner(struct xdr_netobj *owner, clientid_t *clid)
{
	struct nfs4_stateowner *local = NULL;
	int i;

	for (i = 0; i < LOCK_HASH_SIZE; i++) {
		list_for_each_entry(local, &lock_ownerid_hashtbl[i], so_idhash) {
			if (!cmp_owner_str(local, owner, clid))
				continue;
			return local;
		}
	}
	return NULL;
}

static struct nfs4_stateowner *
find_lockstateowner_str(struct inode *inode, clientid_t *clid,
		struct xdr_netobj *owner)
{
	unsigned int hashval = lock_ownerstr_hashval(inode, clid->cl_id, owner);
	struct nfs4_stateowner *op;

	list_for_each_entry(op, &lock_ownerstr_hashtbl[hashval], so_strhash) {
		if (cmp_owner_str(op, owner, clid))
			return op;
	}
	return NULL;
}

/*
 * Alloc a lock owner structure.
 * Called in nfsd4_lock - therefore, OPEN and OPEN_CONFIRM (if needed) has 
 * occured. 
 *
 * strhashval = lock_ownerstr_hashval 
 * so_seqid = lock->lk_new_lock_seqid - 1: it gets bumped in encode 
 */

static struct nfs4_stateowner *
alloc_init_lock_stateowner(unsigned int strhashval, struct nfs4_client *clp, struct nfs4_stateid *open_stp, struct nfsd4_lock *lock) {
	struct nfs4_stateowner *sop;
	struct nfs4_replay *rp;
	unsigned int idhashval;

	if (!(sop = alloc_stateowner(&lock->lk_new_owner)))
		return NULL;
	idhashval = lockownerid_hashval(current_ownerid);
	INIT_LIST_HEAD(&sop->so_idhash);
	INIT_LIST_HEAD(&sop->so_strhash);
	INIT_LIST_HEAD(&sop->so_perclient);
	INIT_LIST_HEAD(&sop->so_perfilestate);
	INIT_LIST_HEAD(&sop->so_perlockowner);
	INIT_LIST_HEAD(&sop->so_close_lru); /* not used */
	sop->so_time = 0;
	list_add(&sop->so_idhash, &lock_ownerid_hashtbl[idhashval]);
	list_add(&sop->so_strhash, &lock_ownerstr_hashtbl[strhashval]);
	list_add(&sop->so_perlockowner, &open_stp->st_perlockowner);
	sop->so_is_open_owner = 0;
	sop->so_id = current_ownerid++;
	sop->so_client = clp;
	sop->so_seqid = lock->lk_new_lock_seqid - 1;
	sop->so_confirmed = 1;
	rp = &sop->so_replay;
	rp->rp_status = NFSERR_SERVERFAULT;
	rp->rp_buflen = 0;
	rp->rp_buf = rp->rp_ibuf;
	return sop;
}

struct nfs4_stateid *
alloc_init_lock_stateid(struct nfs4_stateowner *sop, struct nfs4_file *fp, struct nfs4_stateid *open_stp)
{
	struct nfs4_stateid *stp;
	unsigned int hashval = stateid_hashval(sop->so_id, fp->fi_id);

	if ((stp = kmalloc(sizeof(struct nfs4_stateid), 
					GFP_KERNEL)) == NULL)
		goto out;
	INIT_LIST_HEAD(&stp->st_hash);
	INIT_LIST_HEAD(&stp->st_perfile);
	INIT_LIST_HEAD(&stp->st_perfilestate);
	INIT_LIST_HEAD(&stp->st_perlockowner); /* not used */
	list_add(&stp->st_hash, &lockstateid_hashtbl[hashval]);
	list_add(&stp->st_perfile, &fp->fi_perfile);
	list_add_perfile++;
	list_add(&stp->st_perfilestate, &sop->so_perfilestate);
	stp->st_stateowner = sop;
	stp->st_file = fp;
	stp->st_stateid.si_boot = boot_time;
	stp->st_stateid.si_stateownerid = sop->so_id;
	stp->st_stateid.si_fileid = fp->fi_id;
	stp->st_stateid.si_generation = 0;
	stp->st_vfs_file = open_stp->st_vfs_file; /* FIXME refcount?? */
	stp->st_access_bmap = open_stp->st_access_bmap;
	stp->st_deny_bmap = open_stp->st_deny_bmap;

out:
	return stp;
}

int
check_lock_length(u64 offset, u64 length)
{
	return ((length == 0)  || ((length != ~(u64)0) &&
	     LOFF_OVERFLOW(offset, length)));
}

/*
 *  LOCK operation 
 */
int
nfsd4_lock(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_lock *lock)
{
	struct nfs4_stateowner *lock_sop = NULL, *open_sop = NULL;
	struct nfs4_stateid *lock_stp;
	struct file *filp;
	struct file_lock file_lock;
	struct file_lock *conflock;
	int status = 0;
	unsigned int strhashval;

	dprintk("NFSD: nfsd4_lock: start=%Ld length=%Ld\n",
		(long long) lock->lk_offset,
		(long long) lock->lk_length);

	if (nfs4_in_grace() && !lock->lk_reclaim)
		return nfserr_grace;
	if (!nfs4_in_grace() && lock->lk_reclaim)
		return nfserr_no_grace;

	if (check_lock_length(lock->lk_offset, lock->lk_length))
		 return nfserr_inval;

	nfs4_lock_state();

	if (lock->lk_is_new) {
	/*
	 * Client indicates that this is a new lockowner.
	 * Use open owner and open stateid to create lock owner and lock 
	 * stateid.
	 */
		struct nfs4_stateid *open_stp = NULL;
		struct nfs4_file *fp;
		
		status = nfserr_stale_clientid;
		if (STALE_CLIENTID(&lock->lk_new_clientid)) {
			printk("NFSD: nfsd4_lock: clientid is stale!\n");
			goto out;
		}

		/* is the new lock seqid presented by the client zero? */
		status = nfserr_bad_seqid;
		if (lock->v.new.lock_seqid != 0)
			goto out;

		/* validate and update open stateid and open seqid */
		status = nfs4_preprocess_seqid_op(current_fh, 
				        lock->lk_new_open_seqid,
		                        &lock->lk_new_open_stateid,
		                        CHECK_FH | OPEN_STATE,
		                        &open_sop, &open_stp,
					&lock->v.new.clientid);
		if (status) {
			if (lock->lk_reclaim)
				status = nfserr_reclaim_bad;
			goto out;
		}
		/* create lockowner and lock stateid */
		fp = open_stp->st_file;
		strhashval = lock_ownerstr_hashval(fp->fi_inode, 
				open_sop->so_client->cl_clientid.cl_id, 
				&lock->v.new.owner);
		/* 
		 * If we already have this lock owner, the client is in 
		 * error (or our bookeeping is wrong!) 
		 * for asking for a 'new lock'.
		 */
		status = nfserr_bad_stateid;
		lock_sop = find_lockstateowner(&lock->v.new.owner,
						&lock->v.new.clientid);
		if (lock_sop)
			goto out;
		status = nfserr_resource;
		if (!(lock->lk_stateowner = alloc_init_lock_stateowner(strhashval, open_sop->so_client, open_stp, lock)))
			goto out;
		if ((lock_stp = alloc_init_lock_stateid(lock->lk_stateowner, 
						fp, open_stp)) == NULL) {
			release_stateowner(lock->lk_stateowner);
			lock->lk_stateowner = NULL;
			goto out;
		}
		/* bump the open seqid used to create the lock */
		open_sop->so_seqid++;
	} else {
		/* lock (lock owner + lock stateid) already exists */
		status = nfs4_preprocess_seqid_op(current_fh,
				       lock->lk_old_lock_seqid, 
				       &lock->lk_old_lock_stateid, 
				       CHECK_FH | LOCK_STATE, 
				       &lock->lk_stateowner, &lock_stp, NULL);
		if (status)
			goto out;
	}
	/* lock->lk_stateowner and lock_stp have been created or found */
	filp = lock_stp->st_vfs_file;

	if ((status = fh_verify(rqstp, current_fh, S_IFREG, MAY_LOCK))) {
		printk("NFSD: nfsd4_lock: permission denied!\n");
		goto out;
	}

	locks_init_lock(&file_lock);
	switch (lock->lk_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			file_lock.fl_type = F_RDLCK;
		break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			file_lock.fl_type = F_WRLCK;
		break;
		default:
			status = nfserr_inval;
		goto out;
	}
	file_lock.fl_owner = (fl_owner_t) lock->lk_stateowner;
	file_lock.fl_pid = current->tgid;
	file_lock.fl_file = filp;
	file_lock.fl_flags = FL_POSIX;

	file_lock.fl_start = lock->lk_offset;
	if ((lock->lk_length == ~(u64)0) || 
			LOFF_OVERFLOW(lock->lk_offset, lock->lk_length))
		file_lock.fl_end = ~(u64)0;
	else
		file_lock.fl_end = lock->lk_offset + lock->lk_length - 1;
	nfs4_transform_lock_offset(&file_lock);

	/*
	* Try to lock the file in the VFS.
	* Note: locks.c uses the BKL to protect the inode's lock list.
	*/

	status = posix_lock_file(filp, &file_lock);
	if (file_lock.fl_ops && file_lock.fl_ops->fl_release_private)
		file_lock.fl_ops->fl_release_private(&file_lock);
	dprintk("NFSD: nfsd4_lock: posix_lock_file status %d\n",status);
	switch (-status) {
	case 0: /* success! */
		update_stateid(&lock_stp->st_stateid);
		memcpy(&lock->lk_resp_stateid, &lock_stp->st_stateid, 
				sizeof(stateid_t));
		goto out;
	case (EAGAIN):
		goto conflicting_lock;
	case (EDEADLK):
		status = nfserr_deadlock;
	default:        
		dprintk("NFSD: nfsd4_lock: posix_lock_file() failed! status %d\n",status);
		goto out_destroy_new_stateid;
	}

conflicting_lock:
	dprintk("NFSD: nfsd4_lock: conflicting lock found!\n");
	status = nfserr_denied;
	/* XXX There is a race here. Future patch needed to provide 
	 * an atomic posix_lock_and_test_file
	 */
	if (!(conflock = posix_test_lock(filp, &file_lock))) {
		status = nfserr_serverfault;
		goto out;
	}
	nfs4_set_lock_denied(conflock, &lock->lk_denied);

out_destroy_new_stateid:
	if (lock->lk_is_new) {
		dprintk("NFSD: nfsd4_lock: destroy new stateid!\n");
	/*
	* An error encountered after instantiation of the new
	* stateid has forced us to destroy it.
	*/
		if (!seqid_mutating_err(status))
			open_sop->so_seqid--;

		release_state_owner(lock_stp, LOCK_STATE);
	}
out:
	if (lock->lk_stateowner)
		nfs4_get_stateowner(lock->lk_stateowner);
	nfs4_unlock_state();
	return status;
}

/*
 * LOCKT operation
 */
int
nfsd4_lockt(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_lockt *lockt)
{
	struct inode *inode;
	struct file file;
	struct file_lock file_lock;
	struct file_lock *conflicting_lock;
	int status;

	if (nfs4_in_grace())
		return nfserr_grace;

	if (check_lock_length(lockt->lt_offset, lockt->lt_length))
		 return nfserr_inval;

	lockt->lt_stateowner = NULL;
	nfs4_lock_state();

	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(&lockt->lt_clientid)) {
		printk("NFSD: nfsd4_lockt: clientid is stale!\n");
		goto out;
	}

	if ((status = fh_verify(rqstp, current_fh, S_IFREG, 0))) {
		printk("NFSD: nfsd4_lockt: fh_verify() failed!\n");
		if (status == nfserr_symlink)
			status = nfserr_inval;
		goto out;
	}

	inode = current_fh->fh_dentry->d_inode;
	locks_init_lock(&file_lock);
	switch (lockt->lt_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			file_lock.fl_type = F_RDLCK;
		break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			file_lock.fl_type = F_WRLCK;
		break;
		default:
			printk("NFSD: nfs4_lockt: bad lock type!\n");
			status = nfserr_inval;
		goto out;
	}

	lockt->lt_stateowner = find_lockstateowner_str(inode,
			&lockt->lt_clientid, &lockt->lt_owner);
	if (lockt->lt_stateowner)
		file_lock.fl_owner = (fl_owner_t)lockt->lt_stateowner;
	file_lock.fl_pid = current->tgid;
	file_lock.fl_flags = FL_POSIX;

	file_lock.fl_start = lockt->lt_offset;
	if ((lockt->lt_length == ~(u64)0) || LOFF_OVERFLOW(lockt->lt_offset, lockt->lt_length))
		file_lock.fl_end = ~(u64)0;
	else
		file_lock.fl_end = lockt->lt_offset + lockt->lt_length - 1;

	nfs4_transform_lock_offset(&file_lock);

	/* posix_test_lock uses the struct file _only_ to resolve the inode.
	 * since LOCKT doesn't require an OPEN, and therefore a struct
	 * file may not exist, pass posix_test_lock a struct file with
	 * only the dentry:inode set.
	 */
	memset(&file, 0, sizeof (struct file));
	file.f_dentry = current_fh->fh_dentry;

	status = nfs_ok;
	conflicting_lock = posix_test_lock(&file, &file_lock);
	if (conflicting_lock) {
		status = nfserr_denied;
		nfs4_set_lock_denied(conflicting_lock, &lockt->lt_denied);
	}
out:
	nfs4_unlock_state();
	return status;
}

int
nfsd4_locku(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_locku *locku)
{
	struct nfs4_stateid *stp;
	struct file *filp = NULL;
	struct file_lock file_lock;
	int status;
						        
	dprintk("NFSD: nfsd4_locku: start=%Ld length=%Ld\n",
		(long long) locku->lu_offset,
		(long long) locku->lu_length);

	if (check_lock_length(locku->lu_offset, locku->lu_length))
		 return nfserr_inval;

	nfs4_lock_state();
									        
	if ((status = nfs4_preprocess_seqid_op(current_fh, 
					locku->lu_seqid, 
					&locku->lu_stateid, 
					CHECK_FH | LOCK_STATE, 
					&locku->lu_stateowner, &stp, NULL)))
		goto out;

	filp = stp->st_vfs_file;
	BUG_ON(!filp);
	locks_init_lock(&file_lock);
	file_lock.fl_type = F_UNLCK;
	file_lock.fl_owner = (fl_owner_t) locku->lu_stateowner;
	file_lock.fl_pid = current->tgid;
	file_lock.fl_file = filp;
	file_lock.fl_flags = FL_POSIX; 
	file_lock.fl_start = locku->lu_offset;

	if ((locku->lu_length == ~(u64)0) || LOFF_OVERFLOW(locku->lu_offset, locku->lu_length))
		file_lock.fl_end = ~(u64)0;
	else
		file_lock.fl_end = locku->lu_offset + locku->lu_length - 1;
	nfs4_transform_lock_offset(&file_lock);

	/*
	*  Try to unlock the file in the VFS.
	*/
	status = posix_lock_file(filp, &file_lock); 
	if (file_lock.fl_ops && file_lock.fl_ops->fl_release_private)
		file_lock.fl_ops->fl_release_private(&file_lock);
	if (status) {
		printk("NFSD: nfs4_locku: posix_lock_file failed!\n");
		goto out_nfserr;
	}
	/*
	* OK, unlock succeeded; the only thing left to do is update the stateid.
	*/
	update_stateid(&stp->st_stateid);
	memcpy(&locku->lu_stateid, &stp->st_stateid, sizeof(stateid_t));

out:
	if (locku->lu_stateowner)
		nfs4_get_stateowner(locku->lu_stateowner);
	nfs4_unlock_state();
	return status;

out_nfserr:
	status = nfserrno(status);
	goto out;
}

/*
 * returns
 * 	1: locks held by lockowner
 * 	0: no locks held by lockowner
 */
static int
check_for_locks(struct file *filp, struct nfs4_stateowner *lowner)
{
	struct file_lock **flpp;
	struct inode *inode = filp->f_dentry->d_inode;
	int status = 0;

	lock_kernel();
	for (flpp = &inode->i_flock; *flpp != NULL; flpp = &(*flpp)->fl_next) {
		if ((*flpp)->fl_owner == (fl_owner_t)lowner)
			status = 1;
			goto out;
	}
out:
	unlock_kernel();
	return status;
}

int
nfsd4_release_lockowner(struct svc_rqst *rqstp, struct nfsd4_release_lockowner *rlockowner)
{
	clientid_t *clid = &rlockowner->rl_clientid;
	struct nfs4_stateowner *local = NULL;
	struct xdr_netobj *owner = &rlockowner->rl_owner;
	int status;

	dprintk("nfsd4_release_lockowner clientid: (%08x/%08x):\n",
		clid->cl_boot, clid->cl_id);

	/* XXX check for lease expiration */

	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(clid)) {
		printk("NFSD: nfsd4_release_lockowner: clientid is stale!\n");
		return status;
	}

	nfs4_lock_state();

	status = nfs_ok;
	local = find_lockstateowner(owner, clid);
	if (local) {
		struct nfs4_stateid *stp;

		/* check for any locks held by any stateid
		 * associated with the (lock) stateowner */
		status = nfserr_locks_held;
		list_for_each_entry(stp, &local->so_perfilestate,
				st_perfilestate) {
			if (check_for_locks(stp->st_vfs_file, local))
				goto out;
		}
		/* no locks held by (lock) stateowner */
		status = nfs_ok;
		release_stateowner(local);
	}
out:
	nfs4_unlock_state();
	return status;
}

static inline struct nfs4_client_reclaim *
alloc_reclaim(int namelen)
{
	struct nfs4_client_reclaim *crp = NULL;

	crp = kmalloc(sizeof(struct nfs4_client_reclaim), GFP_KERNEL);
	if (!crp)
		return NULL;
	crp->cr_name.data = kmalloc(namelen, GFP_KERNEL);
	if (!crp->cr_name.data) {
		kfree(crp);
		return NULL;
	}
	return crp;
}

/*
 * failure => all reset bets are off, nfserr_no_grace...
 */
static int
nfs4_client_to_reclaim(char *name, int namlen)
{
	unsigned int strhashval;
	struct nfs4_client_reclaim *crp = NULL;

	dprintk("NFSD nfs4_client_to_reclaim NAME: %.*s\n", namlen, name);
	crp = alloc_reclaim(namlen);
	if (!crp)
		return 0;
	strhashval = clientstr_hashval(name, namlen);
	INIT_LIST_HEAD(&crp->cr_strhash);
	list_add(&crp->cr_strhash, &reclaim_str_hashtbl[strhashval]);
	memcpy(crp->cr_name.data, name, namlen);
	crp->cr_name.len = namlen;
	reclaim_str_hashtbl_size++;
	return 1;
}

static void
nfs4_release_reclaim(void)
{
	struct nfs4_client_reclaim *crp = NULL;
	int i;

	BUG_ON(!nfs4_reclaim_init);
	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		while (!list_empty(&reclaim_str_hashtbl[i])) {
			crp = list_entry(reclaim_str_hashtbl[i].next,
			                struct nfs4_client_reclaim, cr_strhash);
			list_del(&crp->cr_strhash);
			kfree(crp->cr_name.data);
			kfree(crp);
			reclaim_str_hashtbl_size--;
		}
	}
	BUG_ON(reclaim_str_hashtbl_size);
}

/*
 * called from OPEN, CLAIM_PREVIOUS with a new clientid. */
struct nfs4_client_reclaim *
nfs4_find_reclaim_client(clientid_t *clid)
{
	unsigned int strhashval;
	struct nfs4_client *clp;
	struct nfs4_client_reclaim *crp = NULL;


	/* find clientid in conf_id_hashtbl */
	clp = find_confirmed_client(clid);
	if (clp == NULL)
		return NULL;

	dprintk("NFSD: nfs4_find_reclaim_client for %.*s\n",
		            clp->cl_name.len, clp->cl_name.data);

	/* find clp->cl_name in reclaim_str_hashtbl */
	strhashval = clientstr_hashval(clp->cl_name.data, clp->cl_name.len);
	list_for_each_entry(crp, &reclaim_str_hashtbl[strhashval], cr_strhash) {
		if (cmp_name(&crp->cr_name, &clp->cl_name)) {
			return crp;
		}
	}
	return NULL;
}

/*
* Called from OPEN. Look for clientid in reclaim list.
*/
int
nfs4_check_open_reclaim(clientid_t *clid)
{
	struct nfs4_client_reclaim *crp;

	if ((crp = nfs4_find_reclaim_client(clid)) == NULL)
		return nfserr_reclaim_bad;
	return nfs_ok;
}


/* 
 * Start and stop routines
 */

static void
__nfs4_state_init(void)
{
	int i;
	time_t grace_time;

	if (!nfs4_reclaim_init) {
		for (i = 0; i < CLIENT_HASH_SIZE; i++)
			INIT_LIST_HEAD(&reclaim_str_hashtbl[i]);
		reclaim_str_hashtbl_size = 0;
		nfs4_reclaim_init = 1;
	}
	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&conf_id_hashtbl[i]);
		INIT_LIST_HEAD(&conf_str_hashtbl[i]);
		INIT_LIST_HEAD(&unconf_str_hashtbl[i]);
		INIT_LIST_HEAD(&unconf_id_hashtbl[i]);
	}
	for (i = 0; i < FILE_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&file_hashtbl[i]);
	}
	for (i = 0; i < OWNER_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&ownerstr_hashtbl[i]);
		INIT_LIST_HEAD(&ownerid_hashtbl[i]);
	}
	for (i = 0; i < STATEID_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&stateid_hashtbl[i]);
		INIT_LIST_HEAD(&lockstateid_hashtbl[i]);
	}
	for (i = 0; i < LOCK_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&lock_ownerid_hashtbl[i]);
		INIT_LIST_HEAD(&lock_ownerstr_hashtbl[i]);
	}
	memset(&zerostateid, 0, sizeof(stateid_t));
	memset(&onestateid, ~0, sizeof(stateid_t));

	INIT_LIST_HEAD(&close_lru);
	INIT_LIST_HEAD(&client_lru);
	INIT_LIST_HEAD(&del_recall_lru);
	spin_lock_init(&recall_lock);
	boot_time = get_seconds();
	grace_time = max(old_lease_time, lease_time);
	if (reclaim_str_hashtbl_size == 0)
		grace_time = 0;
	if (grace_time)
		printk("NFSD: starting %ld-second grace period\n", grace_time);
	grace_end = boot_time + grace_time;
	INIT_WORK(&laundromat_work,laundromat_main, NULL);
	schedule_delayed_work(&laundromat_work, NFSD_LEASE_TIME*HZ);
}

int
nfs4_state_init(void)
{
	int status;

	if (nfs4_init)
		return 0;
	status = nfsd4_init_slabs();
	if (status)
		return status;
	__nfs4_state_init();
	nfs4_init = 1;
	return 0;
}

int
nfs4_in_grace(void)
{
	return get_seconds() < grace_end;
}

void
set_no_grace(void)
{
	printk("NFSD: ERROR in reboot recovery.  State reclaims will fail.\n");
	grace_end = get_seconds();
}

time_t
nfs4_lease_time(void)
{
	return lease_time;
}

static void
__nfs4_state_shutdown(void)
{
	int i;
	struct nfs4_client *clp = NULL;
	struct nfs4_delegation *dp = NULL;
	struct nfs4_stateowner *sop = NULL;
	struct list_head *pos, *next, reaplist;

	list_for_each_safe(pos, next, &close_lru) {
		sop = list_entry(pos, struct nfs4_stateowner, so_close_lru);
		list_del(&sop->so_close_lru);
		nfs4_put_stateowner(sop);
	}

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		while (!list_empty(&conf_id_hashtbl[i])) {
			clp = list_entry(conf_id_hashtbl[i].next, struct nfs4_client, cl_idhash);
			expire_client(clp);
		}
		while (!list_empty(&unconf_str_hashtbl[i])) {
			clp = list_entry(unconf_str_hashtbl[i].next, struct nfs4_client, cl_strhash);
			expire_client(clp);
		}
	}
	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	list_for_each_safe(pos, next, &del_recall_lru) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	list_for_each_safe(pos, next, &reaplist) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		unhash_delegation(dp);
	}

	release_all_files();
	cancel_delayed_work(&laundromat_work);
	flush_scheduled_work();
	nfs4_init = 0;
	dprintk("NFSD: list_add_perfile %d list_del_perfile %d\n",
			list_add_perfile, list_del_perfile);
	dprintk("NFSD: add_perclient %d del_perclient %d\n",
			add_perclient, del_perclient);
	dprintk("NFSD: alloc_file %d free_file %d\n",
			alloc_file, free_file);
	dprintk("NFSD: vfsopen %d vfsclose %d\n",
			vfsopen, vfsclose);
	dprintk("NFSD: alloc_delegation %d free_delegation %d\n",
			alloc_delegation, free_delegation);

}

void
nfs4_state_shutdown(void)
{
	nfs4_lock_state();
	nfs4_release_reclaim();
	__nfs4_state_shutdown();
	nfsd4_free_slabs();
	nfs4_unlock_state();
}

/*
 * Called when leasetime is changed.
 *
 * if nfsd is not started, simply set the global lease.
 *
 * if nfsd(s) are running, lease change requires nfsv4 state to be reset.
 * e.g: boot_time is reset, existing nfs4_client structs are
 * used to fill reclaim_str_hashtbl, then all state (except for the
 * reclaim_str_hashtbl) is re-initialized.
 *
 * if the old lease time is greater than the new lease time, the grace
 * period needs to be set to the old lease time to allow clients to reclaim
 * their state. XXX - we may want to set the grace period == lease time
 * after an initial grace period == old lease time
 *
 * if an error occurs in this process, the new lease is set, but the server
 * will not honor OPEN or LOCK reclaims, and will return nfserr_no_grace
 * which means OPEN/LOCK/READ/WRITE will fail during grace period.
 *
 * clients will attempt to reset all state with SETCLIENTID/CONFIRM, and
 * OPEN and LOCK reclaims.
 */
void
nfs4_reset_lease(time_t leasetime)
{
	struct nfs4_client *clp;
	int i;

	printk("NFSD: New leasetime %ld\n",leasetime);
	if (!nfs4_init)
		return;
	nfs4_lock_state();
	old_lease_time = lease_time;
	lease_time = leasetime;

	nfs4_release_reclaim();

	/* populate reclaim_str_hashtbl with current confirmed nfs4_clientid */
	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		list_for_each_entry(clp, &conf_id_hashtbl[i], cl_idhash) {
			if (!nfs4_client_to_reclaim(clp->cl_name.data,
						clp->cl_name.len)) {
				nfs4_release_reclaim();
				goto init_state;
			}
		}
	}
init_state:
	__nfs4_state_shutdown();
	__nfs4_state_init();
	nfs4_unlock_state();
}


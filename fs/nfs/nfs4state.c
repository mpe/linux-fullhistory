/*
 *  fs/nfs/nfs4state.c
 *
 *  Client-side XDR for NFSv4.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
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
 * Implementation of the NFSv4 state model.  For the time being,
 * this is minimal, but will be made much more complex in a
 * subsequent patch.
 */

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_idmap.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>

#include "callback.h"
#include "delegation.h"

#define OPENOWNER_POOL_SIZE	8

static spinlock_t		state_spinlock = SPIN_LOCK_UNLOCKED;

nfs4_stateid zero_stateid;

#if 0
nfs4_stateid one_stateid =
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
#endif

static LIST_HEAD(nfs4_clientid_list);

static void nfs4_recover_state(void *);
extern void nfs4_renew_state(void *);

void
init_nfsv4_state(struct nfs_server *server)
{
	server->nfs4_state = NULL;
	INIT_LIST_HEAD(&server->nfs4_siblings);
}

void
destroy_nfsv4_state(struct nfs_server *server)
{
	if (server->mnt_path) {
		kfree(server->mnt_path);
		server->mnt_path = NULL;
	}
	if (server->nfs4_state) {
		nfs4_put_client(server->nfs4_state);
		server->nfs4_state = NULL;
	}
}

/*
 * nfs4_get_client(): returns an empty client structure
 * nfs4_put_client(): drops reference to client structure
 *
 * Since these are allocated/deallocated very rarely, we don't
 * bother putting them in a slab cache...
 */
static struct nfs4_client *
nfs4_alloc_client(struct in_addr *addr)
{
	struct nfs4_client *clp;

	if (nfs_callback_up() < 0)
		return NULL;
	if ((clp = kmalloc(sizeof(*clp), GFP_KERNEL)) == NULL) {
		nfs_callback_down();
		return NULL;
	}
	memset(clp, 0, sizeof(*clp));
	memcpy(&clp->cl_addr, addr, sizeof(clp->cl_addr));
	init_rwsem(&clp->cl_sem);
	INIT_LIST_HEAD(&clp->cl_delegations);
	INIT_LIST_HEAD(&clp->cl_state_owners);
	INIT_LIST_HEAD(&clp->cl_unused);
	spin_lock_init(&clp->cl_lock);
	atomic_set(&clp->cl_count, 1);
	INIT_WORK(&clp->cl_recoverd, nfs4_recover_state, clp);
	INIT_WORK(&clp->cl_renewd, nfs4_renew_state, clp);
	INIT_LIST_HEAD(&clp->cl_superblocks);
	init_waitqueue_head(&clp->cl_waitq);
	rpc_init_wait_queue(&clp->cl_rpcwaitq, "NFS4 client");
	clp->cl_state = 1 << NFS4CLNT_OK;
	return clp;
}

static void
nfs4_free_client(struct nfs4_client *clp)
{
	struct nfs4_state_owner *sp;

	while (!list_empty(&clp->cl_unused)) {
		sp = list_entry(clp->cl_unused.next,
				struct nfs4_state_owner,
				so_list);
		list_del(&sp->so_list);
		kfree(sp);
	}
	BUG_ON(!list_empty(&clp->cl_state_owners));
	if (clp->cl_cred)
		put_rpccred(clp->cl_cred);
	nfs_idmap_delete(clp);
	if (clp->cl_rpcclient)
		rpc_shutdown_client(clp->cl_rpcclient);
	kfree(clp);
	nfs_callback_down();
}

static struct nfs4_client *__nfs4_find_client(struct in_addr *addr)
{
	struct nfs4_client *clp;
	list_for_each_entry(clp, &nfs4_clientid_list, cl_servers) {
		if (memcmp(&clp->cl_addr, addr, sizeof(clp->cl_addr)) == 0) {
			atomic_inc(&clp->cl_count);
			return clp;
		}
	}
	return NULL;
}

struct nfs4_client *nfs4_find_client(struct in_addr *addr)
{
	struct nfs4_client *clp;
	spin_lock(&state_spinlock);
	clp = __nfs4_find_client(addr);
	spin_unlock(&state_spinlock);
	return clp;
}

struct nfs4_client *
nfs4_get_client(struct in_addr *addr)
{
	struct nfs4_client *clp, *new = NULL;

	spin_lock(&state_spinlock);
	for (;;) {
		clp = __nfs4_find_client(addr);
		if (clp != NULL)
			break;
		clp = new;
		if (clp != NULL) {
			list_add(&clp->cl_servers, &nfs4_clientid_list);
			new = NULL;
			break;
		}
		spin_unlock(&state_spinlock);
		new = nfs4_alloc_client(addr);
		spin_lock(&state_spinlock);
		if (new == NULL)
			break;
	}
	spin_unlock(&state_spinlock);
	if (new)
		nfs4_free_client(new);
	return clp;
}

void
nfs4_put_client(struct nfs4_client *clp)
{
	if (!atomic_dec_and_lock(&clp->cl_count, &state_spinlock))
		return;
	list_del(&clp->cl_servers);
	spin_unlock(&state_spinlock);
	BUG_ON(!list_empty(&clp->cl_superblocks));
	wake_up_all(&clp->cl_waitq);
	rpc_wake_up(&clp->cl_rpcwaitq);
	nfs4_kill_renewd(clp);
	nfs4_free_client(clp);
}

int nfs4_init_client(struct nfs4_client *clp)
{
	int status = nfs4_proc_setclientid(clp, NFS4_CALLBACK, nfs_callback_tcpport);
	if (status == 0)
		status = nfs4_proc_setclientid_confirm(clp);
	if (status == 0)
		nfs4_schedule_state_renewal(clp);
	return status;
}

u32
nfs4_alloc_lockowner_id(struct nfs4_client *clp)
{
	return clp->cl_lockowner_id ++;
}

static struct nfs4_state_owner *
nfs4_client_grab_unused(struct nfs4_client *clp, struct rpc_cred *cred)
{
	struct nfs4_state_owner *sp = NULL;

	if (!list_empty(&clp->cl_unused)) {
		sp = list_entry(clp->cl_unused.next, struct nfs4_state_owner, so_list);
		atomic_inc(&sp->so_count);
		sp->so_cred = cred;
		list_move(&sp->so_list, &clp->cl_state_owners);
		clp->cl_nunused--;
	}
	return sp;
}

static struct nfs4_state_owner *
nfs4_find_state_owner(struct nfs4_client *clp, struct rpc_cred *cred)
{
	struct nfs4_state_owner *sp, *res = NULL;

	list_for_each_entry(sp, &clp->cl_state_owners, so_list) {
		if (sp->so_cred != cred)
			continue;
		atomic_inc(&sp->so_count);
		/* Move to the head of the list */
		list_move(&sp->so_list, &clp->cl_state_owners);
		res = sp;
		break;
	}
	return res;
}

/*
 * nfs4_alloc_state_owner(): this is called on the OPEN or CREATE path to
 * create a new state_owner.
 *
 */
static struct nfs4_state_owner *
nfs4_alloc_state_owner(void)
{
	struct nfs4_state_owner *sp;

	sp = kmalloc(sizeof(*sp),GFP_KERNEL);
	if (!sp)
		return NULL;
	init_MUTEX(&sp->so_sema);
	sp->so_seqid = 0;                 /* arbitrary */
	INIT_LIST_HEAD(&sp->so_states);
	INIT_LIST_HEAD(&sp->so_delegations);
	atomic_set(&sp->so_count, 1);
	return sp;
}

static void
nfs4_unhash_state_owner(struct nfs4_state_owner *sp)
{
	struct nfs4_client *clp = sp->so_client;
	spin_lock(&clp->cl_lock);
	list_del_init(&sp->so_list);
	spin_unlock(&clp->cl_lock);
}

/*
 * Note: must be called with clp->cl_sem held in order to prevent races
 *       with reboot recovery!
 */
struct nfs4_state_owner *nfs4_get_state_owner(struct nfs_server *server, struct rpc_cred *cred)
{
	struct nfs4_client *clp = server->nfs4_state;
	struct nfs4_state_owner *sp, *new;

	get_rpccred(cred);
	new = nfs4_alloc_state_owner();
	spin_lock(&clp->cl_lock);
	sp = nfs4_find_state_owner(clp, cred);
	if (sp == NULL)
		sp = nfs4_client_grab_unused(clp, cred);
	if (sp == NULL && new != NULL) {
		list_add(&new->so_list, &clp->cl_state_owners);
		new->so_client = clp;
		new->so_id = nfs4_alloc_lockowner_id(clp);
		new->so_cred = cred;
		sp = new;
		new = NULL;
	}
	spin_unlock(&clp->cl_lock);
	if (new)
		kfree(new);
	if (sp != NULL)
		return sp;
	put_rpccred(cred);
	return NULL;
}

/*
 * Must be called with clp->cl_sem held in order to avoid races
 * with state recovery...
 */
void nfs4_put_state_owner(struct nfs4_state_owner *sp)
{
	struct nfs4_client *clp = sp->so_client;
	struct rpc_cred *cred = sp->so_cred;

	if (!atomic_dec_and_lock(&sp->so_count, &clp->cl_lock))
		return;
	if (clp->cl_nunused >= OPENOWNER_POOL_SIZE)
		goto out_free;
	if (list_empty(&sp->so_list))
		goto out_free;
	list_move(&sp->so_list, &clp->cl_unused);
	clp->cl_nunused++;
	spin_unlock(&clp->cl_lock);
	put_rpccred(cred);
	cred = NULL;
	return;
out_free:
	list_del(&sp->so_list);
	spin_unlock(&clp->cl_lock);
	put_rpccred(cred);
	kfree(sp);
}

static struct nfs4_state *
nfs4_alloc_open_state(void)
{
	struct nfs4_state *state;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;
	state->state = 0;
	state->nreaders = 0;
	state->nwriters = 0;
	state->flags = 0;
	memset(state->stateid.data, 0, sizeof(state->stateid.data));
	atomic_set(&state->count, 1);
	INIT_LIST_HEAD(&state->lock_states);
	init_MUTEX(&state->lock_sema);
	rwlock_init(&state->state_lock);
	return state;
}

static struct nfs4_state *
__nfs4_find_state(struct inode *inode, struct rpc_cred *cred, mode_t mode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs4_state *state;

	mode &= (FMODE_READ|FMODE_WRITE);
	list_for_each_entry(state, &nfsi->open_states, inode_states) {
		if (state->owner->so_cred != cred)
			continue;
		if ((mode & FMODE_READ) != 0 && state->nreaders == 0)
			continue;
		if ((mode & FMODE_WRITE) != 0 && state->nwriters == 0)
			continue;
		if ((state->state & mode) != mode)
			continue;
		atomic_inc(&state->count);
		if (mode & FMODE_READ)
			state->nreaders++;
		if (mode & FMODE_WRITE)
			state->nwriters++;
		return state;
	}
	return NULL;
}

static struct nfs4_state *
__nfs4_find_state_byowner(struct inode *inode, struct nfs4_state_owner *owner)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs4_state *state;

	list_for_each_entry(state, &nfsi->open_states, inode_states) {
		/* Is this in the process of being freed? */
		if (state->nreaders == 0 && state->nwriters == 0)
			continue;
		if (state->owner == owner) {
			atomic_inc(&state->count);
			return state;
		}
	}
	return NULL;
}

struct nfs4_state *
nfs4_find_state(struct inode *inode, struct rpc_cred *cred, mode_t mode)
{
	struct nfs4_state *state;

	spin_lock(&inode->i_lock);
	state = __nfs4_find_state(inode, cred, mode);
	spin_unlock(&inode->i_lock);
	return state;
}

static void
nfs4_free_open_state(struct nfs4_state *state)
{
	kfree(state);
}

struct nfs4_state *
nfs4_get_open_state(struct inode *inode, struct nfs4_state_owner *owner)
{
	struct nfs4_state *state, *new;
	struct nfs_inode *nfsi = NFS_I(inode);

	spin_lock(&inode->i_lock);
	state = __nfs4_find_state_byowner(inode, owner);
	spin_unlock(&inode->i_lock);
	if (state)
		goto out;
	new = nfs4_alloc_open_state();
	spin_lock(&inode->i_lock);
	state = __nfs4_find_state_byowner(inode, owner);
	if (state == NULL && new != NULL) {
		state = new;
		/* Caller *must* be holding owner->so_sem */
		list_add(&state->open_states, &owner->so_states);
		state->owner = owner;
		atomic_inc(&owner->so_count);
		list_add(&state->inode_states, &nfsi->open_states);
		state->inode = inode;
		spin_unlock(&inode->i_lock);
	} else {
		spin_unlock(&inode->i_lock);
		if (new)
			nfs4_free_open_state(new);
	}
out:
	return state;
}

/*
 * Beware! Caller must be holding exactly one
 * reference to clp->cl_sem and owner->so_sema!
 */
void nfs4_put_open_state(struct nfs4_state *state)
{
	struct inode *inode = state->inode;
	struct nfs4_state_owner *owner = state->owner;

	if (!atomic_dec_and_lock(&state->count, &inode->i_lock))
		return;
	if (!list_empty(&state->inode_states))
		list_del(&state->inode_states);
	spin_unlock(&inode->i_lock);
	list_del(&state->open_states);
	BUG_ON (state->state != 0);
	nfs4_free_open_state(state);
	nfs4_put_state_owner(owner);
}

/*
 * Beware! Caller must be holding no references to clp->cl_sem!
 * of owner->so_sema!
 */
void nfs4_close_state(struct nfs4_state *state, mode_t mode)
{
	struct inode *inode = state->inode;
	struct nfs4_state_owner *owner = state->owner;
	struct nfs4_client *clp = owner->so_client;
	int newstate;
	int status = 0;

	atomic_inc(&owner->so_count);
	down_read(&clp->cl_sem);
	down(&owner->so_sema);
	/* Protect against nfs4_find_state() */
	spin_lock(&inode->i_lock);
	if (mode & FMODE_READ)
		state->nreaders--;
	if (mode & FMODE_WRITE)
		state->nwriters--;
	if (state->nwriters == 0 && state->nreaders == 0)
		list_del_init(&state->inode_states);
	spin_unlock(&inode->i_lock);
	newstate = 0;
	if (state->state != 0) {
		if (state->nreaders)
			newstate |= FMODE_READ;
		if (state->nwriters)
			newstate |= FMODE_WRITE;
		if (state->state == newstate)
			goto out;
		if (newstate != 0)
			status = nfs4_do_downgrade(inode, state, newstate);
		else
			status = nfs4_do_close(inode, state);
	}
out:
	nfs4_put_open_state(state);
	up(&owner->so_sema);
	nfs4_put_state_owner(owner);
	up_read(&clp->cl_sem);
}

/*
 * Search the state->lock_states for an existing lock_owner
 * that is compatible with current->files
 */
static struct nfs4_lock_state *
__nfs4_find_lock_state(struct nfs4_state *state, fl_owner_t fl_owner)
{
	struct nfs4_lock_state *pos;
	list_for_each_entry(pos, &state->lock_states, ls_locks) {
		if (pos->ls_owner != fl_owner)
			continue;
		atomic_inc(&pos->ls_count);
		return pos;
	}
	return NULL;
}

struct nfs4_lock_state *
nfs4_find_lock_state(struct nfs4_state *state, fl_owner_t fl_owner)
{
	struct nfs4_lock_state *lsp;
	read_lock(&state->state_lock);
	lsp = __nfs4_find_lock_state(state, fl_owner);
	read_unlock(&state->state_lock);
	return lsp;
}

/*
 * Return a compatible lock_state. If no initialized lock_state structure
 * exists, return an uninitialized one.
 *
 * The caller must be holding state->lock_sema
 */
static struct nfs4_lock_state *nfs4_alloc_lock_state(struct nfs4_state *state, fl_owner_t fl_owner)
{
	struct nfs4_lock_state *lsp;
	struct nfs4_client *clp = state->owner->so_client;

	lsp = kmalloc(sizeof(*lsp), GFP_KERNEL);
	if (lsp == NULL)
		return NULL;
	lsp->ls_flags = 0;
	lsp->ls_seqid = 0;	/* arbitrary */
	lsp->ls_id = -1; 
	memset(lsp->ls_stateid.data, 0, sizeof(lsp->ls_stateid.data));
	atomic_set(&lsp->ls_count, 1);
	lsp->ls_owner = fl_owner;
	INIT_LIST_HEAD(&lsp->ls_locks);
	spin_lock(&clp->cl_lock);
	lsp->ls_id = nfs4_alloc_lockowner_id(clp);
	spin_unlock(&clp->cl_lock);
	return lsp;
}

/*
 * Return a compatible lock_state. If no initialized lock_state structure
 * exists, return an uninitialized one.
 *
 * The caller must be holding state->lock_sema and clp->cl_sem
 */
struct nfs4_lock_state *nfs4_get_lock_state(struct nfs4_state *state, fl_owner_t owner)
{
	struct nfs4_lock_state * lsp;
	
	lsp = nfs4_find_lock_state(state, owner);
	if (lsp == NULL)
		lsp = nfs4_alloc_lock_state(state, owner);
	return lsp;
}

/*
 * Byte-range lock aware utility to initialize the stateid of read/write
 * requests.
 */
void
nfs4_copy_stateid(nfs4_stateid *dst, struct nfs4_state *state, fl_owner_t fl_owner)
{
	if (test_bit(LK_STATE_IN_USE, &state->flags)) {
		struct nfs4_lock_state *lsp;

		lsp = nfs4_find_lock_state(state, fl_owner);
		if (lsp) {
			memcpy(dst, &lsp->ls_stateid, sizeof(*dst));
			nfs4_put_lock_state(lsp);
			return;
		}
	}
	memcpy(dst, &state->stateid, sizeof(*dst));
}

/*
* Called with state->lock_sema and clp->cl_sem held.
*/
void nfs4_increment_lock_seqid(int status, struct nfs4_lock_state *lsp)
{
	if (status == NFS_OK || seqid_mutating_err(-status))
		lsp->ls_seqid++;
}

/* 
* Check to see if the request lock (type FL_UNLK) effects the fl lock.
*
* fl and request must have the same posix owner
*
* return: 
* 0 -> fl not effected by request
* 1 -> fl consumed by request
*/

static int
nfs4_check_unlock(struct file_lock *fl, struct file_lock *request)
{
	if (fl->fl_start >= request->fl_start && fl->fl_end <= request->fl_end)
		return 1;
	return 0;
}

/*
 * Post an initialized lock_state on the state->lock_states list.
 */
void nfs4_notify_setlk(struct nfs4_state *state, struct file_lock *request, struct nfs4_lock_state *lsp)
{
	if (!list_empty(&lsp->ls_locks))
		return;
	atomic_inc(&lsp->ls_count);
	write_lock(&state->state_lock);
	list_add(&lsp->ls_locks, &state->lock_states);
	set_bit(LK_STATE_IN_USE, &state->flags);
	write_unlock(&state->state_lock);
}

/* 
 * to decide to 'reap' lock state:
 * 1) search i_flock for file_locks with fl.lock_state = to ls.
 * 2) determine if unlock will consume found lock. 
 * 	if so, reap
 *
 * 	else, don't reap.
 *
 */
void
nfs4_notify_unlck(struct nfs4_state *state, struct file_lock *request, struct nfs4_lock_state *lsp)
{
	struct inode *inode = state->inode;
	struct file_lock *fl;

	for (fl = inode->i_flock; fl != NULL; fl = fl->fl_next) {
		if (!(fl->fl_flags & FL_POSIX))
			continue;
		if (fl->fl_owner != lsp->ls_owner)
			continue;
		/* Exit if we find at least one lock which is not consumed */
		if (nfs4_check_unlock(fl,request) == 0)
			return;
	}

	write_lock(&state->state_lock);
	list_del_init(&lsp->ls_locks);
	if (list_empty(&state->lock_states))
		clear_bit(LK_STATE_IN_USE, &state->flags);
	write_unlock(&state->state_lock);
	nfs4_put_lock_state(lsp);
}

/*
 * Release reference to lock_state, and free it if we see that
 * it is no longer in use
 */
void
nfs4_put_lock_state(struct nfs4_lock_state *lsp)
{
	if (!atomic_dec_and_test(&lsp->ls_count))
		return;
	BUG_ON (!list_empty(&lsp->ls_locks));
	kfree(lsp);
}

/*
* Called with sp->so_sema and clp->cl_sem held.
*
* Increment the seqid if the OPEN/OPEN_DOWNGRADE/CLOSE succeeded, or
* failed with a seqid incrementing error -
* see comments nfs_fs.h:seqid_mutating_error()
*/
void nfs4_increment_seqid(int status, struct nfs4_state_owner *sp)
{
	if (status == NFS_OK || seqid_mutating_err(-status))
		sp->so_seqid++;
	/* If the server returns BAD_SEQID, unhash state_owner here */
	if (status == -NFS4ERR_BAD_SEQID)
		nfs4_unhash_state_owner(sp);
}

static int reclaimer(void *);
struct reclaimer_args {
	struct nfs4_client *clp;
	struct completion complete;
};

/*
 * State recovery routine
 */
void
nfs4_recover_state(void *data)
{
	struct nfs4_client *clp = (struct nfs4_client *)data;
	struct reclaimer_args args = {
		.clp = clp,
	};
	might_sleep();

	init_completion(&args.complete);

	if (kernel_thread(reclaimer, &args, CLONE_KERNEL) < 0)
		goto out_failed_clear;
	wait_for_completion(&args.complete);
	return;
out_failed_clear:
	set_bit(NFS4CLNT_OK, &clp->cl_state);
	wake_up_all(&clp->cl_waitq);
	rpc_wake_up(&clp->cl_rpcwaitq);
}

/*
 * Schedule a state recovery attempt
 */
void
nfs4_schedule_state_recovery(struct nfs4_client *clp)
{
	if (!clp)
		return;
	if (test_and_clear_bit(NFS4CLNT_OK, &clp->cl_state))
		schedule_work(&clp->cl_recoverd);
}

static int nfs4_reclaim_locks(struct nfs4_state *state)
{
	struct inode *inode = state->inode;
	struct file_lock *fl;
	int status = 0;

	for (fl = inode->i_flock; fl != 0; fl = fl->fl_next) {
		if (!(fl->fl_flags & FL_POSIX))
			continue;
		if (((struct nfs_open_context *)fl->fl_file->private_data)->state != state)
			continue;
		status = nfs4_lock_reclaim(state, fl);
		if (status >= 0)
			continue;
		switch (status) {
			default:
				printk(KERN_ERR "%s: unhandled error %d. Zeroing state\n",
						__FUNCTION__, status);
			case -NFS4ERR_EXPIRED:
			case -NFS4ERR_NO_GRACE:
			case -NFS4ERR_RECLAIM_BAD:
			case -NFS4ERR_RECLAIM_CONFLICT:
				/* kill_proc(fl->fl_owner, SIGLOST, 1); */
				break;
			case -NFS4ERR_STALE_CLIENTID:
				goto out_err;
		}
	}
	return 0;
out_err:
	return status;
}

static int nfs4_reclaim_open_state(struct nfs4_state_owner *sp)
{
	struct nfs4_state *state;
	struct nfs4_lock_state *lock;
	int status = 0;

	list_for_each_entry(state, &sp->so_states, open_states) {
		if (state->state == 0)
			continue;
		status = nfs4_open_reclaim(sp, state);
		list_for_each_entry(lock, &state->lock_states, ls_locks)
			lock->ls_flags &= ~NFS_LOCK_INITIALIZED;
		if (status >= 0) {
			status = nfs4_reclaim_locks(state);
			if (status < 0)
				goto out_err;
			list_for_each_entry(lock, &state->lock_states, ls_locks) {
				if (!(lock->ls_flags & NFS_LOCK_INITIALIZED))
					printk("%s: Lock reclaim failed!\n",
							__FUNCTION__);
			}
			continue;
		}
		switch (status) {
			default:
				printk(KERN_ERR "%s: unhandled error %d. Zeroing state\n",
						__FUNCTION__, status);
			case -NFS4ERR_EXPIRED:
			case -NFS4ERR_NO_GRACE:
			case -NFS4ERR_RECLAIM_BAD:
			case -NFS4ERR_RECLAIM_CONFLICT:
				/*
				 * Open state on this file cannot be recovered
				 * All we can do is revert to using the zero stateid.
				 */
				memset(state->stateid.data, 0,
					sizeof(state->stateid.data));
				/* Mark the file as being 'closed' */
				state->state = 0;
				break;
			case -NFS4ERR_STALE_CLIENTID:
				goto out_err;
		}
	}
	return 0;
out_err:
	return status;
}

static int reclaimer(void *ptr)
{
	struct reclaimer_args *args = (struct reclaimer_args *)ptr;
	struct nfs4_client *clp = args->clp;
	struct nfs4_state_owner *sp;
	int status = 0;

	daemonize("%u.%u.%u.%u-reclaim", NIPQUAD(clp->cl_addr));
	allow_signal(SIGKILL);

	atomic_inc(&clp->cl_count);
	complete(&args->complete);

	/* Ensure exclusive access to NFSv4 state */
	lock_kernel();
	down_write(&clp->cl_sem);
	/* Are there any NFS mounts out there? */
	if (list_empty(&clp->cl_superblocks))
		goto out;
restart_loop:
	status = nfs4_proc_renew(clp);
	if (status == 0 || status == -NFS4ERR_CB_PATH_DOWN)
		goto out;
	status = nfs4_init_client(clp);
	if (status)
		goto out_error;
	/* Mark all delagations for reclaim */
	nfs_delegation_mark_reclaim(clp);
	/* Note: list is protected by exclusive lock on cl->cl_sem */
	list_for_each_entry(sp, &clp->cl_state_owners, so_list) {
		status = nfs4_reclaim_open_state(sp);
		if (status < 0) {
			if (status == -NFS4ERR_STALE_CLIENTID)
				goto restart_loop;
			goto out_error;
		}
	}
	nfs_delegation_reap_unclaimed(clp);
out:
	set_bit(NFS4CLNT_OK, &clp->cl_state);
	up_write(&clp->cl_sem);
	unlock_kernel();
	wake_up_all(&clp->cl_waitq);
	rpc_wake_up(&clp->cl_rpcwaitq);
	if (status == -NFS4ERR_CB_PATH_DOWN)
		nfs_handle_cb_pathdown(clp);
	nfs4_put_client(clp);
	return 0;
out_error:
	printk(KERN_WARNING "Error: state recovery failed on NFSv4 server %u.%u.%u.%u with error %d\n",
				NIPQUAD(clp->cl_addr.s_addr), -status);
	goto out;
}

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */

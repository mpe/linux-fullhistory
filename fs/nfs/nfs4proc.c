/*
 *  fs/nfs/nfs4proc.c
 *
 *  Client-side procedure declarations for NFSv4.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *  Andy Adamson   <andros@umich.edu>
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
 */

#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/smp_lock.h>
#include <linux/namei.h>

#include "delegation.h"

#define NFSDBG_FACILITY		NFSDBG_PROC

#define NFS4_POLL_RETRY_MIN	(1*HZ)
#define NFS4_POLL_RETRY_MAX	(15*HZ)

static int nfs4_do_fsinfo(struct nfs_server *, struct nfs_fh *, struct nfs_fsinfo *);
static int nfs4_async_handle_error(struct rpc_task *, struct nfs_server *);
static int _nfs4_proc_access(struct inode *inode, struct nfs_access_entry *entry);
static int nfs4_handle_exception(struct nfs_server *server, int errorcode, struct nfs4_exception *exception);
extern u32 *nfs4_decode_dirent(u32 *p, struct nfs_entry *entry, int plus);
extern struct rpc_procinfo nfs4_procedures[];

extern nfs4_stateid zero_stateid;

/* Prevent leaks of NFSv4 errors into userland */
int nfs4_map_errors(int err)
{
	if (err < -1000) {
		dprintk("%s could not handle NFSv4 error %d\n",
				__FUNCTION__, -err);
		return -EIO;
	}
	return err;
}

/*
 * This is our standard bitmap for GETATTR requests.
 */
const u32 nfs4_fattr_bitmap[2] = {
	FATTR4_WORD0_TYPE
	| FATTR4_WORD0_CHANGE
	| FATTR4_WORD0_SIZE
	| FATTR4_WORD0_FSID
	| FATTR4_WORD0_FILEID,
	FATTR4_WORD1_MODE
	| FATTR4_WORD1_NUMLINKS
	| FATTR4_WORD1_OWNER
	| FATTR4_WORD1_OWNER_GROUP
	| FATTR4_WORD1_RAWDEV
	| FATTR4_WORD1_SPACE_USED
	| FATTR4_WORD1_TIME_ACCESS
	| FATTR4_WORD1_TIME_METADATA
	| FATTR4_WORD1_TIME_MODIFY
};

const u32 nfs4_statfs_bitmap[2] = {
	FATTR4_WORD0_FILES_AVAIL
	| FATTR4_WORD0_FILES_FREE
	| FATTR4_WORD0_FILES_TOTAL,
	FATTR4_WORD1_SPACE_AVAIL
	| FATTR4_WORD1_SPACE_FREE
	| FATTR4_WORD1_SPACE_TOTAL
};

u32 nfs4_pathconf_bitmap[2] = {
	FATTR4_WORD0_MAXLINK
	| FATTR4_WORD0_MAXNAME,
	0
};

const u32 nfs4_fsinfo_bitmap[2] = { FATTR4_WORD0_MAXFILESIZE
			| FATTR4_WORD0_MAXREAD
			| FATTR4_WORD0_MAXWRITE
			| FATTR4_WORD0_LEASE_TIME,
			0
};

static void nfs4_setup_readdir(u64 cookie, u32 *verifier, struct dentry *dentry,
		struct nfs4_readdir_arg *readdir)
{
	u32 *start, *p;

	BUG_ON(readdir->count < 80);
	if (cookie > 2) {
		readdir->cookie = (cookie > 2) ? cookie : 0;
		memcpy(&readdir->verifier, verifier, sizeof(readdir->verifier));
		return;
	}

	readdir->cookie = 0;
	memset(&readdir->verifier, 0, sizeof(readdir->verifier));
	if (cookie == 2)
		return;
	
	/*
	 * NFSv4 servers do not return entries for '.' and '..'
	 * Therefore, we fake these entries here.  We let '.'
	 * have cookie 0 and '..' have cookie 1.  Note that
	 * when talking to the server, we always send cookie 0
	 * instead of 1 or 2.
	 */
	start = p = (u32 *)kmap_atomic(*readdir->pages, KM_USER0);
	
	if (cookie == 0) {
		*p++ = xdr_one;                                  /* next */
		*p++ = xdr_zero;                   /* cookie, first word */
		*p++ = xdr_one;                   /* cookie, second word */
		*p++ = xdr_one;                             /* entry len */
		memcpy(p, ".\0\0\0", 4);                        /* entry */
		p++;
		*p++ = xdr_one;                         /* bitmap length */
		*p++ = htonl(FATTR4_WORD0_FILEID);             /* bitmap */
		*p++ = htonl(8);              /* attribute buffer length */
		p = xdr_encode_hyper(p, dentry->d_inode->i_ino);
	}
	
	*p++ = xdr_one;                                  /* next */
	*p++ = xdr_zero;                   /* cookie, first word */
	*p++ = xdr_two;                   /* cookie, second word */
	*p++ = xdr_two;                             /* entry len */
	memcpy(p, "..\0\0", 4);                         /* entry */
	p++;
	*p++ = xdr_one;                         /* bitmap length */
	*p++ = htonl(FATTR4_WORD0_FILEID);             /* bitmap */
	*p++ = htonl(8);              /* attribute buffer length */
	p = xdr_encode_hyper(p, dentry->d_parent->d_inode->i_ino);

	readdir->pgbase = (char *)p - (char *)start;
	readdir->count -= readdir->pgbase;
	kunmap_atomic(start, KM_USER0);
}

static void
renew_lease(struct nfs_server *server, unsigned long timestamp)
{
	struct nfs4_client *clp = server->nfs4_state;
	spin_lock(&clp->cl_lock);
	if (time_before(clp->cl_last_renewal,timestamp))
		clp->cl_last_renewal = timestamp;
	spin_unlock(&clp->cl_lock);
}

static void update_changeattr(struct inode *inode, struct nfs4_change_info *cinfo)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	if (cinfo->before == nfsi->change_attr && cinfo->atomic)
		nfsi->change_attr = cinfo->after;
}

static void update_open_stateid(struct nfs4_state *state, nfs4_stateid *stateid, int open_flags)
{
	struct inode *inode = state->inode;

	open_flags &= (FMODE_READ|FMODE_WRITE);
	/* Protect against nfs4_find_state() */
	spin_lock(&inode->i_lock);
	state->state |= open_flags;
	/* NB! List reordering - see the reclaim code for why.  */
	if ((open_flags & FMODE_WRITE) && 0 == state->nwriters++)
		list_move(&state->open_states, &state->owner->so_states);
	if (open_flags & FMODE_READ)
		state->nreaders++;
	memcpy(&state->stateid, stateid, sizeof(state->stateid));
	spin_unlock(&inode->i_lock);
}

/*
 * OPEN_RECLAIM:
 * 	reclaim state on the server after a reboot.
 * 	Assumes caller is holding the sp->so_sem
 */
static int _nfs4_open_reclaim(struct nfs4_state_owner *sp, struct nfs4_state *state)
{
	struct inode *inode = state->inode;
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_delegation *delegation = NFS_I(inode)->delegation;
	struct nfs_openargs o_arg = {
		.fh = NFS_FH(inode),
		.seqid = sp->so_seqid,
		.id = sp->so_id,
		.open_flags = state->state,
		.clientid = server->nfs4_state->cl_clientid,
		.claim = NFS4_OPEN_CLAIM_PREVIOUS,
		.bitmask = server->attr_bitmask,
	};
	struct nfs_openres o_res = {
		.server = server,	/* Grrr */
	};
	struct rpc_message msg = {
		.rpc_proc       = &nfs4_procedures[NFSPROC4_CLNT_OPEN_NOATTR],
		.rpc_argp       = &o_arg,
		.rpc_resp	= &o_res,
		.rpc_cred	= sp->so_cred,
	};
	int status;

	if (delegation != NULL) {
		if (!(delegation->flags & NFS_DELEGATION_NEED_RECLAIM)) {
			memcpy(&state->stateid, &delegation->stateid,
					sizeof(state->stateid));
			set_bit(NFS_DELEGATED_STATE, &state->flags);
			return 0;
		}
		o_arg.u.delegation_type = delegation->type;
	}
	status = rpc_call_sync(server->client, &msg, RPC_TASK_NOINTR);
	nfs4_increment_seqid(status, sp);
	if (status == 0) {
		memcpy(&state->stateid, &o_res.stateid, sizeof(state->stateid));
		if (o_res.delegation_type != 0) {
			nfs_inode_reclaim_delegation(inode, sp->so_cred, &o_res);
			/* Did the server issue an immediate delegation recall? */
			if (o_res.do_recall)
				nfs_async_inode_return_delegation(inode, &o_res.stateid);
		}
	}
	clear_bit(NFS_DELEGATED_STATE, &state->flags);
	/* Ensure we update the inode attributes */
	NFS_CACHEINV(inode);
	return status;
}

static int nfs4_open_reclaim(struct nfs4_state_owner *sp, struct nfs4_state *state)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	struct nfs4_exception exception = { };
	int err;
	do {
		err = _nfs4_open_reclaim(sp, state);
		switch (err) {
			case 0:
			case -NFS4ERR_STALE_CLIENTID:
			case -NFS4ERR_STALE_STATEID:
			case -NFS4ERR_EXPIRED:
				return err;
		}
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_open_delegation_recall(struct dentry *dentry, struct nfs4_state *state)
{
	struct nfs4_state_owner  *sp  = state->owner;
	struct inode *inode = dentry->d_inode;
	struct nfs_server *server = NFS_SERVER(inode);
	struct dentry *parent = dget_parent(dentry);
	struct nfs_openargs arg = {
		.fh = NFS_FH(parent->d_inode),
		.clientid = server->nfs4_state->cl_clientid,
		.name = &dentry->d_name,
		.id = sp->so_id,
		.server = server,
		.bitmask = server->attr_bitmask,
		.claim = NFS4_OPEN_CLAIM_DELEGATE_CUR,
	};
	struct nfs_openres res = {
		.server = server,
	};
	struct 	rpc_message msg = {
		.rpc_proc       = &nfs4_procedures[NFSPROC4_CLNT_OPEN_NOATTR],
		.rpc_argp       = &arg,
		.rpc_resp       = &res,
		.rpc_cred	= sp->so_cred,
	};
	int status = 0;

	down(&sp->so_sema);
	if (!test_bit(NFS_DELEGATED_STATE, &state->flags))
		goto out;
	if (state->state == 0)
		goto out;
	arg.seqid = sp->so_seqid;
	arg.open_flags = state->state;
	memcpy(arg.u.delegation.data, state->stateid.data, sizeof(arg.u.delegation.data));
	status = rpc_call_sync(server->client, &msg, RPC_TASK_NOINTR);
	nfs4_increment_seqid(status, sp);
	if (status >= 0) {
		memcpy(state->stateid.data, res.stateid.data,
				sizeof(state->stateid.data));
		clear_bit(NFS_DELEGATED_STATE, &state->flags);
	}
out:
	up(&sp->so_sema);
	dput(parent);
	return status;
}

int nfs4_open_delegation_recall(struct dentry *dentry, struct nfs4_state *state)
{
	struct nfs4_exception exception = { };
	struct nfs_server *server = NFS_SERVER(dentry->d_inode);
	int err;
	do {
		err = _nfs4_open_delegation_recall(dentry, state);
		switch (err) {
			case 0:
				return err;
			case -NFS4ERR_STALE_CLIENTID:
			case -NFS4ERR_STALE_STATEID:
			case -NFS4ERR_EXPIRED:
				/* Don't recall a delegation if it was lost */
				nfs4_schedule_state_recovery(server->nfs4_state);
				return err;
		}
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
	return err;
}

static inline int _nfs4_proc_open_confirm(struct rpc_clnt *clnt, const struct nfs_fh *fh, struct nfs4_state_owner *sp, nfs4_stateid *stateid)
{
	struct nfs_open_confirmargs arg = {
		.fh             = fh,
		.seqid          = sp->so_seqid,
		.stateid	= *stateid,
	};
	struct nfs_open_confirmres res;
	struct 	rpc_message msg = {
		.rpc_proc       = &nfs4_procedures[NFSPROC4_CLNT_OPEN_CONFIRM],
		.rpc_argp       = &arg,
		.rpc_resp       = &res,
		.rpc_cred	= sp->so_cred,
	};
	int status;

	status = rpc_call_sync(clnt, &msg, RPC_TASK_NOINTR);
	nfs4_increment_seqid(status, sp);
	if (status >= 0)
		memcpy(stateid, &res.stateid, sizeof(*stateid));
	return status;
}

static int _nfs4_proc_open(struct inode *dir, struct nfs4_state_owner  *sp, struct nfs_openargs *o_arg, struct nfs_openres *o_res)
{
	struct nfs_server *server = NFS_SERVER(dir);
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_OPEN],
		.rpc_argp = o_arg,
		.rpc_resp = o_res,
		.rpc_cred = sp->so_cred,
	};
	int status;

	/* Update sequence id. The caller must serialize! */
	o_arg->seqid = sp->so_seqid;
	o_arg->id = sp->so_id;
	o_arg->clientid = sp->so_client->cl_clientid;

	status = rpc_call_sync(server->client, &msg, RPC_TASK_NOINTR);
	nfs4_increment_seqid(status, sp);
	if (status != 0)
		goto out;
	update_changeattr(dir, &o_res->cinfo);
	if(o_res->rflags & NFS4_OPEN_RESULT_CONFIRM) {
		status = _nfs4_proc_open_confirm(server->client, &o_res->fh,
				sp, &o_res->stateid);
		if (status != 0)
			goto out;
	}
	if (!(o_res->f_attr->valid & NFS_ATTR_FATTR))
		status = server->rpc_ops->getattr(server, &o_res->fh, o_res->f_attr);
out:
	return status;
}

static int _nfs4_do_access(struct inode *inode, struct rpc_cred *cred, int openflags)
{
	struct nfs_access_entry cache;
	int mask = 0;
	int status;

	if (openflags & FMODE_READ)
		mask |= MAY_READ;
	if (openflags & FMODE_WRITE)
		mask |= MAY_WRITE;
	status = nfs_access_get_cached(inode, cred, &cache);
	if (status == 0)
		goto out;

	/* Be clever: ask server to check for all possible rights */
	cache.mask = MAY_EXEC | MAY_WRITE | MAY_READ;
	cache.cred = cred;
	cache.jiffies = jiffies;
	status = _nfs4_proc_access(inode, &cache);
	if (status != 0)
		return status;
	nfs_access_add_cache(inode, &cache);
out:
	if ((cache.mask & mask) == mask)
		return 0;
	return -EACCES;
}

/*
 * OPEN_EXPIRED:
 * 	reclaim state on the server after a network partition.
 * 	Assumes caller holds the appropriate lock
 */
static int _nfs4_open_expired(struct nfs4_state_owner *sp, struct nfs4_state *state, struct dentry *dentry)
{
	struct dentry *parent = dget_parent(dentry);
	struct inode *dir = parent->d_inode;
	struct inode *inode = state->inode;
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_delegation *delegation = NFS_I(inode)->delegation;
	struct nfs_fattr        f_attr = {
		.valid = 0,
	};
	struct nfs_openargs o_arg = {
		.fh = NFS_FH(dir),
		.open_flags = state->state,
		.name = &dentry->d_name,
		.bitmask = server->attr_bitmask,
		.claim = NFS4_OPEN_CLAIM_NULL,
	};
	struct nfs_openres o_res = {
		.f_attr = &f_attr,
		.server = server,
	};
	int status = 0;

	if (delegation != NULL && !(delegation->flags & NFS_DELEGATION_NEED_RECLAIM)) {
		status = _nfs4_do_access(inode, sp->so_cred, state->state);
		if (status < 0)
			goto out;
		memcpy(&state->stateid, &delegation->stateid, sizeof(state->stateid));
		set_bit(NFS_DELEGATED_STATE, &state->flags);
		goto out;
	}
	status = _nfs4_proc_open(dir, sp, &o_arg, &o_res);
	if (status != 0)
		goto out_nodeleg;
	/* Check if files differ */
	if ((f_attr.mode & S_IFMT) != (inode->i_mode & S_IFMT))
		goto out_stale;
	/* Has the file handle changed? */
	if (nfs_compare_fh(&o_res.fh, NFS_FH(inode)) != 0) {
		/* Verify if the change attributes are the same */
		if (f_attr.change_attr != NFS_I(inode)->change_attr)
			goto out_stale;
		if (nfs_size_to_loff_t(f_attr.size) != inode->i_size)
			goto out_stale;
		/* Lets just pretend that this is the same file */
		nfs_copy_fh(NFS_FH(inode), &o_res.fh);
		NFS_I(inode)->fileid = f_attr.fileid;
	}
	memcpy(&state->stateid, &o_res.stateid, sizeof(state->stateid));
	if (o_res.delegation_type != 0) {
		if (!(delegation->flags & NFS_DELEGATION_NEED_RECLAIM))
			nfs_inode_set_delegation(inode, sp->so_cred, &o_res);
		else
			nfs_inode_reclaim_delegation(inode, sp->so_cred, &o_res);
	}
out_nodeleg:
	clear_bit(NFS_DELEGATED_STATE, &state->flags);
out:
	dput(parent);
	return status;
out_stale:
	status = -ESTALE;
	/* Invalidate the state owner so we don't ever use it again */
	nfs4_drop_state_owner(sp);
	d_drop(dentry);
	/* Should we be trying to close that stateid? */
	goto out_nodeleg;
}

static int nfs4_open_expired(struct nfs4_state_owner *sp, struct nfs4_state *state)
{
	struct nfs_inode *nfsi = NFS_I(state->inode);
	struct nfs_open_context *ctx;
	int status;

	spin_lock(&state->inode->i_lock);
	list_for_each_entry(ctx, &nfsi->open_files, list) {
		if (ctx->state != state)
			continue;
		get_nfs_open_context(ctx);
		spin_unlock(&state->inode->i_lock);
		status = _nfs4_open_expired(sp, state, ctx->dentry);
		put_nfs_open_context(ctx);
		return status;
	}
	spin_unlock(&state->inode->i_lock);
	return -ENOENT;
}

/*
 * Returns an nfs4_state + an extra reference to the inode
 */
static int _nfs4_open_delegated(struct inode *inode, int flags, struct rpc_cred *cred, struct nfs4_state **res)
{
	struct nfs_delegation *delegation;
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_client *clp = server->nfs4_state;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs4_state_owner *sp = NULL;
	struct nfs4_state *state = NULL;
	int open_flags = flags & (FMODE_READ|FMODE_WRITE);
	int err;

	/* Protect against reboot recovery - NOTE ORDER! */
	down_read(&clp->cl_sem);
	/* Protect against delegation recall */
	down_read(&nfsi->rwsem);
	delegation = NFS_I(inode)->delegation;
	err = -ENOENT;
	if (delegation == NULL || (delegation->type & open_flags) != open_flags)
		goto out_err;
	err = -ENOMEM;
	if (!(sp = nfs4_get_state_owner(server, cred))) {
		dprintk("%s: nfs4_get_state_owner failed!\n", __FUNCTION__);
		goto out_err;
	}
	down(&sp->so_sema);
	state = nfs4_get_open_state(inode, sp);
	if (state == NULL)
		goto out_err;

	err = -ENOENT;
	if ((state->state & open_flags) == open_flags) {
		spin_lock(&inode->i_lock);
		if (open_flags & FMODE_READ)
			state->nreaders++;
		if (open_flags & FMODE_WRITE)
			state->nwriters++;
		spin_unlock(&inode->i_lock);
		goto out_ok;
	} else if (state->state != 0)
		goto out_err;

	lock_kernel();
	err = _nfs4_do_access(inode, cred, open_flags);
	unlock_kernel();
	if (err != 0)
		goto out_err;
	set_bit(NFS_DELEGATED_STATE, &state->flags);
	update_open_stateid(state, &delegation->stateid, open_flags);
out_ok:
	up(&sp->so_sema);
	nfs4_put_state_owner(sp);
	up_read(&nfsi->rwsem);
	up_read(&clp->cl_sem);
	igrab(inode);
	*res = state;
	return 0; 
out_err:
	if (sp != NULL) {
		if (state != NULL)
			nfs4_put_open_state(state);
		up(&sp->so_sema);
		nfs4_put_state_owner(sp);
	}
	up_read(&nfsi->rwsem);
	up_read(&clp->cl_sem);
	return err;
}

static struct nfs4_state *nfs4_open_delegated(struct inode *inode, int flags, struct rpc_cred *cred)
{
	struct nfs4_exception exception = { };
	struct nfs4_state *res;
	int err;

	do {
		err = _nfs4_open_delegated(inode, flags, cred, &res);
		if (err == 0)
			break;
		res = ERR_PTR(nfs4_handle_exception(NFS_SERVER(inode),
					err, &exception));
	} while (exception.retry);
	return res;
}

/*
 * Returns an nfs4_state + an referenced inode
 */
static int _nfs4_do_open(struct inode *dir, struct dentry *dentry, int flags, struct iattr *sattr, struct rpc_cred *cred, struct nfs4_state **res)
{
	struct nfs4_state_owner  *sp;
	struct nfs4_state     *state = NULL;
	struct nfs_server       *server = NFS_SERVER(dir);
	struct nfs4_client *clp = server->nfs4_state;
	struct inode *inode = NULL;
	int                     status;
	struct nfs_fattr        f_attr = {
		.valid          = 0,
	};
	struct nfs_openargs o_arg = {
		.fh             = NFS_FH(dir),
		.open_flags	= flags,
		.name           = &dentry->d_name,
		.server         = server,
		.bitmask = server->attr_bitmask,
		.claim = NFS4_OPEN_CLAIM_NULL,
	};
	struct nfs_openres o_res = {
		.f_attr         = &f_attr,
		.server         = server,
	};

	/* Protect against reboot recovery conflicts */
	down_read(&clp->cl_sem);
	status = -ENOMEM;
	if (!(sp = nfs4_get_state_owner(server, cred))) {
		dprintk("nfs4_do_open: nfs4_get_state_owner failed!\n");
		goto out_err;
	}
	if (flags & O_EXCL) {
		u32 *p = (u32 *) o_arg.u.verifier.data;
		p[0] = jiffies;
		p[1] = current->pid;
	} else
		o_arg.u.attrs = sattr;
	/* Serialization for the sequence id */
	down(&sp->so_sema);

	status = _nfs4_proc_open(dir, sp, &o_arg, &o_res);
	if (status != 0)
		goto out_err;

	status = -ENOMEM;
	inode = nfs_fhget(dir->i_sb, &o_res.fh, &f_attr);
	if (!inode)
		goto out_err;
	state = nfs4_get_open_state(inode, sp);
	if (!state)
		goto out_err;
	update_open_stateid(state, &o_res.stateid, flags);
	if (o_res.delegation_type != 0)
		nfs_inode_set_delegation(inode, cred, &o_res);
	up(&sp->so_sema);
	nfs4_put_state_owner(sp);
	up_read(&clp->cl_sem);
	*res = state;
	return 0;
out_err:
	if (sp != NULL) {
		if (state != NULL)
			nfs4_put_open_state(state);
		up(&sp->so_sema);
		nfs4_put_state_owner(sp);
	}
	/* Note: clp->cl_sem must be released before nfs4_put_open_state()! */
	up_read(&clp->cl_sem);
	if (inode != NULL)
		iput(inode);
	*res = NULL;
	return status;
}


static struct nfs4_state *nfs4_do_open(struct inode *dir, struct dentry *dentry, int flags, struct iattr *sattr, struct rpc_cred *cred)
{
	struct nfs4_exception exception = { };
	struct nfs4_state *res;
	int status;

	do {
		status = _nfs4_do_open(dir, dentry, flags, sattr, cred, &res);
		if (status == 0)
			break;
		/* NOTE: BAD_SEQID means the server and client disagree about the
		 * book-keeping w.r.t. state-changing operations
		 * (OPEN/CLOSE/LOCK/LOCKU...)
		 * It is actually a sign of a bug on the client or on the server.
		 *
		 * If we receive a BAD_SEQID error in the particular case of
		 * doing an OPEN, we assume that nfs4_increment_seqid() will
		 * have unhashed the old state_owner for us, and that we can
		 * therefore safely retry using a new one. We should still warn
		 * the user though...
		 */
		if (status == -NFS4ERR_BAD_SEQID) {
			printk(KERN_WARNING "NFS: v4 server returned a bad sequence-id error!\n");
			exception.retry = 1;
			continue;
		}
		res = ERR_PTR(nfs4_handle_exception(NFS_SERVER(dir),
					status, &exception));
	} while (exception.retry);
	return res;
}

static int _nfs4_do_setattr(struct nfs_server *server, struct nfs_fattr *fattr,
                struct nfs_fh *fhandle, struct iattr *sattr,
                struct nfs4_state *state)
{
        struct nfs_setattrargs  arg = {
                .fh             = fhandle,
                .iap            = sattr,
		.server		= server,
		.bitmask = server->attr_bitmask,
        };
        struct nfs_setattrres  res = {
		.fattr		= fattr,
		.server		= server,
        };
        struct rpc_message msg = {
                .rpc_proc       = &nfs4_procedures[NFSPROC4_CLNT_SETATTR],
                .rpc_argp       = &arg,
                .rpc_resp       = &res,
        };

        fattr->valid = 0;

	if (state != NULL)
		msg.rpc_cred = state->owner->so_cred;
	if (sattr->ia_valid & ATTR_SIZE)
		nfs4_copy_stateid(&arg.stateid, state, NULL);
	else
		memcpy(&arg.stateid, &zero_stateid, sizeof(arg.stateid));

	return rpc_call_sync(server->client, &msg, 0);
}

static int nfs4_do_setattr(struct nfs_server *server, struct nfs_fattr *fattr,
                struct nfs_fh *fhandle, struct iattr *sattr,
                struct nfs4_state *state)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(server,
				_nfs4_do_setattr(server, fattr, fhandle, sattr,
					state),
				&exception);
	} while (exception.retry);
	return err;
}

struct nfs4_closedata {
	struct inode *inode;
	struct nfs4_state *state;
	struct nfs_closeargs arg;
	struct nfs_closeres res;
};

static void nfs4_close_done(struct rpc_task *task)
{
	struct nfs4_closedata *calldata = (struct nfs4_closedata *)task->tk_calldata;
	struct nfs4_state *state = calldata->state;
	struct nfs4_state_owner *sp = state->owner;
	struct nfs_server *server = NFS_SERVER(calldata->inode);

        /* hmm. we are done with the inode, and in the process of freeing
	 * the state_owner. we keep this around to process errors
	 */
	nfs4_increment_seqid(task->tk_status, sp);
	switch (task->tk_status) {
		case 0:
			memcpy(&state->stateid, &calldata->res.stateid,
					sizeof(state->stateid));
			break;
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_EXPIRED:
			state->state = calldata->arg.open_flags;
			nfs4_schedule_state_recovery(server->nfs4_state);
			break;
		default:
			if (nfs4_async_handle_error(task, server) == -EAGAIN) {
				rpc_restart_call(task);
				return;
			}
	}
	state->state = calldata->arg.open_flags;
	nfs4_put_open_state(state);
	up(&sp->so_sema);
	nfs4_put_state_owner(sp);
	up_read(&server->nfs4_state->cl_sem);
	kfree(calldata);
}

static inline int nfs4_close_call(struct rpc_clnt *clnt, struct nfs4_closedata *calldata)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_CLOSE],
		.rpc_argp = &calldata->arg,
		.rpc_resp = &calldata->res,
		.rpc_cred = calldata->state->owner->so_cred,
	};
	if (calldata->arg.open_flags != 0)
		msg.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_OPEN_DOWNGRADE];
	return rpc_call_async(clnt, &msg, 0, nfs4_close_done, calldata);
}

/* 
 * It is possible for data to be read/written from a mem-mapped file 
 * after the sys_close call (which hits the vfs layer as a flush).
 * This means that we can't safely call nfsv4 close on a file until 
 * the inode is cleared. This in turn means that we are not good
 * NFSv4 citizens - we do not indicate to the server to update the file's 
 * share state even when we are done with one of the three share 
 * stateid's in the inode.
 *
 * NOTE: Caller must be holding the sp->so_owner semaphore!
 */
int nfs4_do_close(struct inode *inode, struct nfs4_state *state, mode_t mode) 
{
	struct nfs4_closedata *calldata;
	int status;

	/* Tell caller we're done */
	if (test_bit(NFS_DELEGATED_STATE, &state->flags)) {
		state->state = mode;
		return 0;
	}
	calldata = (struct nfs4_closedata *)kmalloc(sizeof(*calldata), GFP_KERNEL);
	if (calldata == NULL)
		return -ENOMEM;
	calldata->inode = inode;
	calldata->state = state;
	calldata->arg.fh = NFS_FH(inode);
	/* Serialization for the sequence id */
	calldata->arg.seqid = state->owner->so_seqid;
	calldata->arg.open_flags = mode;
	memcpy(&calldata->arg.stateid, &state->stateid,
			sizeof(calldata->arg.stateid));
	status = nfs4_close_call(NFS_SERVER(inode)->client, calldata);
	/*
	 * Return -EINPROGRESS on success in order to indicate to the
	 * caller that an asynchronous RPC call has been launched, and
	 * that it will release the semaphores on completion.
	 */
	return (status == 0) ? -EINPROGRESS : status;
}

struct inode *
nfs4_atomic_open(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct iattr attr;
	struct rpc_cred *cred;
	struct nfs4_state *state;

	if (nd->flags & LOOKUP_CREATE) {
		attr.ia_mode = nd->intent.open.create_mode;
		attr.ia_valid = ATTR_MODE;
		if (!IS_POSIXACL(dir))
			attr.ia_mode &= ~current->fs->umask;
	} else {
		attr.ia_valid = 0;
		BUG_ON(nd->intent.open.flags & O_CREAT);
	}

	cred = rpcauth_lookupcred(NFS_SERVER(dir)->client->cl_auth, 0);
	if (IS_ERR(cred))
		return (struct inode *)cred;
	state = nfs4_do_open(dir, dentry, nd->intent.open.flags, &attr, cred);
	put_rpccred(cred);
	if (IS_ERR(state))
		return (struct inode *)state;
	return state->inode;
}

int
nfs4_open_revalidate(struct inode *dir, struct dentry *dentry, int openflags)
{
	struct rpc_cred *cred;
	struct nfs4_state *state;
	struct inode *inode;

	cred = rpcauth_lookupcred(NFS_SERVER(dir)->client->cl_auth, 0);
	if (IS_ERR(cred))
		return PTR_ERR(cred);
	state = nfs4_open_delegated(dentry->d_inode, openflags, cred);
	if (IS_ERR(state))
		state = nfs4_do_open(dir, dentry, openflags, NULL, cred);
	put_rpccred(cred);
	if (state == ERR_PTR(-ENOENT) && dentry->d_inode == 0)
		return 1;
	if (IS_ERR(state))
		return 0;
	inode = state->inode;
	if (inode == dentry->d_inode) {
		iput(inode);
		return 1;
	}
	d_drop(dentry);
	nfs4_close_state(state, openflags);
	iput(inode);
	return 0;
}


static int _nfs4_server_capabilities(struct nfs_server *server, struct nfs_fh *fhandle)
{
	struct nfs4_server_caps_res res = {};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SERVER_CAPS],
		.rpc_argp = fhandle,
		.rpc_resp = &res,
	};
	int status;

	status = rpc_call_sync(server->client, &msg, 0);
	if (status == 0) {
		memcpy(server->attr_bitmask, res.attr_bitmask, sizeof(server->attr_bitmask));
		if (res.attr_bitmask[0] & FATTR4_WORD0_ACL)
			server->caps |= NFS_CAP_ACLS;
		if (res.has_links != 0)
			server->caps |= NFS_CAP_HARDLINKS;
		if (res.has_symlinks != 0)
			server->caps |= NFS_CAP_SYMLINKS;
		server->acl_bitmask = res.acl_bitmask;
	}
	return status;
}

static int nfs4_server_capabilities(struct nfs_server *server, struct nfs_fh *fhandle)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(server,
				_nfs4_server_capabilities(server, fhandle),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_lookup_root(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_fsinfo *info)
{
	struct nfs_fattr *	fattr = info->fattr;
	struct nfs4_lookup_root_arg args = {
		.bitmask = nfs4_fattr_bitmap,
	};
	struct nfs4_lookup_res res = {
		.server = server,
		.fattr = fattr,
		.fh = fhandle,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LOOKUP_ROOT],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	fattr->valid = 0;
	return rpc_call_sync(server->client, &msg, 0);
}

static int nfs4_lookup_root(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_fsinfo *info)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(server,
				_nfs4_lookup_root(server, fhandle, info),
				&exception);
	} while (exception.retry);
	return err;
}

static int nfs4_proc_get_root(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_fsinfo *info)
{
	struct nfs_fattr *	fattr = info->fattr;
	unsigned char *		p;
	struct qstr		q;
	struct nfs4_lookup_arg args = {
		.dir_fh = fhandle,
		.name = &q,
		.bitmask = nfs4_fattr_bitmap,
	};
	struct nfs4_lookup_res res = {
		.server = server,
		.fattr = fattr,
		.fh = fhandle,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LOOKUP],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	int status;

	/*
	 * Now we do a separate LOOKUP for each component of the mount path.
	 * The LOOKUPs are done separately so that we can conveniently
	 * catch an ERR_WRONGSEC if it occurs along the way...
	 */
	status = nfs4_lookup_root(server, fhandle, info);
	if (status)
		goto out;

	p = server->mnt_path;
	for (;;) {
		struct nfs4_exception exception = { };

		while (*p == '/')
			p++;
		if (!*p)
			break;
		q.name = p;
		while (*p && (*p != '/'))
			p++;
		q.len = p - q.name;

		do {
			fattr->valid = 0;
			status = nfs4_handle_exception(server,
					rpc_call_sync(server->client, &msg, 0),
					&exception);
		} while (exception.retry);
		if (status == 0)
			continue;
		if (status == -ENOENT) {
			printk(KERN_NOTICE "NFS: mount path %s does not exist!\n", server->mnt_path);
			printk(KERN_NOTICE "NFS: suggestion: try mounting '/' instead.\n");
		}
		break;
	}
	if (status == 0)
		status = nfs4_server_capabilities(server, fhandle);
	if (status == 0)
		status = nfs4_do_fsinfo(server, fhandle, info);
out:
	return status;
}

static int _nfs4_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs4_getattr_arg args = {
		.fh = fhandle,
		.bitmask = server->attr_bitmask,
	};
	struct nfs4_getattr_res res = {
		.fattr = fattr,
		.server = server,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_GETATTR],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	
	fattr->valid = 0;
	return rpc_call_sync(server->client, &msg, 0);
}

static int nfs4_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(server,
				_nfs4_proc_getattr(server, fhandle, fattr),
				&exception);
	} while (exception.retry);
	return err;
}

/* 
 * The file is not closed if it is opened due to the a request to change
 * the size of the file. The open call will not be needed once the
 * VFS layer lookup-intents are implemented.
 *
 * Close is called when the inode is destroyed.
 * If we haven't opened the file for O_WRONLY, we
 * need to in the size_change case to obtain a stateid.
 *
 * Got race?
 * Because OPEN is always done by name in nfsv4, it is
 * possible that we opened a different file by the same
 * name.  We can recognize this race condition, but we
 * can't do anything about it besides returning an error.
 *
 * This will be fixed with VFS changes (lookup-intent).
 */
static int
nfs4_proc_setattr(struct dentry *dentry, struct nfs_fattr *fattr,
		  struct iattr *sattr)
{
	struct inode *		inode = dentry->d_inode;
	int			size_change = sattr->ia_valid & ATTR_SIZE;
	struct nfs4_state	*state = NULL;
	int need_iput = 0;
	int status;

	fattr->valid = 0;
	
	if (size_change) {
		struct rpc_cred *cred = rpcauth_lookupcred(NFS_SERVER(inode)->client->cl_auth, 0);
		if (IS_ERR(cred))
			return PTR_ERR(cred);
		state = nfs4_find_state(inode, cred, FMODE_WRITE);
		if (state == NULL) {
			state = nfs4_open_delegated(dentry->d_inode,
					FMODE_WRITE, cred);
			if (IS_ERR(state))
				state = nfs4_do_open(dentry->d_parent->d_inode,
						dentry, FMODE_WRITE,
						NULL, cred);
			need_iput = 1;
		}
		put_rpccred(cred);
		if (IS_ERR(state))
			return PTR_ERR(state);

		if (state->inode != inode) {
			printk(KERN_WARNING "nfs: raced in setattr (%p != %p), returning -EIO\n", inode, state->inode);
			status = -EIO;
			goto out;
		}
	}
	status = nfs4_do_setattr(NFS_SERVER(inode), fattr,
			NFS_FH(inode), sattr, state);
out:
	if (state) {
		inode = state->inode;
		nfs4_close_state(state, FMODE_WRITE);
		if (need_iput)
			iput(inode);
	}
	return status;
}

static int _nfs4_proc_lookup(struct inode *dir, struct qstr *name,
		struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	int		       status;
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs4_lookup_arg args = {
		.bitmask = server->attr_bitmask,
		.dir_fh = NFS_FH(dir),
		.name = name,
	};
	struct nfs4_lookup_res res = {
		.server = server,
		.fattr = fattr,
		.fh = fhandle,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LOOKUP],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	
	fattr->valid = 0;
	
	dprintk("NFS call  lookup %s\n", name->name);
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	dprintk("NFS reply lookup: %d\n", status);
	return status;
}

static int nfs4_proc_lookup(struct inode *dir, struct qstr *name, struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(dir),
				_nfs4_proc_lookup(dir, name, fhandle, fattr),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_access(struct inode *inode, struct nfs_access_entry *entry)
{
	struct nfs4_accessargs args = {
		.fh = NFS_FH(inode),
	};
	struct nfs4_accessres res = { 0 };
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_ACCESS],
		.rpc_argp = &args,
		.rpc_resp = &res,
		.rpc_cred = entry->cred,
	};
	int mode = entry->mask;
	int status;

	/*
	 * Determine which access bits we want to ask for...
	 */
	if (mode & MAY_READ)
		args.access |= NFS4_ACCESS_READ;
	if (S_ISDIR(inode->i_mode)) {
		if (mode & MAY_WRITE)
			args.access |= NFS4_ACCESS_MODIFY | NFS4_ACCESS_EXTEND | NFS4_ACCESS_DELETE;
		if (mode & MAY_EXEC)
			args.access |= NFS4_ACCESS_LOOKUP;
	} else {
		if (mode & MAY_WRITE)
			args.access |= NFS4_ACCESS_MODIFY | NFS4_ACCESS_EXTEND;
		if (mode & MAY_EXEC)
			args.access |= NFS4_ACCESS_EXECUTE;
	}
	status = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	if (!status) {
		entry->mask = 0;
		if (res.access & NFS4_ACCESS_READ)
			entry->mask |= MAY_READ;
		if (res.access & (NFS4_ACCESS_MODIFY | NFS4_ACCESS_EXTEND | NFS4_ACCESS_DELETE))
			entry->mask |= MAY_WRITE;
		if (res.access & (NFS4_ACCESS_LOOKUP|NFS4_ACCESS_EXECUTE))
			entry->mask |= MAY_EXEC;
	}
	return status;
}

static int nfs4_proc_access(struct inode *inode, struct nfs_access_entry *entry)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(inode),
				_nfs4_proc_access(inode, entry),
				&exception);
	} while (exception.retry);
	return err;
}

/*
 * TODO: For the time being, we don't try to get any attributes
 * along with any of the zero-copy operations READ, READDIR,
 * READLINK, WRITE.
 *
 * In the case of the first three, we want to put the GETATTR
 * after the read-type operation -- this is because it is hard
 * to predict the length of a GETATTR response in v4, and thus
 * align the READ data correctly.  This means that the GETATTR
 * may end up partially falling into the page cache, and we should
 * shift it into the 'tail' of the xdr_buf before processing.
 * To do this efficiently, we need to know the total length
 * of data received, which doesn't seem to be available outside
 * of the RPC layer.
 *
 * In the case of WRITE, we also want to put the GETATTR after
 * the operation -- in this case because we want to make sure
 * we get the post-operation mtime and size.  This means that
 * we can't use xdr_encode_pages() as written: we need a variant
 * of it which would leave room in the 'tail' iovec.
 *
 * Both of these changes to the XDR layer would in fact be quite
 * minor, but I decided to leave them for a subsequent patch.
 */
static int _nfs4_proc_readlink(struct inode *inode, struct page *page,
		unsigned int pgbase, unsigned int pglen)
{
	struct nfs4_readlink args = {
		.fh       = NFS_FH(inode),
		.pgbase	  = pgbase,
		.pglen    = pglen,
		.pages    = &page,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READLINK],
		.rpc_argp = &args,
		.rpc_resp = NULL,
	};

	return rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
}

static int nfs4_proc_readlink(struct inode *inode, struct page *page,
		unsigned int pgbase, unsigned int pglen)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(inode),
				_nfs4_proc_readlink(inode, page, pgbase, pglen),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_read(struct nfs_read_data *rdata)
{
	int flags = rdata->flags;
	struct inode *inode = rdata->inode;
	struct nfs_fattr *fattr = rdata->res.fattr;
	struct nfs_server *server = NFS_SERVER(inode);
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_READ],
		.rpc_argp	= &rdata->args,
		.rpc_resp	= &rdata->res,
		.rpc_cred	= rdata->cred,
	};
	unsigned long timestamp = jiffies;
	int status;

	dprintk("NFS call  read %d @ %Ld\n", rdata->args.count,
			(long long) rdata->args.offset);

	fattr->valid = 0;
	status = rpc_call_sync(server->client, &msg, flags);
	if (!status)
		renew_lease(server, timestamp);
	dprintk("NFS reply read: %d\n", status);
	return status;
}

static int nfs4_proc_read(struct nfs_read_data *rdata)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(rdata->inode),
				_nfs4_proc_read(rdata),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_write(struct nfs_write_data *wdata)
{
	int rpcflags = wdata->flags;
	struct inode *inode = wdata->inode;
	struct nfs_fattr *fattr = wdata->res.fattr;
	struct nfs_server *server = NFS_SERVER(inode);
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_WRITE],
		.rpc_argp	= &wdata->args,
		.rpc_resp	= &wdata->res,
		.rpc_cred	= wdata->cred,
	};
	int status;

	dprintk("NFS call  write %d @ %Ld\n", wdata->args.count,
			(long long) wdata->args.offset);

	fattr->valid = 0;
	status = rpc_call_sync(server->client, &msg, rpcflags);
	dprintk("NFS reply write: %d\n", status);
	return status;
}

static int nfs4_proc_write(struct nfs_write_data *wdata)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(wdata->inode),
				_nfs4_proc_write(wdata),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_commit(struct nfs_write_data *cdata)
{
	struct inode *inode = cdata->inode;
	struct nfs_fattr *fattr = cdata->res.fattr;
	struct nfs_server *server = NFS_SERVER(inode);
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_COMMIT],
		.rpc_argp	= &cdata->args,
		.rpc_resp	= &cdata->res,
		.rpc_cred	= cdata->cred,
	};
	int status;

	dprintk("NFS call  commit %d @ %Ld\n", cdata->args.count,
			(long long) cdata->args.offset);

	fattr->valid = 0;
	status = rpc_call_sync(server->client, &msg, 0);
	dprintk("NFS reply commit: %d\n", status);
	return status;
}

static int nfs4_proc_commit(struct nfs_write_data *cdata)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(cdata->inode),
				_nfs4_proc_commit(cdata),
				&exception);
	} while (exception.retry);
	return err;
}

/*
 * Got race?
 * We will need to arrange for the VFS layer to provide an atomic open.
 * Until then, this create/open method is prone to inefficiency and race
 * conditions due to the lookup, create, and open VFS calls from sys_open()
 * placed on the wire.
 *
 * Given the above sorry state of affairs, I'm simply sending an OPEN.
 * The file will be opened again in the subsequent VFS open call
 * (nfs4_proc_file_open).
 *
 * The open for read will just hang around to be used by any process that
 * opens the file O_RDONLY. This will all be resolved with the VFS changes.
 */

static int
nfs4_proc_create(struct inode *dir, struct dentry *dentry, struct iattr *sattr,
                 int flags)
{
	struct nfs4_state *state;
	struct rpc_cred *cred;
	int status = 0;

	cred = rpcauth_lookupcred(NFS_SERVER(dir)->client->cl_auth, 0);
	if (IS_ERR(cred)) {
		status = PTR_ERR(cred);
		goto out;
	}
	state = nfs4_do_open(dir, dentry, flags, sattr, cred);
	put_rpccred(cred);
	if (IS_ERR(state)) {
		status = PTR_ERR(state);
		goto out;
	}
	d_instantiate(dentry, state->inode);
	if (flags & O_EXCL) {
		struct nfs_fattr fattr;
		status = nfs4_do_setattr(NFS_SERVER(dir), &fattr,
		                     NFS_FH(state->inode), sattr, state);
		if (status == 0)
			goto out;
	} else if (flags != 0)
		goto out;
	nfs4_close_state(state, flags);
out:
	return status;
}

static int _nfs4_proc_remove(struct inode *dir, struct qstr *name)
{
	struct nfs4_remove_arg args = {
		.fh = NFS_FH(dir),
		.name = name,
	};
	struct nfs4_change_info	res;
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_REMOVE],
		.rpc_argp	= &args,
		.rpc_resp	= &res,
	};
	int			status;

	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	if (status == 0)
		update_changeattr(dir, &res);
	return status;
}

static int nfs4_proc_remove(struct inode *dir, struct qstr *name)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(dir),
				_nfs4_proc_remove(dir, name),
				&exception);
	} while (exception.retry);
	return err;
}

struct unlink_desc {
	struct nfs4_remove_arg	args;
	struct nfs4_change_info	res;
};

static int nfs4_proc_unlink_setup(struct rpc_message *msg, struct dentry *dir,
		struct qstr *name)
{
	struct unlink_desc *up;

	up = (struct unlink_desc *) kmalloc(sizeof(*up), GFP_KERNEL);
	if (!up)
		return -ENOMEM;
	
	up->args.fh = NFS_FH(dir->d_inode);
	up->args.name = name;
	
	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_REMOVE];
	msg->rpc_argp = &up->args;
	msg->rpc_resp = &up->res;
	return 0;
}

static int nfs4_proc_unlink_done(struct dentry *dir, struct rpc_task *task)
{
	struct rpc_message *msg = &task->tk_msg;
	struct unlink_desc *up;
	
	if (msg->rpc_resp != NULL) {
		up = container_of(msg->rpc_resp, struct unlink_desc, res);
		update_changeattr(dir->d_inode, &up->res);
		kfree(up);
		msg->rpc_resp = NULL;
		msg->rpc_argp = NULL;
	}
	return 0;
}

static int _nfs4_proc_rename(struct inode *old_dir, struct qstr *old_name,
		struct inode *new_dir, struct qstr *new_name)
{
	struct nfs4_rename_arg arg = {
		.old_dir = NFS_FH(old_dir),
		.new_dir = NFS_FH(new_dir),
		.old_name = old_name,
		.new_name = new_name,
	};
	struct nfs4_rename_res res = { };
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_RENAME],
		.rpc_argp = &arg,
		.rpc_resp = &res,
	};
	int			status;
	
	status = rpc_call_sync(NFS_CLIENT(old_dir), &msg, 0);

	if (!status) {
		update_changeattr(old_dir, &res.old_cinfo);
		update_changeattr(new_dir, &res.new_cinfo);
	}
	return status;
}

static int nfs4_proc_rename(struct inode *old_dir, struct qstr *old_name,
		struct inode *new_dir, struct qstr *new_name)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(old_dir),
				_nfs4_proc_rename(old_dir, old_name,
					new_dir, new_name),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_link(struct inode *inode, struct inode *dir, struct qstr *name)
{
	struct nfs4_link_arg arg = {
		.fh     = NFS_FH(inode),
		.dir_fh = NFS_FH(dir),
		.name   = name,
	};
	struct nfs4_change_info	cinfo = { };
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LINK],
		.rpc_argp = &arg,
		.rpc_resp = &cinfo,
	};
	int			status;

	status = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	if (!status)
		update_changeattr(dir, &cinfo);

	return status;
}

static int nfs4_proc_link(struct inode *inode, struct inode *dir, struct qstr *name)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(inode),
				_nfs4_proc_link(inode, dir, name),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_symlink(struct inode *dir, struct qstr *name,
		struct qstr *path, struct iattr *sattr, struct nfs_fh *fhandle,
		struct nfs_fattr *fattr)
{
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs4_create_arg arg = {
		.dir_fh = NFS_FH(dir),
		.server = server,
		.name = name,
		.attrs = sattr,
		.ftype = NF4LNK,
		.bitmask = server->attr_bitmask,
	};
	struct nfs4_create_res res = {
		.server = server,
		.fh = fhandle,
		.fattr = fattr,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SYMLINK],
		.rpc_argp = &arg,
		.rpc_resp = &res,
	};
	int			status;

	if (path->len > NFS4_MAXPATHLEN)
		return -ENAMETOOLONG;
	arg.u.symlink = path;
	fattr->valid = 0;
	
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	if (!status)
		update_changeattr(dir, &res.dir_cinfo);
	return status;
}

static int nfs4_proc_symlink(struct inode *dir, struct qstr *name,
		struct qstr *path, struct iattr *sattr, struct nfs_fh *fhandle,
		struct nfs_fattr *fattr)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(dir),
				_nfs4_proc_symlink(dir, name, path, sattr,
					fhandle, fattr),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_mkdir(struct inode *dir, struct dentry *dentry,
		struct iattr *sattr)
{
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_fh fhandle;
	struct nfs_fattr fattr;
	struct nfs4_create_arg arg = {
		.dir_fh = NFS_FH(dir),
		.server = server,
		.name = &dentry->d_name,
		.attrs = sattr,
		.ftype = NF4DIR,
		.bitmask = server->attr_bitmask,
	};
	struct nfs4_create_res res = {
		.server = server,
		.fh = &fhandle,
		.fattr = &fattr,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_CREATE],
		.rpc_argp = &arg,
		.rpc_resp = &res,
	};
	int			status;

	fattr.valid = 0;
	
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	if (!status) {
		update_changeattr(dir, &res.dir_cinfo);
		status = nfs_instantiate(dentry, &fhandle, &fattr);
	}
	return status;
}

static int nfs4_proc_mkdir(struct inode *dir, struct dentry *dentry,
		struct iattr *sattr)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(dir),
				_nfs4_proc_mkdir(dir, dentry, sattr),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_readdir(struct dentry *dentry, struct rpc_cred *cred,
                  u64 cookie, struct page *page, unsigned int count, int plus)
{
	struct inode		*dir = dentry->d_inode;
	struct nfs4_readdir_arg args = {
		.fh = NFS_FH(dir),
		.pages = &page,
		.pgbase = 0,
		.count = count,
		.bitmask = NFS_SERVER(dentry->d_inode)->attr_bitmask,
	};
	struct nfs4_readdir_res res;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READDIR],
		.rpc_argp = &args,
		.rpc_resp = &res,
		.rpc_cred = cred,
	};
	int			status;

	lock_kernel();
	nfs4_setup_readdir(cookie, NFS_COOKIEVERF(dir), dentry, &args);
	res.pgbase = args.pgbase;
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	if (status == 0)
		memcpy(NFS_COOKIEVERF(dir), res.verifier.data, NFS4_VERIFIER_SIZE);
	unlock_kernel();
	return status;
}

static int nfs4_proc_readdir(struct dentry *dentry, struct rpc_cred *cred,
                  u64 cookie, struct page *page, unsigned int count, int plus)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(dentry->d_inode),
				_nfs4_proc_readdir(dentry, cred, cookie,
					page, count, plus),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_mknod(struct inode *dir, struct dentry *dentry,
		struct iattr *sattr, dev_t rdev)
{
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_fh fh;
	struct nfs_fattr fattr;
	struct nfs4_create_arg arg = {
		.dir_fh = NFS_FH(dir),
		.server = server,
		.name = &dentry->d_name,
		.attrs = sattr,
		.bitmask = server->attr_bitmask,
	};
	struct nfs4_create_res res = {
		.server = server,
		.fh = &fh,
		.fattr = &fattr,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_CREATE],
		.rpc_argp = &arg,
		.rpc_resp = &res,
	};
	int			status;
	int                     mode = sattr->ia_mode;

	fattr.valid = 0;

	BUG_ON(!(sattr->ia_valid & ATTR_MODE));
	BUG_ON(!S_ISFIFO(mode) && !S_ISBLK(mode) && !S_ISCHR(mode) && !S_ISSOCK(mode));
	if (S_ISFIFO(mode))
		arg.ftype = NF4FIFO;
	else if (S_ISBLK(mode)) {
		arg.ftype = NF4BLK;
		arg.u.device.specdata1 = MAJOR(rdev);
		arg.u.device.specdata2 = MINOR(rdev);
	}
	else if (S_ISCHR(mode)) {
		arg.ftype = NF4CHR;
		arg.u.device.specdata1 = MAJOR(rdev);
		arg.u.device.specdata2 = MINOR(rdev);
	}
	else
		arg.ftype = NF4SOCK;
	
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	if (status == 0) {
		update_changeattr(dir, &res.dir_cinfo);
		status = nfs_instantiate(dentry, &fh, &fattr);
	}
	return status;
}

static int nfs4_proc_mknod(struct inode *dir, struct dentry *dentry,
		struct iattr *sattr, dev_t rdev)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(dir),
				_nfs4_proc_mknod(dir, dentry, sattr, rdev),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle,
		 struct nfs_fsstat *fsstat)
{
	struct nfs4_statfs_arg args = {
		.fh = fhandle,
		.bitmask = server->attr_bitmask,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_STATFS],
		.rpc_argp = &args,
		.rpc_resp = fsstat,
	};

	fsstat->fattr->valid = 0;
	return rpc_call_sync(server->client, &msg, 0);
}

static int nfs4_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle, struct nfs_fsstat *fsstat)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(server,
				_nfs4_proc_statfs(server, fhandle, fsstat),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_do_fsinfo(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_fsinfo *fsinfo)
{
	struct nfs4_fsinfo_arg args = {
		.fh = fhandle,
		.bitmask = server->attr_bitmask,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_FSINFO],
		.rpc_argp = &args,
		.rpc_resp = fsinfo,
	};

	return rpc_call_sync(server->client, &msg, 0);
}

static int nfs4_do_fsinfo(struct nfs_server *server, struct nfs_fh *fhandle, struct nfs_fsinfo *fsinfo)
{
	struct nfs4_exception exception = { };
	int err;

	do {
		err = nfs4_handle_exception(server,
				_nfs4_do_fsinfo(server, fhandle, fsinfo),
				&exception);
	} while (exception.retry);
	return err;
}

static int nfs4_proc_fsinfo(struct nfs_server *server, struct nfs_fh *fhandle, struct nfs_fsinfo *fsinfo)
{
	fsinfo->fattr->valid = 0;
	return nfs4_do_fsinfo(server, fhandle, fsinfo);
}

static int _nfs4_proc_pathconf(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_pathconf *pathconf)
{
	struct nfs4_pathconf_arg args = {
		.fh = fhandle,
		.bitmask = server->attr_bitmask,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_PATHCONF],
		.rpc_argp = &args,
		.rpc_resp = pathconf,
	};

	/* None of the pathconf attributes are mandatory to implement */
	if ((args.bitmask[0] & nfs4_pathconf_bitmap[0]) == 0) {
		memset(pathconf, 0, sizeof(*pathconf));
		return 0;
	}

	pathconf->fattr->valid = 0;
	return rpc_call_sync(server->client, &msg, 0);
}

static int nfs4_proc_pathconf(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_pathconf *pathconf)
{
	struct nfs4_exception exception = { };
	int err;

	do {
		err = nfs4_handle_exception(server,
				_nfs4_proc_pathconf(server, fhandle, pathconf),
				&exception);
	} while (exception.retry);
	return err;
}

static void
nfs4_read_done(struct rpc_task *task)
{
	struct nfs_read_data *data = (struct nfs_read_data *) task->tk_calldata;
	struct inode *inode = data->inode;

	if (nfs4_async_handle_error(task, NFS_SERVER(inode)) == -EAGAIN) {
		rpc_restart_call(task);
		return;
	}
	if (task->tk_status > 0)
		renew_lease(NFS_SERVER(inode), data->timestamp);
	/* Call back common NFS readpage processing */
	nfs_readpage_result(task);
}

static void
nfs4_proc_read_setup(struct nfs_read_data *data)
{
	struct rpc_task	*task = &data->task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READ],
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};
	struct inode *inode = data->inode;
	int flags;

	data->timestamp   = jiffies;

	/* N.B. Do we need to test? Never called for swapfile inode */
	flags = RPC_TASK_ASYNC | (IS_SWAPFILE(inode)? NFS_RPC_SWAPFLAGS : 0);

	/* Finalize the task. */
	rpc_init_task(task, NFS_CLIENT(inode), nfs4_read_done, flags);
	rpc_call_setup(task, &msg, 0);
}

static void
nfs4_write_done(struct rpc_task *task)
{
	struct nfs_write_data *data = (struct nfs_write_data *) task->tk_calldata;
	struct inode *inode = data->inode;
	
	if (nfs4_async_handle_error(task, NFS_SERVER(inode)) == -EAGAIN) {
		rpc_restart_call(task);
		return;
	}
	if (task->tk_status >= 0)
		renew_lease(NFS_SERVER(inode), data->timestamp);
	/* Call back common NFS writeback processing */
	nfs_writeback_done(task);
}

static void
nfs4_proc_write_setup(struct nfs_write_data *data, int how)
{
	struct rpc_task	*task = &data->task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_WRITE],
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};
	struct inode *inode = data->inode;
	int stable;
	int flags;
	
	if (how & FLUSH_STABLE) {
		if (!NFS_I(inode)->ncommit)
			stable = NFS_FILE_SYNC;
		else
			stable = NFS_DATA_SYNC;
	} else
		stable = NFS_UNSTABLE;
	data->args.stable = stable;

	data->timestamp   = jiffies;

	/* Set the initial flags for the task.  */
	flags = (how & FLUSH_SYNC) ? 0 : RPC_TASK_ASYNC;

	/* Finalize the task. */
	rpc_init_task(task, NFS_CLIENT(inode), nfs4_write_done, flags);
	rpc_call_setup(task, &msg, 0);
}

static void
nfs4_commit_done(struct rpc_task *task)
{
	struct nfs_write_data *data = (struct nfs_write_data *) task->tk_calldata;
	struct inode *inode = data->inode;
	
	if (nfs4_async_handle_error(task, NFS_SERVER(inode)) == -EAGAIN) {
		rpc_restart_call(task);
		return;
	}
	/* Call back common NFS writeback processing */
	nfs_commit_done(task);
}

static void
nfs4_proc_commit_setup(struct nfs_write_data *data, int how)
{
	struct rpc_task	*task = &data->task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_COMMIT],
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};	
	struct inode *inode = data->inode;
	int flags;
	
	/* Set the initial flags for the task.  */
	flags = (how & FLUSH_SYNC) ? 0 : RPC_TASK_ASYNC;

	/* Finalize the task. */
	rpc_init_task(task, NFS_CLIENT(inode), nfs4_commit_done, flags);
	rpc_call_setup(task, &msg, 0);	
}

/*
 * nfs4_proc_async_renew(): This is not one of the nfs_rpc_ops; it is a special
 * standalone procedure for queueing an asynchronous RENEW.
 */
static void
renew_done(struct rpc_task *task)
{
	struct nfs4_client *clp = (struct nfs4_client *)task->tk_msg.rpc_argp;
	unsigned long timestamp = (unsigned long)task->tk_calldata;

	if (task->tk_status < 0) {
		switch (task->tk_status) {
			case -NFS4ERR_STALE_CLIENTID:
			case -NFS4ERR_EXPIRED:
			case -NFS4ERR_CB_PATH_DOWN:
				nfs4_schedule_state_recovery(clp);
		}
		return;
	}
	spin_lock(&clp->cl_lock);
	if (time_before(clp->cl_last_renewal,timestamp))
		clp->cl_last_renewal = timestamp;
	spin_unlock(&clp->cl_lock);
}

int
nfs4_proc_async_renew(struct nfs4_client *clp)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_RENEW],
		.rpc_argp	= clp,
		.rpc_cred	= clp->cl_cred,
	};

	return rpc_call_async(clp->cl_rpcclient, &msg, RPC_TASK_SOFT,
			renew_done, (void *)jiffies);
}

int
nfs4_proc_renew(struct nfs4_client *clp)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_RENEW],
		.rpc_argp	= clp,
		.rpc_cred	= clp->cl_cred,
	};
	unsigned long now = jiffies;
	int status;

	status = rpc_call_sync(clp->cl_rpcclient, &msg, 0);
	if (status < 0)
		return status;
	spin_lock(&clp->cl_lock);
	if (time_before(clp->cl_last_renewal,now))
		clp->cl_last_renewal = now;
	spin_unlock(&clp->cl_lock);
	return 0;
}

/*
 * We will need to arrange for the VFS layer to provide an atomic open.
 * Until then, this open method is prone to inefficiency and race conditions
 * due to the lookup, potential create, and open VFS calls from sys_open()
 * placed on the wire.
 */
static int
nfs4_proc_file_open(struct inode *inode, struct file *filp)
{
	struct dentry *dentry = filp->f_dentry;
	struct nfs_open_context *ctx;
	struct nfs4_state *state = NULL;
	struct rpc_cred *cred;
	int status = -ENOMEM;

	dprintk("nfs4_proc_file_open: starting on (%.*s/%.*s)\n",
	                       (int)dentry->d_parent->d_name.len,
	                       dentry->d_parent->d_name.name,
	                       (int)dentry->d_name.len, dentry->d_name.name);


	/* Find our open stateid */
	cred = rpcauth_lookupcred(NFS_SERVER(inode)->client->cl_auth, 0);
	if (IS_ERR(cred))
		return PTR_ERR(cred);
	ctx = alloc_nfs_open_context(dentry, cred);
	put_rpccred(cred);
	if (unlikely(ctx == NULL))
		return -ENOMEM;
	status = -EIO; /* ERACE actually */
	state = nfs4_find_state(inode, cred, filp->f_mode);
	if (unlikely(state == NULL))
		goto no_state;
	ctx->state = state;
	nfs4_close_state(state, filp->f_mode);
	ctx->mode = filp->f_mode;
	nfs_file_set_open_context(filp, ctx);
	put_nfs_open_context(ctx);
	if (filp->f_mode & FMODE_WRITE)
		nfs_begin_data_update(inode);
	return 0;
no_state:
	printk(KERN_WARNING "NFS: v4 raced in function %s\n", __FUNCTION__);
	put_nfs_open_context(ctx);
	return status;
}

/*
 * Release our state
 */
static int
nfs4_proc_file_release(struct inode *inode, struct file *filp)
{
	if (filp->f_mode & FMODE_WRITE)
		nfs_end_data_update(inode);
	nfs_file_clear_open_context(filp);
	return 0;
}

static int
nfs4_async_handle_error(struct rpc_task *task, struct nfs_server *server)
{
	struct nfs4_client *clp = server->nfs4_state;

	if (!clp || task->tk_status >= 0)
		return 0;
	switch(task->tk_status) {
		case -NFS4ERR_STALE_CLIENTID:
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_EXPIRED:
			rpc_sleep_on(&clp->cl_rpcwaitq, task, NULL, NULL);
			nfs4_schedule_state_recovery(clp);
			if (test_bit(NFS4CLNT_OK, &clp->cl_state))
				rpc_wake_up_task(task);
			task->tk_status = 0;
			return -EAGAIN;
		case -NFS4ERR_GRACE:
		case -NFS4ERR_DELAY:
			rpc_delay(task, NFS4_POLL_RETRY_MAX);
			task->tk_status = 0;
			return -EAGAIN;
		case -NFS4ERR_OLD_STATEID:
			task->tk_status = 0;
			return -EAGAIN;
	}
	task->tk_status = nfs4_map_errors(task->tk_status);
	return 0;
}

static int nfs4_wait_clnt_recover(struct rpc_clnt *clnt, struct nfs4_client *clp)
{
	DEFINE_WAIT(wait);
	sigset_t oldset;
	int interruptible, res = 0;

	might_sleep();

	rpc_clnt_sigmask(clnt, &oldset);
	interruptible = TASK_UNINTERRUPTIBLE;
	if (clnt->cl_intr)
		interruptible = TASK_INTERRUPTIBLE;
	prepare_to_wait(&clp->cl_waitq, &wait, interruptible);
	nfs4_schedule_state_recovery(clp);
	if (clnt->cl_intr && signalled())
		res = -ERESTARTSYS;
	else if (!test_bit(NFS4CLNT_OK, &clp->cl_state))
		schedule();
	finish_wait(&clp->cl_waitq, &wait);
	rpc_clnt_sigunmask(clnt, &oldset);
	return res;
}

static int nfs4_delay(struct rpc_clnt *clnt, long *timeout)
{
	sigset_t oldset;
	int res = 0;

	might_sleep();

	if (*timeout <= 0)
		*timeout = NFS4_POLL_RETRY_MIN;
	if (*timeout > NFS4_POLL_RETRY_MAX)
		*timeout = NFS4_POLL_RETRY_MAX;
	rpc_clnt_sigmask(clnt, &oldset);
	if (clnt->cl_intr) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(*timeout);
		if (signalled())
			res = -ERESTARTSYS;
	} else {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(*timeout);
	}
	rpc_clnt_sigunmask(clnt, &oldset);
	*timeout <<= 1;
	return res;
}

/* This is the error handling routine for processes that are allowed
 * to sleep.
 */
int nfs4_handle_exception(struct nfs_server *server, int errorcode, struct nfs4_exception *exception)
{
	struct nfs4_client *clp = server->nfs4_state;
	int ret = errorcode;

	exception->retry = 0;
	switch(errorcode) {
		case 0:
			return 0;
		case -NFS4ERR_STALE_CLIENTID:
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_EXPIRED:
			ret = nfs4_wait_clnt_recover(server->client, clp);
			if (ret == 0)
				exception->retry = 1;
			break;
		case -NFS4ERR_GRACE:
		case -NFS4ERR_DELAY:
			ret = nfs4_delay(server->client, &exception->timeout);
			if (ret == 0)
				exception->retry = 1;
			break;
		case -NFS4ERR_OLD_STATEID:
			if (ret == 0)
				exception->retry = 1;
	}
	/* We failed to handle the error */
	return nfs4_map_errors(ret);
}

int nfs4_proc_setclientid(struct nfs4_client *clp, u32 program, unsigned short port)
{
	nfs4_verifier sc_verifier;
	struct nfs4_setclientid setclientid = {
		.sc_verifier = &sc_verifier,
		.sc_prog = program,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SETCLIENTID],
		.rpc_argp = &setclientid,
		.rpc_resp = clp,
		.rpc_cred = clp->cl_cred,
	};
	u32 *p;
	int loop = 0;
	int status;

	p = (u32*)sc_verifier.data;
	*p++ = htonl((u32)clp->cl_boot_time.tv_sec);
	*p = htonl((u32)clp->cl_boot_time.tv_nsec);

	for(;;) {
		setclientid.sc_name_len = scnprintf(setclientid.sc_name,
				sizeof(setclientid.sc_name), "%s/%u.%u.%u.%u %s %u",
				clp->cl_ipaddr, NIPQUAD(clp->cl_addr.s_addr),
				clp->cl_cred->cr_ops->cr_name,
				clp->cl_id_uniquifier);
		setclientid.sc_netid_len = scnprintf(setclientid.sc_netid,
				sizeof(setclientid.sc_netid), "tcp");
		setclientid.sc_uaddr_len = scnprintf(setclientid.sc_uaddr,
				sizeof(setclientid.sc_uaddr), "%s.%d.%d",
				clp->cl_ipaddr, port >> 8, port & 255);

		status = rpc_call_sync(clp->cl_rpcclient, &msg, 0);
		if (status != -NFS4ERR_CLID_INUSE)
			break;
		if (signalled())
			break;
		if (loop++ & 1)
			ssleep(clp->cl_lease_time + 1);
		else
			if (++clp->cl_id_uniquifier == 0)
				break;
	}
	return status;
}

int
nfs4_proc_setclientid_confirm(struct nfs4_client *clp)
{
	struct nfs_fsinfo fsinfo;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SETCLIENTID_CONFIRM],
		.rpc_argp = clp,
		.rpc_resp = &fsinfo,
		.rpc_cred = clp->cl_cred,
	};
	unsigned long now;
	int status;

	now = jiffies;
	status = rpc_call_sync(clp->cl_rpcclient, &msg, 0);
	if (status == 0) {
		spin_lock(&clp->cl_lock);
		clp->cl_lease_time = fsinfo.lease_time * HZ;
		clp->cl_last_renewal = now;
		spin_unlock(&clp->cl_lock);
	}
	return status;
}

static int _nfs4_proc_delegreturn(struct inode *inode, struct rpc_cred *cred, const nfs4_stateid *stateid)
{
	struct nfs4_delegreturnargs args = {
		.fhandle = NFS_FH(inode),
		.stateid = stateid,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_DELEGRETURN],
		.rpc_argp = &args,
		.rpc_cred = cred,
	};

	return rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
}

int nfs4_proc_delegreturn(struct inode *inode, struct rpc_cred *cred, const nfs4_stateid *stateid)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_exception exception = { };
	int err;
	do {
		err = _nfs4_proc_delegreturn(inode, cred, stateid);
		switch (err) {
			case -NFS4ERR_STALE_STATEID:
			case -NFS4ERR_EXPIRED:
				nfs4_schedule_state_recovery(server->nfs4_state);
			case 0:
				return 0;
		}
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
	return err;
}

#define NFS4_LOCK_MINTIMEOUT (1 * HZ)
#define NFS4_LOCK_MAXTIMEOUT (30 * HZ)

/* 
 * sleep, with exponential backoff, and retry the LOCK operation. 
 */
static unsigned long
nfs4_set_lock_task_retry(unsigned long timeout)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(timeout);
	timeout <<= 1;
	if (timeout > NFS4_LOCK_MAXTIMEOUT)
		return NFS4_LOCK_MAXTIMEOUT;
	return timeout;
}

static inline int
nfs4_lck_type(int cmd, struct file_lock *request)
{
	/* set lock type */
	switch (request->fl_type) {
		case F_RDLCK:
			return IS_SETLKW(cmd) ? NFS4_READW_LT : NFS4_READ_LT;
		case F_WRLCK:
			return IS_SETLKW(cmd) ? NFS4_WRITEW_LT : NFS4_WRITE_LT;
		case F_UNLCK:
			return NFS4_WRITE_LT; 
	}
	BUG();
	return 0;
}

static inline uint64_t
nfs4_lck_length(struct file_lock *request)
{
	if (request->fl_end == OFFSET_MAX)
		return ~(uint64_t)0;
	return request->fl_end - request->fl_start + 1;
}

static int _nfs4_proc_getlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct inode *inode = state->inode;
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_client *clp = server->nfs4_state;
	struct nfs_lockargs arg = {
		.fh = NFS_FH(inode),
		.type = nfs4_lck_type(cmd, request),
		.offset = request->fl_start,
		.length = nfs4_lck_length(request),
	};
	struct nfs_lockres res = {
		.server = server,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_LOCKT],
		.rpc_argp       = &arg,
		.rpc_resp       = &res,
		.rpc_cred	= state->owner->so_cred,
	};
	struct nfs_lowner nlo;
	struct nfs4_lock_state *lsp;
	int status;

	down_read(&clp->cl_sem);
	nlo.clientid = clp->cl_clientid;
	down(&state->lock_sema);
	lsp = nfs4_find_lock_state(state, request->fl_owner);
	if (lsp)
		nlo.id = lsp->ls_id; 
	else {
		spin_lock(&clp->cl_lock);
		nlo.id = nfs4_alloc_lockowner_id(clp);
		spin_unlock(&clp->cl_lock);
	}
	arg.u.lockt = &nlo;
	status = rpc_call_sync(server->client, &msg, 0);
	if (!status) {
		request->fl_type = F_UNLCK;
	} else if (status == -NFS4ERR_DENIED) {
		int64_t len, start, end;
		start = res.u.denied.offset;
		len = res.u.denied.length;
		end = start + len - 1;
		if (end < 0 || len == 0)
			request->fl_end = OFFSET_MAX;
		else
			request->fl_end = (loff_t)end;
		request->fl_start = (loff_t)start;
		request->fl_type = F_WRLCK;
		if (res.u.denied.type & 1)
			request->fl_type = F_RDLCK;
		request->fl_pid = 0;
		status = 0;
	}
	if (lsp)
		nfs4_put_lock_state(lsp);
	up(&state->lock_sema);
	up_read(&clp->cl_sem);
	return status;
}

static int nfs4_proc_getlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct nfs4_exception exception = { };
	int err;

	do {
		err = nfs4_handle_exception(NFS_SERVER(state->inode),
				_nfs4_proc_getlk(state, cmd, request),
				&exception);
	} while (exception.retry);
	return err;
}

static int do_vfs_lock(struct file *file, struct file_lock *fl)
{
	int res = 0;
	switch (fl->fl_flags & (FL_POSIX|FL_FLOCK)) {
		case FL_POSIX:
			res = posix_lock_file_wait(file, fl);
			break;
		case FL_FLOCK:
			res = flock_lock_file_wait(file, fl);
			break;
		default:
			BUG();
	}
	if (res < 0)
		printk(KERN_WARNING "%s: VFS is out of sync with lock manager!\n",
				__FUNCTION__);
	return res;
}

static int _nfs4_proc_unlck(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct inode *inode = state->inode;
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_client *clp = server->nfs4_state;
	struct nfs_lockargs arg = {
		.fh = NFS_FH(inode),
		.type = nfs4_lck_type(cmd, request),
		.offset = request->fl_start,
		.length = nfs4_lck_length(request),
	};
	struct nfs_lockres res = {
		.server = server,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_LOCKU],
		.rpc_argp       = &arg,
		.rpc_resp       = &res,
		.rpc_cred	= state->owner->so_cred,
	};
	struct nfs4_lock_state *lsp;
	struct nfs_locku_opargs luargs;
	int status = 0;
			
	down_read(&clp->cl_sem);
	down(&state->lock_sema);
	lsp = nfs4_find_lock_state(state, request->fl_owner);
	if (!lsp)
		goto out;
	/* We might have lost the locks! */
	if ((lsp->ls_flags & NFS_LOCK_INITIALIZED) != 0) {
		luargs.seqid = lsp->ls_seqid;
		memcpy(&luargs.stateid, &lsp->ls_stateid, sizeof(luargs.stateid));
		arg.u.locku = &luargs;
		status = rpc_call_sync(server->client, &msg, RPC_TASK_NOINTR);
		nfs4_increment_lock_seqid(status, lsp);
	}

	if (status == 0) {
		memcpy(&lsp->ls_stateid,  &res.u.stateid, 
				sizeof(lsp->ls_stateid));
		nfs4_notify_unlck(state, request, lsp);
	}
	nfs4_put_lock_state(lsp);
out:
	up(&state->lock_sema);
	if (status == 0)
		do_vfs_lock(request->fl_file, request);
	up_read(&clp->cl_sem);
	return status;
}

static int nfs4_proc_unlck(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct nfs4_exception exception = { };
	int err;

	do {
		err = nfs4_handle_exception(NFS_SERVER(state->inode),
				_nfs4_proc_unlck(state, cmd, request),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_do_setlk(struct nfs4_state *state, int cmd, struct file_lock *request, int reclaim)
{
	struct inode *inode = state->inode;
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_lock_state *lsp;
	struct nfs_lockargs arg = {
		.fh = NFS_FH(inode),
		.type = nfs4_lck_type(cmd, request),
		.offset = request->fl_start,
		.length = nfs4_lck_length(request),
	};
	struct nfs_lockres res = {
		.server = server,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_LOCK],
		.rpc_argp       = &arg,
		.rpc_resp       = &res,
		.rpc_cred	= state->owner->so_cred,
	};
	struct nfs_lock_opargs largs = {
		.reclaim = reclaim,
		.new_lock_owner = 0,
	};
	int status;

	lsp = nfs4_get_lock_state(state, request->fl_owner);
	if (lsp == NULL)
		return -ENOMEM;
	if (!(lsp->ls_flags & NFS_LOCK_INITIALIZED)) {
		struct nfs4_state_owner *owner = state->owner;
		struct nfs_open_to_lock otl = {
			.lock_owner = {
				.clientid = server->nfs4_state->cl_clientid,
			},
		};

		otl.lock_seqid = lsp->ls_seqid;
		otl.lock_owner.id = lsp->ls_id;
		memcpy(&otl.open_stateid, &state->stateid, sizeof(otl.open_stateid));
		largs.u.open_lock = &otl;
		largs.new_lock_owner = 1;
		arg.u.lock = &largs;
		down(&owner->so_sema);
		otl.open_seqid = owner->so_seqid;
		status = rpc_call_sync(server->client, &msg, RPC_TASK_NOINTR);
		/* increment open_owner seqid on success, and 
		* seqid mutating errors */
		nfs4_increment_seqid(status, owner);
		up(&owner->so_sema);
	} else {
		struct nfs_exist_lock el = {
			.seqid = lsp->ls_seqid,
		};
		memcpy(&el.stateid, &lsp->ls_stateid, sizeof(el.stateid));
		largs.u.exist_lock = &el;
		largs.new_lock_owner = 0;
		arg.u.lock = &largs;
		status = rpc_call_sync(server->client, &msg, RPC_TASK_NOINTR);
	}
	/* increment seqid on success, and * seqid mutating errors*/
	nfs4_increment_lock_seqid(status, lsp);
	/* save the returned stateid. */
	if (status == 0) {
		memcpy(&lsp->ls_stateid, &res.u.stateid, sizeof(nfs4_stateid));
		lsp->ls_flags |= NFS_LOCK_INITIALIZED;
		if (!reclaim)
			nfs4_notify_setlk(state, request, lsp);
	} else if (status == -NFS4ERR_DENIED)
		status = -EAGAIN;
	nfs4_put_lock_state(lsp);
	return status;
}

static int nfs4_lock_reclaim(struct nfs4_state *state, struct file_lock *request)
{
	return _nfs4_do_setlk(state, F_SETLK, request, 1);
}

static int nfs4_lock_expired(struct nfs4_state *state, struct file_lock *request)
{
	return _nfs4_do_setlk(state, F_SETLK, request, 0);
}

static int _nfs4_proc_setlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct nfs4_client *clp = state->owner->so_client;
	int status;

	down_read(&clp->cl_sem);
	down(&state->lock_sema);
	status = _nfs4_do_setlk(state, cmd, request, 0);
	up(&state->lock_sema);
	if (status == 0) {
		/* Note: we always want to sleep here! */
		request->fl_flags |= FL_SLEEP;
		if (do_vfs_lock(request->fl_file, request) < 0)
			printk(KERN_WARNING "%s: VFS is out of sync with lock manager!\n", __FUNCTION__);
	}
	up_read(&clp->cl_sem);
	return status;
}

static int nfs4_proc_setlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct nfs4_exception exception = { };
	int err;

	do {
		err = nfs4_handle_exception(NFS_SERVER(state->inode),
				_nfs4_proc_setlk(state, cmd, request),
				&exception);
	} while (exception.retry);
	return err;
}

static int
nfs4_proc_lock(struct file *filp, int cmd, struct file_lock *request)
{
	struct nfs_open_context *ctx;
	struct nfs4_state *state;
	unsigned long timeout = NFS4_LOCK_MINTIMEOUT;
	int status;

	/* verify open state */
	ctx = (struct nfs_open_context *)filp->private_data;
	state = ctx->state;

	if (request->fl_start < 0 || request->fl_end < 0)
		return -EINVAL;

	if (IS_GETLK(cmd))
		return nfs4_proc_getlk(state, F_GETLK, request);

	if (!(IS_SETLK(cmd) || IS_SETLKW(cmd)))
		return -EINVAL;

	if (request->fl_type == F_UNLCK)
		return nfs4_proc_unlck(state, cmd, request);

	do {
		status = nfs4_proc_setlk(state, cmd, request);
		if ((status != -EAGAIN) || IS_SETLK(cmd))
			break;
		timeout = nfs4_set_lock_task_retry(timeout);
		status = -ERESTARTSYS;
		if (signalled())
			break;
	} while(status < 0);

	return status;
}

struct nfs4_state_recovery_ops nfs4_reboot_recovery_ops = {
	.recover_open	= nfs4_open_reclaim,
	.recover_lock	= nfs4_lock_reclaim,
};

struct nfs4_state_recovery_ops nfs4_network_partition_recovery_ops = {
	.recover_open	= nfs4_open_expired,
	.recover_lock	= nfs4_lock_expired,
};

struct nfs_rpc_ops	nfs_v4_clientops = {
	.version	= 4,			/* protocol version */
	.dentry_ops	= &nfs4_dentry_operations,
	.dir_inode_ops	= &nfs4_dir_inode_operations,
	.getroot	= nfs4_proc_get_root,
	.getattr	= nfs4_proc_getattr,
	.setattr	= nfs4_proc_setattr,
	.lookup		= nfs4_proc_lookup,
	.access		= nfs4_proc_access,
	.readlink	= nfs4_proc_readlink,
	.read		= nfs4_proc_read,
	.write		= nfs4_proc_write,
	.commit		= nfs4_proc_commit,
	.create		= nfs4_proc_create,
	.remove		= nfs4_proc_remove,
	.unlink_setup	= nfs4_proc_unlink_setup,
	.unlink_done	= nfs4_proc_unlink_done,
	.rename		= nfs4_proc_rename,
	.link		= nfs4_proc_link,
	.symlink	= nfs4_proc_symlink,
	.mkdir		= nfs4_proc_mkdir,
	.rmdir		= nfs4_proc_remove,
	.readdir	= nfs4_proc_readdir,
	.mknod		= nfs4_proc_mknod,
	.statfs		= nfs4_proc_statfs,
	.fsinfo		= nfs4_proc_fsinfo,
	.pathconf	= nfs4_proc_pathconf,
	.decode_dirent	= nfs4_decode_dirent,
	.read_setup	= nfs4_proc_read_setup,
	.write_setup	= nfs4_proc_write_setup,
	.commit_setup	= nfs4_proc_commit_setup,
	.file_open      = nfs4_proc_file_open,
	.file_release   = nfs4_proc_file_release,
	.lock		= nfs4_proc_lock,
};

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */

/*
 * linux/net/sunrpc/auth_gss.c
 *
 * RPCSEC_GSS client authentication.
 * 
 *  Copyright (c) 2000 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Dug Song       <dugsong@monkey.org>
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
 *
 * $Id$
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/sched.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/auth_gss.h>
#include <linux/sunrpc/gss_err.h>
#include <linux/sunrpc/rpc_pipe_fs.h>
#include <asm/uaccess.h>

static struct rpc_authops authgss_ops;

static struct rpc_credops gss_credops;

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

#define NFS_NGROUPS	16

#define GSS_CRED_EXPIRE		(60 * HZ)	/* XXX: reasonable? */
#define GSS_CRED_SLACK		1024		/* XXX: unused */
#define GSS_VERF_SLACK		48		/* length of a krb5 verifier.*/

/* XXX this define must match the gssd define
* as it is passed to gssd to signal the use of
* machine creds should be part of the shared rpc interface */

#define CA_RUN_AS_MACHINE  0x00000200 

/* dump the buffer in `emacs-hexl' style */
#define isprint(c)      ((c > 0x1f) && (c < 0x7f))

static rwlock_t gss_ctx_lock = RW_LOCK_UNLOCKED;

struct gss_auth {
	struct rpc_auth rpc_auth;
	struct gss_api_mech *mech;
	struct list_head upcalls;
	struct dentry *dentry;
	char path[48];
	spinlock_t lock;
};

static void gss_destroy_ctx(struct gss_cl_ctx *);
static struct rpc_pipe_ops gss_upcall_ops;

void
print_hexl(u32 *p, u_int length, u_int offset)
{
	u_int i, j, jm;
	u8 c, *cp;
	
	dprintk("RPC: print_hexl: length %d\n",length);
	dprintk("\n");
	cp = (u8 *) p;
	
	for (i = 0; i < length; i += 0x10) {
		dprintk("  %04x: ", (u_int)(i + offset));
		jm = length - i;
		jm = jm > 16 ? 16 : jm;
		
		for (j = 0; j < jm; j++) {
			if ((j % 2) == 1)
				dprintk("%02x ", (u_int)cp[i+j]);
			else
				dprintk("%02x", (u_int)cp[i+j]);
		}
		for (; j < 16; j++) {
			if ((j % 2) == 1)
				dprintk("   ");
			else
				dprintk("  ");
		}
		dprintk(" ");
		
		for (j = 0; j < jm; j++) {
			c = cp[i+j];
			c = isprint(c) ? c : '.';
			dprintk("%c", c);
		}
		dprintk("\n");
	}
}

static inline struct gss_cl_ctx *
gss_get_ctx(struct gss_cl_ctx *ctx)
{
	atomic_inc(&ctx->count);
	return ctx;
}

static inline void
gss_put_ctx(struct gss_cl_ctx *ctx)
{
	if (atomic_dec_and_test(&ctx->count))
		gss_destroy_ctx(ctx);
}

static void
gss_cred_set_ctx(struct rpc_cred *cred, struct gss_cl_ctx *ctx)
{
	struct gss_cred *gss_cred = container_of(cred, struct gss_cred, gc_base);
	struct gss_cl_ctx *old;
	write_lock(&gss_ctx_lock);
	old = gss_cred->gc_ctx;
	gss_cred->gc_ctx = ctx;
	cred->cr_flags |= RPCAUTH_CRED_UPTODATE;
	write_unlock(&gss_ctx_lock);
	if (old)
		gss_put_ctx(old);
}

static struct gss_cl_ctx *
gss_cred_get_uptodate_ctx(struct rpc_cred *cred)
{
	struct gss_cred *gss_cred = container_of(cred, struct gss_cred, gc_base);
	struct gss_cl_ctx *ctx = NULL;

	read_lock(&gss_ctx_lock);
	if ((cred->cr_flags & RPCAUTH_CRED_UPTODATE) && gss_cred->gc_ctx)
		ctx = gss_get_ctx(gss_cred->gc_ctx);
	read_unlock(&gss_ctx_lock);
	return ctx;
}

static inline int
simple_get_bytes(char **ptr, const char *end, void *res, int len)
{
	char *p, *q;
	p = *ptr;
	q = p + len;
	if (q > end || q < p)
		return -1;
	memcpy(res, p, len);
	*ptr = q;
	return 0;
}

static inline int
simple_get_netobj(char **ptr, const char *end, struct xdr_netobj *res)
{
	char *p, *q;
	p = *ptr;
	if (simple_get_bytes(&p, end, &res->len, sizeof(res->len)))
		return -1;
	q = p + res->len;
	if (q > end || q < p)
		return -1;
	res->data = p;
	*ptr = q;
	return 0;
}

static int
dup_netobj(struct xdr_netobj *source, struct xdr_netobj *dest)
{
	dest->len = source->len;
	if (!(dest->data = kmalloc(dest->len, GFP_KERNEL)))
		return -1;
	memcpy(dest->data, source->data, dest->len);
	return 0;
}

static struct gss_cl_ctx *
gss_cred_get_ctx(struct rpc_cred *cred)
{
	struct gss_cred *gss_cred = container_of(cred, struct gss_cred, gc_base);
	struct gss_cl_ctx *ctx = NULL;

	read_lock(&gss_ctx_lock);
	if (gss_cred->gc_ctx)
		ctx = gss_get_ctx(gss_cred->gc_ctx);
	read_unlock(&gss_ctx_lock);
	return ctx;
}

static int
gss_parse_init_downcall(struct gss_api_mech *gm, struct xdr_netobj *buf,
		struct gss_cl_ctx **gc, uid_t *uid, int *gss_err)
{
	char *end = buf->data + buf->len;
	char *p = buf->data;
	struct gss_cl_ctx *ctx;
	struct xdr_netobj tmp_buf;
	unsigned int timeout;
	int err = -EIO;

	if (!(ctx = kmalloc(sizeof(*ctx), GFP_KERNEL))) {
		err = -ENOMEM;
		goto err;
	}
	ctx->gc_proc = RPC_GSS_PROC_DATA;
	ctx->gc_seq = 1;	/* NetApp 6.4R1 doesn't accept seq. no. 0 */
	spin_lock_init(&ctx->gc_seq_lock);
	atomic_set(&ctx->count,1);

	if (simple_get_bytes(&p, end, uid, sizeof(uid)))
		goto err_free_ctx;
	/* FIXME: discarded timeout for now */
	if (simple_get_bytes(&p, end, &timeout, sizeof(timeout)))
		goto err_free_ctx;
	*gss_err = 0;
	if (simple_get_bytes(&p, end, &ctx->gc_win, sizeof(ctx->gc_win)))
		goto err_free_ctx;
	/* gssd signals an error by passing ctx->gc_win = 0: */
	if (!ctx->gc_win) {
		/* in which case the next int is an error code: */
		if (simple_get_bytes(&p, end, gss_err, sizeof(*gss_err)))
			goto err_free_ctx;
		err = 0;
		goto err_free_ctx;
	}
	if (simple_get_netobj(&p, end, &tmp_buf))
		goto err_free_ctx;
	if (dup_netobj(&tmp_buf, &ctx->gc_wire_ctx)) {
		err = -ENOMEM;
		goto err_free_ctx;
	}
	if (simple_get_netobj(&p, end, &tmp_buf))
		goto err_free_wire_ctx;
	if (p != end)
		goto err_free_wire_ctx;
	if (gss_import_sec_context(&tmp_buf, gm, &ctx->gc_gss_ctx))
		goto err_free_wire_ctx;
	*gc = ctx;
	return 0;
err_free_wire_ctx:
	kfree(ctx->gc_wire_ctx.data);
err_free_ctx:
	kfree(ctx);
err:
	*gc = NULL;
	dprintk("RPC: gss_parse_init_downcall returning %d\n", err);
	return err;
}


struct gss_upcall_msg {
	struct rpc_pipe_msg msg;
	struct list_head list;
	struct gss_auth *auth;
	struct rpc_wait_queue waitq;
	uid_t	uid;
	atomic_t count;
};

static void
gss_release_msg(struct gss_upcall_msg *gss_msg)
{
	struct gss_auth *gss_auth = gss_msg->auth;

	if (!atomic_dec_and_lock(&gss_msg->count, &gss_auth->lock))
		return;
	if (!list_empty(&gss_msg->list))
		list_del(&gss_msg->list);
	spin_unlock(&gss_auth->lock);
	kfree(gss_msg);
}

static struct gss_upcall_msg *
__gss_find_upcall(struct gss_auth *gss_auth, uid_t uid)
{
	struct gss_upcall_msg *pos;
	list_for_each_entry(pos, &gss_auth->upcalls, list) {
		if (pos->uid != uid)
			continue;
		atomic_inc(&pos->count);
		return pos;
	}
	return NULL;
}

static struct gss_upcall_msg *
gss_find_upcall(struct gss_auth *gss_auth, uid_t uid)
{
	struct gss_upcall_msg *gss_msg;

	spin_lock(&gss_auth->lock);
	gss_msg = __gss_find_upcall(gss_auth, uid);
	spin_unlock(&gss_auth->lock);
	return gss_msg;
}

static void
__gss_unhash_msg(struct gss_upcall_msg *gss_msg)
{
	if (list_empty(&gss_msg->list))
		return;
	list_del_init(&gss_msg->list);
	rpc_wake_up(&gss_msg->waitq);
}

static void
gss_unhash_msg(struct gss_upcall_msg *gss_msg)
{
	struct gss_auth *gss_auth = gss_msg->auth;

	spin_lock(&gss_auth->lock);
	__gss_unhash_msg(gss_msg);
	spin_unlock(&gss_auth->lock);
}

static void
gss_release_callback(struct rpc_task *task)
{
	struct rpc_clnt *clnt = task->tk_client;
	struct gss_auth *gss_auth = container_of(clnt->cl_auth,
			struct gss_auth, rpc_auth);
	struct gss_upcall_msg *gss_msg;

	gss_msg = gss_find_upcall(gss_auth, task->tk_msg.rpc_cred->cr_uid);
	BUG_ON(!gss_msg);
	atomic_dec(&gss_msg->count);
	gss_release_msg(gss_msg);
}

static int
gss_upcall(struct rpc_clnt *clnt, struct rpc_task *task, uid_t uid)
{
	struct gss_auth *gss_auth = container_of(clnt->cl_auth,
			struct gss_auth, rpc_auth);
	struct gss_upcall_msg *gss_msg, *gss_new = NULL;
	struct rpc_pipe_msg *msg;
	struct dentry *dentry = gss_auth->dentry;
	int res;

retry:
	gss_msg = __gss_find_upcall(gss_auth, uid);
	if (gss_msg)
		goto out_sleep;
	if (gss_new == NULL) {
		spin_unlock(&gss_auth->lock);
		gss_new = kmalloc(sizeof(*gss_new), GFP_KERNEL);
		if (gss_new)
			return -ENOMEM;
		spin_lock(&gss_auth->lock);
		goto retry;
	}
	gss_msg = gss_new;
	memset(gss_new, 0, sizeof(*gss_new));
	INIT_LIST_HEAD(&gss_new->list);
	INIT_RPC_WAITQ(&gss_new->waitq, "RPCSEC_GSS upcall waitq");
	atomic_set(&gss_new->count, 2);
	msg = &gss_new->msg;
	msg->data = &gss_new->uid;
	msg->len = sizeof(gss_new->uid);
	gss_new->uid = uid;
	gss_new->auth = gss_auth;
	list_add(&gss_new->list, &gss_auth->upcalls);
	gss_new = NULL;
	task->tk_timeout = 5 * HZ;
	rpc_sleep_on(&gss_msg->waitq, task, gss_release_callback, NULL);
	spin_unlock(&gss_auth->lock);
	res = rpc_queue_upcall(dentry->d_inode, msg);
	if (res) {
		gss_unhash_msg(gss_msg);
		gss_release_msg(gss_msg);
	}
	return res;
out_sleep:
	rpc_sleep_on(&gss_msg->waitq, task, gss_release_callback, NULL);
	spin_unlock(&gss_auth->lock);
	if (gss_new)
		kfree(gss_new);
	return 0;
}

static ssize_t
gss_pipe_upcall(struct file *filp, struct rpc_pipe_msg *msg,
		char *dst, size_t buflen)
{
	char *data = (char *)msg->data + msg->copied;
	ssize_t mlen = msg->len;
	ssize_t left;

	if (mlen > buflen)
		mlen = buflen;
	left = copy_to_user(dst, data, mlen);
	if (left < 0) {
		msg->errno = left;
		return left;
	}
	mlen -= left;
	msg->copied += mlen;
	msg->errno = 0;
	return mlen;
}

static ssize_t
gss_pipe_downcall(struct file *filp, const char *src, size_t mlen)
{
	char buf[1024];
	struct xdr_netobj obj = {
		.len	= mlen,
		.data	= buf,
	};
	struct inode *inode = filp->f_dentry->d_inode;
	struct rpc_inode *rpci = RPC_I(inode);
	struct rpc_clnt *clnt;
	struct rpc_auth *auth;
	struct gss_auth *gss_auth;
	struct gss_api_mech *mech;
	struct auth_cred acred = { 0 };
	struct rpc_cred *cred;
	struct gss_upcall_msg *gss_msg;
	struct gss_cl_ctx *ctx = NULL;
	ssize_t left;
	int err;
	int gss_err;

	if (mlen > sizeof(buf))
		return -ENOSPC;
	left = copy_from_user(buf, src, mlen);
	if (left)
		return -EFAULT;
	clnt = rpci->private;
	atomic_inc(&clnt->cl_users);
	auth = clnt->cl_auth;
	gss_auth = container_of(auth, struct gss_auth, rpc_auth);
	mech = gss_auth->mech;
	err = gss_parse_init_downcall(mech, &obj, &ctx, &acred.uid, &gss_err);
	if (err)
		goto err;
	cred = rpcauth_lookup_credcache(auth, &acred, 0);
	if (!cred)
		goto err;
	if (gss_err)
		cred->cr_flags |= RPCAUTH_CRED_DEAD;
	else
		gss_cred_set_ctx(cred, ctx);
	spin_lock(&gss_auth->lock);
	gss_msg = __gss_find_upcall(gss_auth, acred.uid);
	if (gss_msg) {
		__gss_unhash_msg(gss_msg);
		spin_unlock(&gss_auth->lock);
		gss_release_msg(gss_msg);
	} else
		spin_unlock(&gss_auth->lock);
	rpc_release_client(clnt);
	return mlen;
err:
	if (ctx)
		gss_destroy_ctx(ctx);
	rpc_release_client(clnt);
	dprintk("RPC: gss_pipe_downcall returning %d\n", err);
	return err;
}

void
gss_pipe_destroy_msg(struct rpc_pipe_msg *msg)
{
	struct gss_upcall_msg *gss_msg = container_of(msg, struct gss_upcall_msg, msg);

	if (msg->errno < 0)
		gss_unhash_msg(gss_msg);
	gss_release_msg(gss_msg);
}

/* 
 * NOTE: we have the opportunity to use different 
 * parameters based on the input flavor (which must be a pseudoflavor)
 */
static struct rpc_auth *
gss_create(struct rpc_clnt *clnt, rpc_authflavor_t flavor)
{
	struct gss_auth *gss_auth;
	struct rpc_auth * auth;

	dprintk("RPC: creating GSS authenticator for client %p\n",clnt);
	if (!(gss_auth = kmalloc(sizeof(*gss_auth), GFP_KERNEL)))
		goto out_dec;
	gss_auth->mech = gss_pseudoflavor_to_mech(flavor);
	if (!gss_auth->mech) {
		printk(KERN_WARNING "%s: Pseudoflavor %d not found!",
				__FUNCTION__, flavor);
		goto err_free;
	}
	INIT_LIST_HEAD(&gss_auth->upcalls);
	spin_lock_init(&gss_auth->lock);
	auth = &gss_auth->rpc_auth;
	auth->au_cslack = GSS_CRED_SLACK >> 2;
	auth->au_rslack = GSS_VERF_SLACK >> 2;
	auth->au_expire = GSS_CRED_EXPIRE;
	auth->au_ops = &authgss_ops;
	auth->au_flavor = flavor;

	rpcauth_init_credcache(auth);

	snprintf(gss_auth->path, sizeof(gss_auth->path), "%s/%s",
			clnt->cl_pathname,
			gss_auth->mech->gm_ops->name);
	gss_auth->dentry = rpc_mkpipe(gss_auth->path, clnt, &gss_upcall_ops, RPC_PIPE_WAIT_FOR_OPEN);
	if (IS_ERR(gss_auth->dentry))
		goto err_free;

	return auth;
err_free:
	kfree(gss_auth);
out_dec:
	return NULL;
}

static void
gss_destroy(struct rpc_auth *auth)
{
	struct gss_auth *gss_auth;
	dprintk("RPC: destroying GSS authenticator %p flavor %d\n",
		auth, auth->au_flavor);

	gss_auth = container_of(auth, struct gss_auth, rpc_auth);
	rpc_unlink(gss_auth->path);

	rpcauth_free_credcache(auth);
}

/* gss_destroy_cred (and gss_destroy_ctx) are used to clean up after failure
 * to create a new cred or context, so they check that things have been
 * allocated before freeing them. */
static void
gss_destroy_ctx(struct gss_cl_ctx *ctx)
{

	dprintk("RPC: gss_destroy_ctx\n");

	if (ctx->gc_gss_ctx)
		gss_delete_sec_context(&ctx->gc_gss_ctx);

	if (ctx->gc_wire_ctx.len > 0) {
		kfree(ctx->gc_wire_ctx.data);
		ctx->gc_wire_ctx.len = 0;
	}

	kfree(ctx);

}

static void
gss_destroy_cred(struct rpc_cred *rc)
{
	struct gss_cred *cred = (struct gss_cred *)rc;

	dprintk("RPC: gss_destroy_cred \n");

	if (cred->gc_ctx)
		gss_put_ctx(cred->gc_ctx);
	kfree(cred);
}

static struct rpc_cred *
gss_create_cred(struct rpc_auth *auth, struct auth_cred *acred, int taskflags)
{
	struct gss_cred	*cred = NULL;

	dprintk("RPC: gss_create_cred for uid %d, flavor %d\n",
		acred->uid, auth->au_flavor);

	if (!(cred = kmalloc(sizeof(*cred), GFP_KERNEL)))
		goto out_err;

	memset(cred, 0, sizeof(*cred));
	atomic_set(&cred->gc_count, 0);
	cred->gc_uid = acred->uid;
	/*
	 * Note: in order to force a call to call_refresh(), we deliberately
	 * fail to flag the credential as RPCAUTH_CRED_UPTODATE.
	 */
	cred->gc_flags = 0;
	cred->gc_base.cr_ops = &gss_credops;
	cred->gc_flavor = auth->au_flavor;

	return (struct rpc_cred *) cred;

out_err:
	dprintk("RPC: gss_create_cred failed\n");
	if (cred) gss_destroy_cred((struct rpc_cred *)cred);
	return NULL;
}

static int
gss_match(struct auth_cred *acred, struct rpc_cred *rc, int taskflags)
{
	return (rc->cr_uid == acred->uid);
}

/*
* Marshal credentials.
* Maybe we should keep a cached credential for performance reasons.
*/
static u32 *
gss_marshal(struct rpc_task *task, u32 *p, int ruid)
{
	struct rpc_cred *cred = task->tk_msg.rpc_cred;
	struct gss_cred	*gss_cred = container_of(cred, struct gss_cred,
						 gc_base);
	struct gss_cl_ctx	*ctx = gss_cred_get_ctx(cred);
	u32		*cred_len;
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_clnt *clnt = task->tk_client;
	struct rpc_xprt *xprt = clnt->cl_xprt;
	u32             *verfbase = req->rq_svec[0].iov_base; 
	u32             maj_stat = 0;
	struct xdr_netobj bufin,bufout;
	u32		service;

	dprintk("RPC: gss_marshal\n");

	/* We compute the checksum for the verifier over the xdr-encoded bytes
	 * starting with the xid (which verfbase points to) and ending at
	 * the end of the credential. */
	if (xprt->stream)
		verfbase++; /* See clnt.c:call_header() */

	*p++ = htonl(RPC_AUTH_GSS);
	cred_len = p++;

	service = gss_pseudoflavor_to_service(gss_cred->gc_flavor);
	if (service == 0) {
		dprintk("Bad pseudoflavor %d in gss_marshal\n",
			gss_cred->gc_flavor);
		goto out_put_ctx;
	}
	spin_lock(&ctx->gc_seq_lock);
	task->tk_gss_seqno = ctx->gc_seq++;
	spin_unlock(&ctx->gc_seq_lock);

	*p++ = htonl((u32) RPC_GSS_VERSION);
	*p++ = htonl((u32) ctx->gc_proc);
	*p++ = htonl((u32) task->tk_gss_seqno);
	*p++ = htonl((u32) service);
	p = xdr_encode_netobj(p, &ctx->gc_wire_ctx);
	*cred_len = htonl((p - (cred_len + 1)) << 2);

	/* Marshal verifier. */
	bufin.data = (u8 *)verfbase;
	bufin.len = (p - verfbase) << 2;

	/* set verifier flavor*/
	*p++ = htonl(RPC_AUTH_GSS);

	maj_stat = gss_get_mic(ctx->gc_gss_ctx,
			       GSS_C_QOP_DEFAULT, 
			       &bufin, &bufout);
	if(maj_stat != 0){
		printk("gss_marshal: gss_get_mic FAILED (%d)\n",
		       maj_stat);
		goto out_put_ctx;
	}
	p = xdr_encode_netobj(p, &bufout);
	return p;
out_put_ctx:
	gss_put_ctx(ctx);
	return NULL;
}

/*
* Refresh credentials. XXX - finish
*/
static int
gss_refresh(struct rpc_task *task)
{
	struct rpc_clnt *clnt = task->tk_client;
	struct gss_auth *gss_auth = container_of(clnt->cl_auth,
			struct gss_auth, rpc_auth);
	struct rpc_xprt *xprt = task->tk_xprt;
	struct rpc_cred *cred = task->tk_msg.rpc_cred;
	int err = 0;

	task->tk_timeout = xprt->timeout.to_current;
	spin_lock(&gss_auth->lock);
	if (gss_cred_get_uptodate_ctx(cred))
		goto out;
	err = gss_upcall(clnt, task, cred->cr_uid);
out:
	spin_unlock(&gss_auth->lock);
	return err;
}

static u32 *
gss_validate(struct rpc_task *task, u32 *p)
{
	struct rpc_cred *cred = task->tk_msg.rpc_cred;
	struct gss_cl_ctx *ctx = gss_cred_get_ctx(cred);
	u32		seq, qop_state;
	struct xdr_netobj bufin;
	struct xdr_netobj bufout;
	u32		flav,len;

	dprintk("RPC: gss_validate\n");

	flav = ntohl(*p++);
	if ((len = ntohl(*p++)) > RPC_MAX_AUTH_SIZE) {
                printk("RPC: giant verf size: %ld\n", (unsigned long) len);
                return NULL;
	}
	dprintk("RPC: gss_validate: verifier flavor %d, len %d\n", flav, len);

	if (flav != RPC_AUTH_GSS) {
		printk("RPC: bad verf flavor: %ld\n", (unsigned long)flav);
		return NULL;
	}
	seq = htonl(task->tk_gss_seqno);
	bufin.data = (u8 *) &seq;
	bufin.len = sizeof(seq);
	bufout.data = (u8 *) p;
	bufout.len = len;

	if (gss_verify_mic(ctx->gc_gss_ctx, &bufin, &bufout, &qop_state) != 0)
		return NULL;
	task->tk_auth->au_rslack = XDR_QUADLEN(len) + 2;
	dprintk("RPC: GSS gss_validate: gss_verify_mic succeeded.\n");
	return p + XDR_QUADLEN(len);
}

static struct rpc_authops authgss_ops = {
	.owner		= THIS_MODULE,
	.au_flavor	= RPC_AUTH_GSS,
#ifdef RPC_DEBUG
	.au_name	= "RPCSEC_GSS",
#endif
	.create		= gss_create,
	.destroy	= gss_destroy,
	.crcreate	= gss_create_cred
};

static struct rpc_credops gss_credops = {
	.crdestroy	= gss_destroy_cred,
	.crmatch	= gss_match,
	.crmarshal	= gss_marshal,
	.crrefresh	= gss_refresh,
	.crvalidate	= gss_validate,
};

static struct rpc_pipe_ops gss_upcall_ops = {
	.upcall		= gss_pipe_upcall,
	.downcall	= gss_pipe_downcall,
	.destroy_msg	= gss_pipe_destroy_msg,
};

/*
 * Initialize RPCSEC_GSS module
 */
static int __init init_rpcsec_gss(void)
{
	int err = 0;

	err = rpcauth_register(&authgss_ops);
	return err;
}

static void __exit exit_rpcsec_gss(void)
{
	gss_mech_unregister_all();
	rpcauth_unregister(&authgss_ops);
}

MODULE_LICENSE("GPL");
module_init(init_rpcsec_gss)
module_exit(exit_rpcsec_gss)

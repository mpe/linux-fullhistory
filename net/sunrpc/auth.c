/*
 * linux/fs/nfs/rpcauth.c
 *
 * Generic RPC authentication API.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/sunrpc/clnt.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

#define RPC_MAXFLAVOR	8

static struct rpc_authops *	auth_flavors[RPC_MAXFLAVOR] = {
	&authnull_ops,		/* AUTH_NULL */
	&authunix_ops,		/* AUTH_UNIX */
	NULL,			/* others can be loadable modules */
};

int
rpcauth_register(struct rpc_authops *ops)
{
	unsigned int	flavor;

	if ((flavor = ops->au_flavor) >= RPC_MAXFLAVOR)
		return -EINVAL;
	if (auth_flavors[flavor] != NULL)
		return -EPERM;		/* what else? */
	auth_flavors[flavor] = ops;
	return 0;
}

int
rpcauth_unregister(struct rpc_authops *ops)
{
	unsigned int	flavor;

	if ((flavor = ops->au_flavor) >= RPC_MAXFLAVOR)
		return -EINVAL;
	if (auth_flavors[flavor] != ops)
		return -EPERM;		/* what else? */
	auth_flavors[flavor] = NULL;
	return 0;
}

struct rpc_auth *
rpcauth_create(unsigned int flavor, struct rpc_clnt *clnt)
{
	struct rpc_authops	*ops;

	if (flavor >= RPC_MAXFLAVOR || !(ops = auth_flavors[flavor]))
		return NULL;
	clnt->cl_auth = ops->create(clnt);
	return clnt->cl_auth;
}

void
rpcauth_destroy(struct rpc_auth *auth)
{
	auth->au_ops->destroy(auth);
}

/*
 * Initialize RPC credential cache
 */
void
rpcauth_init_credcache(struct rpc_auth *auth)
{
	memset(auth->au_credcache, 0, sizeof(auth->au_credcache));
	auth->au_nextgc = jiffies + (auth->au_expire >> 1);
}

/*
 * Clear the RPC credential cache
 */
void
rpcauth_free_credcache(struct rpc_auth *auth)
{
	struct rpc_cred	**q, *cred;
	void		(*destroy)(struct rpc_cred *);
	int		i;

	if (!(destroy = auth->au_ops->crdestroy))
		destroy = (void (*)(struct rpc_cred *)) rpc_free;

	for (i = 0; i < RPC_CREDCACHE_NR; i++) {
		q = &auth->au_credcache[i];
		while ((cred = *q) != NULL) {
			*q = cred->cr_next;
			destroy(cred);
		}
	}
}

/*
 * Remove stale credentials. Avoid sleeping inside the loop.
 */
static void
rpcauth_gc_credcache(struct rpc_auth *auth)
{
	struct rpc_cred	**q, *cred, *free = NULL;
	int		i, safe = 0;

	dprintk("RPC: gc'ing RPC credentials for auth %p\n", auth);
	for (i = 0; i < RPC_CREDCACHE_NR; i++) {
		q = &auth->au_credcache[i];
		while ((cred = *q) != NULL) {
			if (++safe > 500) {
				printk("RPC: rpcauth_gc_credcache looping!\n");
				break;
			}
			if (!cred->cr_count && time_before(cred->cr_expire, jiffies)) {
				*q = cred->cr_next;
				cred->cr_next = free;
				free = cred;
				continue;
			}
			q = &cred->cr_next;
		}
	}
	while ((cred = free) != NULL) {
		free = cred->cr_next;
		rpc_free(cred);
	}
	auth->au_nextgc = jiffies + auth->au_expire;
}

/*
 * Insert credential into cache
 */
inline void
rpcauth_insert_credcache(struct rpc_auth *auth, struct rpc_cred *cred)
{
	int		nr;

	nr = (cred->cr_uid % RPC_CREDCACHE_NR);
	cred->cr_next = auth->au_credcache[nr];
	auth->au_credcache[nr] = cred;
	cred->cr_expire = jiffies + auth->au_expire;
	cred->cr_count++;
}

/*
 * Look up a process' credentials in the authentication cache
 */
static struct rpc_cred *
rpcauth_lookup_credcache(struct rpc_task *task)
{
	struct rpc_auth	*auth = task->tk_auth;
	struct rpc_cred	**q, *cred = NULL;
	int		nr;

	nr = RPC_DO_ROOTOVERRIDE(task)? 0 : (current->uid % RPC_CREDCACHE_NR);

	if (time_before(auth->au_nextgc, jiffies))
		rpcauth_gc_credcache(auth);

	q = &auth->au_credcache[nr];
	while ((cred = *q) != NULL) {
		if (auth->au_ops->crmatch(task, cred)) {
			*q = cred->cr_next;
			break;
		}
		q = &cred->cr_next;
	}

	if (!cred)
		cred = auth->au_ops->crcreate(task);

	rpcauth_insert_credcache(auth, cred);

	return (struct rpc_cred *) cred;
}

/*
 * Remove cred handle from cache
 */
static inline void
rpcauth_remove_credcache(struct rpc_auth *auth, struct rpc_cred *cred)
{
	struct rpc_cred	**q, *cr;
	int		nr;

	nr = (cred->cr_uid % RPC_CREDCACHE_NR);
	q = &auth->au_credcache[nr];
	while ((cr = *q) != NULL) {
		if (cred == cr) {
			*q = cred->cr_next;
			return;
		}
		q = &cred->cr_next;
	}
}

struct rpc_cred *
rpcauth_lookupcred(struct rpc_task *task)
{
	dprintk("RPC: %4d looking up %s cred\n",
		task->tk_pid, task->tk_auth->au_ops->au_name);
	return task->tk_cred = rpcauth_lookup_credcache(task);
}

int
rpcauth_matchcred(struct rpc_task *task, struct rpc_cred *cred)
{
	struct rpc_auth	*auth = task->tk_auth;

	dprintk("RPC: %4d matching %s cred %p\n",
		task->tk_pid, auth->au_ops->au_name, task->tk_cred);
	return auth->au_ops->crmatch(task, cred);
}

void
rpcauth_holdcred(struct rpc_task *task)
{
	dprintk("RPC: %4d holding %s cred %p\n",
		task->tk_pid, task->tk_auth->au_ops->au_name, task->tk_cred);
	if (task->tk_cred)
		task->tk_cred->cr_count++;
}

void
rpcauth_releasecred(struct rpc_task *task)
{
	struct rpc_auth	*auth = task->tk_auth;
	struct rpc_cred	*cred;

	dprintk("RPC: %4d releasing %s cred %p\n",
		task->tk_pid, auth->au_ops->au_name, task->tk_cred);
	if ((cred = task->tk_cred) != NULL) {
		cred->cr_count--;
		if (cred->cr_flags & RPCAUTH_CRED_DEAD) {
			rpcauth_remove_credcache(auth, cred);
			if (!cred->cr_count)
				auth->au_ops->crdestroy(cred);
		}
		task->tk_cred = NULL;
	}
}

u32 *
rpcauth_marshcred(struct rpc_task *task, u32 *p)
{
	struct rpc_auth	*auth = task->tk_auth;

	dprintk("RPC: %4d marshaling %s cred %p\n",
		task->tk_pid, auth->au_ops->au_name, task->tk_cred);
	return auth->au_ops->crmarshal(task, p,
				task->tk_flags & RPC_CALL_REALUID);
}

u32 *
rpcauth_checkverf(struct rpc_task *task, u32 *p)
{
	struct rpc_auth	*auth = task->tk_auth;

	dprintk("RPC: %4d validating %s cred %p\n",
		task->tk_pid, auth->au_ops->au_name, task->tk_cred);
	return auth->au_ops->crvalidate(task, p);
}

int
rpcauth_refreshcred(struct rpc_task *task)
{
	struct rpc_auth	*auth = task->tk_auth;

	dprintk("RPC: %4d refreshing %s cred %p\n",
		task->tk_pid, auth->au_ops->au_name, task->tk_cred);
	task->tk_status = auth->au_ops->crrefresh(task);
	return task->tk_status;
}

void
rpcauth_invalcred(struct rpc_task *task)
{
	dprintk("RPC: %4d invalidating %s cred %p\n",
		task->tk_pid, task->tk_auth->au_ops->au_name, task->tk_cred);
	if (task->tk_cred)
		task->tk_cred->cr_flags &= ~RPCAUTH_CRED_UPTODATE;
}

int
rpcauth_uptodatecred(struct rpc_task *task)
{
	return !(task->tk_cred) ||
		(task->tk_cred->cr_flags & RPCAUTH_CRED_UPTODATE);
}

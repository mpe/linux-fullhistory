/*
 * linux/include/linux/auth.h
 *
 * Declarations for the RPC authentication machinery.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_AUTH_H
#define _LINUX_SUNRPC_AUTH_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/sunrpc/sched.h>

/* size of the nodename buffer */
#define UNX_MAXNODENAME	32

/*
 * Client user credentials
 */
struct rpc_cred {
	struct rpc_cred *	cr_next;	/* linked list */
	unsigned long		cr_expire;	/* when to gc */
	unsigned short		cr_count;	/* ref count */
	unsigned short		cr_flags;	/* various flags */

	uid_t			cr_uid;

	/* per-flavor data */
};
#define RPCAUTH_CRED_LOCKED	0x0001
#define RPCAUTH_CRED_UPTODATE	0x0002
#define RPCAUTH_CRED_DEAD	0x0004

/*
 * Client authentication handle
 */
#define RPC_CREDCACHE_NR	8
struct rpc_auth {
	struct rpc_cred *	au_credcache[RPC_CREDCACHE_NR];
	unsigned long		au_expire;	/* cache expiry interval */
	unsigned long		au_nextgc;	/* next garbage collection */
	unsigned int		au_cslack;	/* call cred size estimate */
	unsigned int		au_rslack;	/* reply verf size guess */
	unsigned int		au_flags;	/* various flags */
	struct rpc_authops *	au_ops;		/* operations */

	/* per-flavor data */
};
#define RPC_AUTH_PROC_CREDS	0x0010		/* process creds (including
						 * uid/gid, fs[ug]id, gids)
						 */

/*
 * Client authentication ops
 */
struct rpc_authops {
	unsigned int		au_flavor;	/* flavor (RPC_AUTH_*) */
#ifdef RPC_DEBUG
	char *			au_name;
#endif
	struct rpc_auth *	(*create)(struct rpc_clnt *);
	void			(*destroy)(struct rpc_auth *);

	struct rpc_cred *	(*crcreate)(struct rpc_task *);
	void			(*crdestroy)(struct rpc_cred *);

	int			(*crmatch)(struct rpc_task *, struct rpc_cred*);
	u32 *			(*crmarshal)(struct rpc_task *, u32 *, int);
	int			(*crrefresh)(struct rpc_task *);
	u32 *			(*crvalidate)(struct rpc_task *, u32 *);
};

extern struct rpc_authops	authunix_ops;
extern struct rpc_authops	authnull_ops;
#ifdef CONFIG_SUNRPC_SECURE
extern struct rpc_authops	authdes_ops;
#endif

int			rpcauth_register(struct rpc_authops *);
int			rpcauth_unregister(struct rpc_authops *);
struct rpc_auth *	rpcauth_create(unsigned int, struct rpc_clnt *);
void			rpcauth_destroy(struct rpc_auth *);
struct rpc_cred *	rpcauth_lookupcred(struct rpc_task *);
void			rpcauth_holdcred(struct rpc_task *);
void			rpcauth_releasecred(struct rpc_task *);
int			rpcauth_matchcred(struct rpc_task *, struct rpc_cred *);
u32 *			rpcauth_marshcred(struct rpc_task *, u32 *);
u32 *			rpcauth_checkverf(struct rpc_task *, u32 *);
int			rpcauth_refreshcred(struct rpc_task *);
void			rpcauth_invalcred(struct rpc_task *);
int			rpcauth_uptodatecred(struct rpc_task *);
void			rpcauth_init_credcache(struct rpc_auth *);
void			rpcauth_free_credcache(struct rpc_auth *);
void			rpcauth_insert_credcache(struct rpc_auth *,
						struct rpc_cred *);

#endif /* __KERNEL__ */
#endif /* _LINUX_SUNRPC_AUTH_H */

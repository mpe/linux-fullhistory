/*
 * linux/include/linux/nfsiod.h
 *
 * Declarations for asynchronous NFS RPC calls.
 *
 */

#ifndef _LINUX_NFSIOD_H
#define _LINUX_NFSIOD_H

#include <linux/rpcsock.h>

#ifdef __KERNEL__

/*
 * This is the callback handler for nfsiod requests.
 * Note that the callback procedure must NOT sleep.
 */
struct nfsiod_req;
typedef void	(*nfsiod_done_fn_t)(int result, struct nfsiod_req *);

/*
 * This is the nfsiod request struct.
 */
struct nfsiod_req {
	struct nfsiod_req *	rq_next;
	struct nfsiod_req *	rq_prev;
	struct nfs_server *	rq_server;
	struct wait_queue *	rq_wait;
	struct rpc_ioreq	rq_rpcreq;
	nfsiod_done_fn_t	rq_callback;
	void *			rq_cdata;
};

struct nfsiod_req *	nfsiod_reserve(struct nfs_server *, nfsiod_done_fn_t);
void			nfsiod_release(struct nfsiod_req *);
int			nfsiod_enqueue(struct nfsiod_req *);
int			nfsiod(void);


#endif /* __KERNEL__ */
#endif /* _LINUX_NFSIOD_H */

/*
 * linux/fs/nfs/nfsiod.c
 *
 * Async NFS RPC call support.
 *
 * When a process wants to place an asynchronous RPC call, it reserves
 * an nfsiod slot, fills in all necessary fields including the callback
 * handler field, and enqueues the request.
 *
 * This will wake up nfsiod, which calls nfs_rpc_doio to collect the
 * reply. It then dispatches the result to the caller via the callback
 * function, including result value and request pointer. It then re-inserts
 * itself into the free list.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/sched.h>
#include <linux/nfs_fs.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/rpcsock.h>
#include <linux/nfsiod.h>

static struct nfsiod_req *	free_list = NULL;
static int			active = 0;

#undef DEBUG_NFSIOD
#ifdef DEBUG_NFSIOD
#define dprintk(args...)	printk(## args)
#else
#define dprintk(args...)	/* nothing */
#endif


/*
 * Reserve an nfsiod slot and initialize the request struct
 */
struct nfsiod_req *
nfsiod_reserve(struct nfs_server *server)
{
	struct nfsiod_req	*req;

	if (!(req = free_list)) {
		dprintk("BIO: nfsiod_reserve: no free nfsiods\n");
		return NULL;
	}
	free_list = req->rq_next;
	memset(&req->rq_rpcreq, 0, sizeof(struct rpc_ioreq));

	if (rpc_reserve(server->rsock, &req->rq_rpcreq, 1) < 0) {
		dprintk("BIO: nfsiod_reserve failed to reserve RPC slot\n");
		req->rq_next = free_list;
		free_list = req;
		return NULL;
	}

	req->rq_server = server;
	return req;
}

void
nfsiod_release(struct nfsiod_req *req)
{
	dprintk("BIO: nfsiod_release called\n");
	rpc_release(req->rq_server->rsock, &req->rq_rpcreq);
	memset(&req->rq_rpcreq, 0, sizeof(struct rpc_ioreq));
	req->rq_next = free_list;
	free_list = req;
}

/*
 * Transmit a request and put it on nfsiod's list of pending requests.
 */
void
nfsiod_enqueue(struct nfsiod_req *req)
{
	dprintk("BIO: enqueuing request %p\n", &req->rq_rpcreq);
	wake_up(&req->rq_wait);
	schedule();
}

/*
 * This is the main nfsiod loop.
 */
int
nfsiod(void)
{
	struct nfsiod_req	request, *req = &request;
	int			result;

	dprintk("BIO: nfsiod %d starting\n", current->pid);
	while (1) {
		/* Insert request into free list */
		memset(req, 0, sizeof(*req));
		req->rq_next = free_list;
		free_list = req;

		/* Wait until user enqueues request */
		dprintk("BIO: before: now %d nfsiod's active\n", active);
		dprintk("BIO: nfsiod %d waiting\n", current->pid);
		interruptible_sleep_on(&req->rq_wait);

		if (current->signal & ~current->blocked)
			break;
		if (!req->rq_rpcreq.rq_slot)
			continue;
		dprintk("BIO: nfsiod %d woken up; calling nfs_rpc_doio.\n",
				current->pid);
		active++;
		dprintk("BIO: before: now %d nfsiod's active\n", active);
		do {
			result = nfs_rpc_doio(req->rq_server,
						&req->rq_rpcreq, 1);
		} while (!req->rq_callback(result, req));
		active--;
	}

	return 0;
}

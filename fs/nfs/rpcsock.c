/*
 *  linux/fs/nfs/rpcsock.c
 *
 *  This is a generic RPC call interface for datagram sockets that is able
 *  to place several concurrent RPC requests at the same time. It works like
 *  this:
 *
 *  -	When a process places a call, it allocates a request slot if
 *	one is available. Otherwise, it sleeps on the backlog queue
 *	(rpc_reserve).
 *  -	Then, the message is transmitted via rpc_send (exported by name of
 *	rpc_transmit).
 *  -	Finally, the process waits for the call to complete (rpc_doio):
 *	The first process on the receive queue waits for the next RPC packet,
 *	and peeks at the XID. If it finds a matching request, it receives
 *	the datagram on behalf of that process and wakes it up. Otherwise,
 *	the datagram is discarded.
 *  -	If the process having received the datagram was the first one on
 *	the receive queue, it wakes up the next one to listen for replies.
 *  -	It then removes itself from the request queue (rpc_release).
 *	If there are more callers waiting on the backlog queue, they are
 *	woken up, too.
 *
 * Mar 1996:
 *  -	Split up large functions into smaller chunks as per Linus' coding
 *	style. Found an interesting bug this way, too.
 *  -	Added entry points for nfsiod.
 *
 *  Copyright (C) 1995, 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/nfs_fs.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/rpcsock.h>

#include <linux/udp.h>
#include <net/sock.h>

#include <asm/segment.h>

#define msleep(sec)	{ current->timeout = sec * HZ / 1000; \
			  current->state = TASK_INTERRUPTIBLE; \
			  schedule(); \
			}

#undef DEBUG_RPC
#ifdef DEBUG_RPC			
#define dprintk(args...)	printk(## args)
#else
#define	dprintk(args...)
#endif


/*
 * Insert new request into wait list. We make sure list is sorted by
 * increasing timeout value.
 */
static inline void
rpc_insque(struct rpc_sock *rsock, struct rpc_wait *slot)
{
	struct rpc_wait	*next = rsock->pending;

	slot->w_next = next;
	slot->w_prev = NULL;
	if (next)
		next->w_prev = slot;
	rsock->pending = slot;
	slot->w_queued = 1;

	dprintk("RPC: inserted %p into queue\n", slot);
}

/*
 * Remove request from request queue
 */
static inline void
rpc_remque(struct rpc_sock *rsock, struct rpc_wait *slot)
{
	struct rpc_wait	*prev = slot->w_prev,
			*next = slot->w_next;

	if (prev != NULL)
		prev->w_next = next;
	else
		rsock->pending = next;
	if (next != NULL)
		next->w_prev = prev;

	slot->w_queued = 0;
	dprintk("RPC: removed %p from queue, head now %p.\n",
			slot, rsock->pending);
}

/*
 * Write data to socket.
 */
static inline int
rpc_sendmsg(struct rpc_sock *rsock, struct iovec *iov, int nr, int len,
				struct sockaddr *sap, int salen)
{
	struct socket	*sock = rsock->sock;
	struct msghdr	msg;
	unsigned long	oldfs;
	int		result;

	msg.msg_iov	= iov;
	msg.msg_iovlen	= nr;
	msg.msg_name	= sap;
	msg.msg_namelen = salen;
	msg.msg_control = NULL;

	oldfs = get_fs();
	set_fs(get_ds());
	result = sock->ops->sendmsg(sock, &msg, len, 0, 0);
	set_fs(oldfs);

	dprintk("RPC: rpc_sendmsg(iov %p, len %d) = %d\n", iov, len, result);
	return result;
}
/*
 * Read data from socket
 */
static inline int
rpc_recvmsg(struct rpc_sock *rsock, struct iovec *iov,
			int nr, int len, int flags)
{
	struct socket	*sock = rsock->sock;
	struct sockaddr	sa;
	struct msghdr	msg;
	unsigned long	oldfs;
	int		result, alen;

	msg.msg_iov	= iov;
	msg.msg_iovlen	= nr;
	msg.msg_name	= &sa;
	msg.msg_namelen = sizeof(sa);
	msg.msg_control = NULL;

	oldfs = get_fs();
	set_fs(get_ds());
	result = sock->ops->recvmsg(sock, &msg, len, 1, flags, &alen);
	set_fs(oldfs);

	dprintk("RPC: rpc_recvmsg(iov %p, len %d) = %d\n", iov, len, result);
	return result;
}

/*
 * This code is slightly complicated. Since the networking code does not
 * honor the current->timeout value, we have to select on the socket.
 */
static inline int
rpc_select(struct rpc_sock *rsock)
{
	struct select_table_entry entry;
	struct file	*file = rsock->file;
	select_table	wait_table;

	dprintk("RPC: selecting on socket...\n");
	wait_table.nr = 0;
	wait_table.entry = &entry;
	current->state = TASK_INTERRUPTIBLE;
	if (!file->f_op->select(file->f_inode, file, SEL_IN, &wait_table)
	 && !file->f_op->select(file->f_inode, file, SEL_IN, NULL)) {
		schedule();
		remove_wait_queue(entry.wait_address, &entry.wait);
		current->state = TASK_RUNNING;
		if (current->signal & ~current->blocked)
			return -ERESTARTSYS;
		if (current->timeout == 0)
			return -ETIMEDOUT;
	} else if (wait_table.nr)
		remove_wait_queue(entry.wait_address, &entry.wait);
	current->state = TASK_RUNNING;
	dprintk("RPC: ...Okay, there appears to be some data.\n");
	return 0;
}

/*
 * Reserve an RPC call slot. nocwait determines whether we wait in case
 * of congestion or not.
 */
int
rpc_reserve(struct rpc_sock *rsock, struct rpc_ioreq *req, int nocwait)
{
	struct rpc_wait	*slot;

	req->rq_slot = NULL;

	while (!(slot = rsock->free) || rsock->cong >= rsock->cwnd) {
		if (nocwait) {
			current->timeout = 0;
			return -ENOBUFS;
		}
		dprintk("RPC: rpc_reserve waiting on backlog\n");
		interruptible_sleep_on(&rsock->backlog);
		if (current->timeout == 0)
			return -ETIMEDOUT;
		if (current->signal & ~current->blocked)
			return -ERESTARTSYS;
		if (rsock->shutdown)
			return -EIO;
	}

	rsock->free = slot->w_next;
	rsock->cong += RPC_CWNDSCALE;	/* bump congestion value */

	slot->w_queued = 0;
	slot->w_gotit = 0;
	slot->w_req = req;

	dprintk("RPC: reserved slot %p\n", slot);
	req->rq_slot = slot;
	return 0;
}

/*
 * Release an RPC call slot
 */
void
rpc_release(struct rpc_sock *rsock, struct rpc_ioreq *req)
{
	struct rpc_wait	*slot = req->rq_slot;

	if (slot != NULL) {
		dprintk("RPC: release slot %p\n", slot);

		/* Wake up the next receiver */
		if (slot == rsock->pending && slot->w_next != NULL)
			wake_up(&slot->w_next->w_wait);

		/* remove slot from queue of pending */
		if (slot->w_queued)
			rpc_remque(rsock, slot);
		slot->w_next = rsock->free;
		rsock->free = slot;

		/* decrease congestion value */
		rsock->cong -= RPC_CWNDSCALE;
		if (rsock->cong < rsock->cwnd && rsock->backlog)
			wake_up(&rsock->backlog);
		if (rsock->shutdown)
			wake_up(&rsock->shutwait);

		req->rq_slot = NULL;
	}
}

/*
 * Adjust RPC congestion window
 */
static void
rpc_cwnd_adjust(struct rpc_sock *rsock, int timeout)
{
	unsigned long	cwnd = rsock->cwnd;

	if (!timeout) {
		if (rsock->cong >= cwnd) {
			/* The (cwnd >> 1) term makes sure
			 * the result gets rounded properly. */
			cwnd += (RPC_CWNDSCALE * RPC_CWNDSCALE +
					(cwnd >> 1)) / cwnd;
			if (cwnd > RPC_MAXCWND)
				cwnd = RPC_MAXCWND;
		}
	} else {
		if ((cwnd >>= 1) < RPC_CWNDSCALE)
			cwnd = RPC_CWNDSCALE;
		dprintk("RPC: cwnd decrease %08lx\n", cwnd);
	}
	dprintk("RPC: cong %08lx, cwnd was %08lx, now %08lx\n",
			rsock->cong, rsock->cwnd, cwnd);

	rsock->cwnd = cwnd;
}

static inline void
rpc_send_check(char *where, u32 *ptr)
{
	if (ptr[1] != htonl(RPC_CALL) || ptr[2] != htonl(RPC_VERSION)) {
		printk("RPC: %s sending evil packet:\n"
		       "     %08x %08x %08x %08x %08x %08x %08x %08x\n",
		       where,
		       ptr[0], ptr[1], ptr[2], ptr[3],
		       ptr[4], ptr[5], ptr[6], ptr[7]);
	}
}

/*
 * Place the actual RPC call.
 * We have to copy the iovec because sendmsg fiddles with its contents.
 */
static inline int
rpc_send(struct rpc_sock *rsock, struct rpc_wait *slot)
{
	struct rpc_ioreq *req = slot->w_req;
	struct iovec	iov[UIO_MAXIOV];

	if (rsock->shutdown)
		return -EIO;

	memcpy(iov, req->rq_svec, req->rq_snr * sizeof(iov[0]));
	slot->w_xid = *(u32 *)(iov[0].iov_base);
	if (!slot->w_queued)
		rpc_insque(rsock, slot);

	dprintk("rpc_send(%p, %x)\n", slot, slot->w_xid);
	rpc_send_check("rpc_send", (u32 *) req->rq_svec[0].iov_base);
	return rpc_sendmsg(rsock, iov, req->rq_snr, req->rq_slen,
				req->rq_addr, req->rq_alen);
}

/*
 * This is the same as rpc_send but for the functions exported to nfsiod
 */
int
rpc_transmit(struct rpc_sock *rsock, struct rpc_ioreq *req)
{
	rpc_send_check("rpc_transmit", (u32 *) req->rq_svec[0].iov_base);
	return rpc_send(rsock, req->rq_slot);
}

/*
 * Receive and dispatch a single reply
 */
static inline int
rpc_grok(struct rpc_sock *rsock)
{
	struct rpc_wait	*rovr;
	struct rpc_ioreq *req;
	struct iovec	iov[UIO_MAXIOV];
	u32		xid;
	int		safe, result;

	iov[0].iov_base = (void *) &xid;
	iov[0].iov_len  = sizeof(xid);
	result = rpc_recvmsg(rsock, iov, 1, sizeof(xid), MSG_PEEK);

	if (result < 0) {
		switch (-result) {
		case EAGAIN: case ECONNREFUSED:
			return 0;
		case ERESTARTSYS:
			return result;
		default:
			dprintk("rpc_grok: recv error = %d\n", result);
		}
	}
	if (result < 4) {
		printk(KERN_WARNING "RPC: impossible RPC reply size %d\n",
						result);
		return 0;
	}

	dprintk("RPC: rpc_grok: got xid %08lx\n", (unsigned long) xid);

	/* Look for the caller */
	safe = 0;
	for (rovr = rsock->pending; rovr; rovr = rovr->w_next) {
		if (rovr->w_xid == xid)
			break;
		if (safe++ > RPC_MAXREQS) {
			printk(KERN_WARNING "RPC: loop in request Q!!\n");
			rovr = NULL;
			break;
		}
	}

	if (!rovr || rovr->w_gotit) {
		/* discard dgram */
		dprintk("RPC: rpc_grok: %s.\n",
			rovr? "duplicate reply" : "bad XID");
		iov[0].iov_base = (void *) &xid;
		iov[0].iov_len  = sizeof(xid);
		rpc_recvmsg(rsock, iov, 1, sizeof(xid), 0);
		return 0;
	}
	req = rovr->w_req;

	/* Now receive the reply... Copy the iovec first because of 
	 * memcpy_fromiovec fiddling. */
	memcpy(iov, req->rq_rvec, req->rq_rnr * sizeof(iov[0]));
	result = rpc_recvmsg(rsock, iov, req->rq_rnr, req->rq_rlen, 0);
	rovr->w_result = result;
	rovr->w_gotit = 1;

	/* ... and wake up the process */
	wake_up(&rovr->w_wait);

	return result;
}

/*
 * Wait for the reply to our call.
 */
static int
rpc_recv(struct rpc_sock *rsock, struct rpc_wait *slot)
{
	int	result;

	do {
		/* If we are not the receiver, wait on the sidelines */
		dprintk("RPC: rpc_recv TP1\n");
		while (rsock->pending != slot) {
			if (!slot->w_gotit)
				interruptible_sleep_on(&slot->w_wait);
			if (slot->w_gotit)
				return slot->w_result; /* quite important */
			if (current->signal & ~current->blocked)
				return -ERESTARTSYS;
			if (rsock->shutdown)
				return -EIO;
			if (current->timeout == 0)
				return -ETIMEDOUT;
		}

		/* Wait for data to arrive */
		if ((result = rpc_select(rsock)) < 0) {
			dprintk("RPC: select error = %d\n", result);
			return result;
		}

		/* Receive and dispatch */
		if ((result = rpc_grok(rsock)) < 0)
			return result;
	} while (current->timeout && !slot->w_gotit);

	return slot->w_gotit? slot->w_result : -ETIMEDOUT;
}

/*
 * Generic RPC call routine. This handles retries and timeouts etc pp.
 *
 * If sent is non-null, it assumes the called has already sent out the
 * message, so it won't need to do so unless a timeout occurs.
 */
int
rpc_doio(struct rpc_sock *rsock, struct rpc_ioreq *req,
			struct rpc_timeout *strategy, int sent)
{
	struct rpc_wait	*slot;
	int		result, retries;
	unsigned long	timeout;

	timeout = strategy->to_initval;
	retries = 0;
	slot = req->rq_slot;

	do {
		dprintk("RPC: rpc_doio: TP1 (req %p)\n", req);
		current->timeout = jiffies + timeout;
		if (slot == NULL) {
			result = rpc_reserve(rsock, req, 0);
			if (result == -ETIMEDOUT)
				goto timedout;
			if (result < 0)
				break;
			slot = req->rq_slot;
			rpc_send_check("rpc_doio",
				(u32 *) req->rq_svec[0].iov_base);
			rpc_insque(rsock, slot);
		}

		/* This check is for loopback NFS. Sometimes replies come
		 * in before biod has called rpc_doio... */
		if (slot->w_gotit) {
			result = slot->w_result;
			break;
		}

		dprintk("RPC: rpc_doio: TP2\n");
		if (sent || (result = rpc_send(rsock, slot)) >= 0) {
			result = rpc_recv(rsock, slot);
			sent = 0;
		}

		if (result != -ETIMEDOUT) {
			/* dprintk("RPC: rpc_recv returned %d\n", result); */
			rpc_cwnd_adjust(rsock, 0);
			break;
		}

		rpc_cwnd_adjust(rsock, 1);

timedout:
		dprintk("RPC: rpc_recv returned timeout.\n");
		if (strategy->to_exponential)
			timeout <<= 1;
		else
			timeout += strategy->to_increment;
		if (strategy->to_maxval && timeout >= strategy->to_maxval)
			timeout = strategy->to_maxval;
		if (strategy->to_retries && ++retries >= strategy->to_retries)
			break;
	} while (1);

	dprintk("RPC: rpc_doio: TP3\n");
	current->timeout = 0;
	return result;
}

/*
 */
int
rpc_call(struct rpc_sock *rsock, struct rpc_ioreq *req,
			struct rpc_timeout *strategy)
{
	int	result;

	result = rpc_doio(rsock, req, strategy, 0);
	if (req->rq_slot == NULL)
		printk(KERN_WARNING "RPC: bad: rq_slot == NULL\n");
	rpc_release(rsock, req);
	return result;
}

struct rpc_sock *
rpc_makesock(struct file *file)
{
	struct rpc_sock	*rsock;
	struct socket	*sock;
	struct sock	*sk;
	struct rpc_wait	*slot;
	int		i;

	dprintk("RPC: make RPC socket...\n");
	sock = &file->f_inode->u.socket_i;
	if (sock->type != SOCK_DGRAM || sock->ops->family != AF_INET) {
		printk(KERN_WARNING "RPC: only UDP sockets supported\n");
		return NULL;
	}
	sk = (struct sock *) sock->data;

	if ((rsock = kmalloc(sizeof(struct rpc_sock), GFP_KERNEL)) == NULL)
		return NULL;
	memset(rsock, 0, sizeof(*rsock)); /* Nnnngh! */

	rsock->sock = sock;
	rsock->inet = sk;
	rsock->file = file;
	rsock->cwnd = RPC_INITCWND;

	dprintk("RPC: slots %p, %p, ...\n", rsock->waiting, rsock->waiting + 1);
	rsock->free = rsock->waiting;
	for (i = 0, slot = rsock->waiting; i < RPC_MAXREQS-1; i++, slot++)
		slot->w_next = slot + 1;
	slot->w_next = NULL;

	dprintk("RPC: made socket %p\n", rsock);
	return rsock;
}

int
rpc_closesock(struct rpc_sock *rsock)
{
	unsigned long	t0 = jiffies;

	rsock->shutdown = 1;
	while (rsock->pending || rsock->backlog) {
		interruptible_sleep_on(&rsock->shutwait);
		if (current->signal & ~current->blocked)
			return -EINTR;
#if 1
		if (t0 && t0 - jiffies > 60 * HZ) {
			printk(KERN_WARNING "RPC: hanging in rpc_closesock.\n");
			t0 = 0;
		}
#endif
	}

	kfree(rsock);
	return 0;
}

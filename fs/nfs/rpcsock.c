/*
 *  linux/fs/nfs/rpcsock.c
 *
 *  This is a generic RPC call interface for datagram sockets that is able
 *  to place several concurrent RPC requests at the same time. It works like
 *  this:
 *
 *  -	When a process places a call, it allocates a request slot if
 *	one is available. Otherwise, it sleeps on the backlog queue.
 *  -	The first process on the receive queue waits for the next RPC reply,
 *	and peeks at the XID. If it finds a matching request, it receives
 *	the datagram on behalf of that process and wakes it up. Otherwise,
 *	the datagram is discarded.
 *  -	If the process having received the datagram was the first one on
 *	the receive queue, it wakes up the next one to listen for replies.
 *  -	It then removes itself from the request queue. If there are more
 *	callers waiting on the backlog queue, they are woken up, too.
 *
 *  Copyright (C) 1995, Olaf Kirch <okir@monad.swb.de>
 */

#ifdef MODULE
#include <linux/module.h>
#endif

#include <linux/types.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/nfs_fs.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/fcntl.h>
#include <asm/segment.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/rpcsock.h>

#define msleep(sec)	{ current->timeout = sec * HZ / 1000; \
			  current->state = TASK_INTERRUPTIBLE; \
			  schedule(); \
			}
#define dprintk		if (0) printk

static inline void
rpc_insque(struct rpc_sock *rsock, struct rpc_wait *slot)
{
	struct rpc_wait	*tmp;

	if ((tmp = rsock->tail) != NULL) {
		tmp->next = slot;
	} else {
		rsock->head = slot;
	}
	rsock->tail = slot;
	slot->prev = tmp;
	slot->next = NULL;
	dprintk("RPC: inserted %08lx into queue.\n", (long)slot);
	dprintk("RPC: head = %08lx, tail = %08lx.\n",
			(long) rsock->head, (long) rsock->tail);
}

static inline void
rpc_remque(struct rpc_sock *rsock, struct rpc_wait *slot)
{
	struct rpc_wait	*prev = slot->prev,
			*next = slot->next;

	if (prev != NULL)
		prev->next = next;
	else
		rsock->head = next;
	if (next != NULL)
		next->prev = prev;
	else
		rsock->tail = prev;
	dprintk("RPC: removed %08lx from queue.\n", (long)slot);
	dprintk("RPC: head = %08lx, tail = %08lx.\n",
			(long) rsock->head, (long) rsock->tail);
}

static inline int
rpc_sendmsg(struct rpc_sock *rsock, struct msghdr *msg, int len)
{
	struct socket	*sock = rsock->sock;
	unsigned long	oldfs;
	int		result;

	dprintk("RPC: sending %d bytes (buf %p)\n", len, msg->msg_iov[0].iov_base);
	oldfs = get_fs();
	set_fs(get_ds());
	result = sock->ops->sendmsg(sock, msg, len, 0, 0);
	set_fs(oldfs);
	dprintk("RPC: result = %d\n", result);

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

static inline int
rpc_recvmsg(struct rpc_sock *rsock, struct msghdr *msg, int len,int flags)
{
	struct socket	*sock = rsock->sock;
	struct sockaddr	sa;
	int		alen = sizeof(sa);
	unsigned long	oldfs;
	int		result;

	dprintk("RPC: receiving %d bytes max (buf %p)\n", len, msg->msg_iov[0].iov_base);
	oldfs = get_fs();
	set_fs(get_ds());
	result = sock->ops->recvmsg(sock, msg, len, 1, flags, &alen);
	set_fs(oldfs);
	dprintk("RPC: result = %d\n", result);

#if 0
	if (alen != salen || memcmp(&sa, sap, alen)) {
		dprintk("RPC: reply address mismatch... rejected.\n");
		result = -EAGAIN;
	}
#endif

	return result;
}

/*
 * Place the actual RPC call.
 */
static int
rpc_call_one(struct rpc_sock *rsock, struct rpc_wait *slot,
		struct sockaddr *sap, int salen,
		const int *sndbuf, int slen, int *rcvbuf, int rlen)
{
	struct rpc_wait	*rovr = NULL;
	int		result;
	u32		xid;
	int		safe;
	struct msghdr   msg;
	struct iovec	iov;
	
	msg.msg_iov	=	&iov;
	msg.msg_iovlen	=	1;
	msg.msg_name	=	(void *)sap;
	msg.msg_namelen	=	salen;
	msg.msg_accrights =	NULL;
	iov.iov_base	=	(void *)sndbuf;
	iov.iov_len	=	slen;

	dprintk("RPC: placing one call, rsock = %08lx, slot = %08lx, "
		"sap = %08lx, salen = %d, "
		"sndbuf = %08lx, slen = %d, rcvbuf = %08lx, rlen = %d\n",
		(long) rsock, (long) slot, (long) sap, 
		salen, (long) sndbuf, slen, (long) rcvbuf, rlen);

	result = rpc_sendmsg(rsock, &msg, slen);
	if (result < 0)
		return result;

	do {
		/* We are not the receiver. Wait on the side lines. */
		if (rsock->head != slot) {
			interruptible_sleep_on(&slot->wait);
			if (slot->gotit)
				break;
			if (current->timeout != 0)
				continue;
			if (rsock->shutdown) {
				printk("RPC: aborting call due to shutdown.\n");
				return -EIO;
			}
			return -ETIMEDOUT;
		}
		
		/* wait for data to arrive */
		result = rpc_select(rsock);
		if (result < 0) {
			dprintk("RPC: select error = %d\n", result);
			break;
		}

		iov.iov_base=(void *)&xid;
		iov.iov_len=sizeof(xid);
		
		result = rpc_recvmsg(rsock, &msg, sizeof(xid), MSG_PEEK);
		if (result < 0) {
			switch (-result) {
			case EAGAIN: case ECONNREFUSED:
				continue;
			default:
				dprintk("rpc_call: recv error = %d\n", result);
			case ERESTARTSYS:
				return result;
			}
		}

		/* Look for the caller */
		safe = 0;
		for (rovr = rsock->head; rovr; rovr = rovr->next) {
			if (safe++ > NRREQS) {
				printk("RPC: loop in request Q!!\n");
				rovr = NULL;
				break;
			}
			if (rovr->xid == xid)
				break;
		}

		if (!rovr || rovr->gotit) {
			/* bad XID or duplicate reply, discard dgram */
			dprintk("RPC: bad XID or duplicate reply.\n");
			iov.iov_base=(void *)&xid;
			iov.iov_len=sizeof(xid);
			rpc_recvmsg(rsock, &msg, sizeof(xid),0);
			continue;
		}
		rovr->gotit = 1;

		/* Now receive the reply */
		
		iov.iov_base=rovr->buf;
		iov.iov_len=rovr->len;
		
		result = rpc_recvmsg(rsock, &msg, rovr->len, 0);

		/* If this is not for ourselves, wake up the caller */
		if (rovr != slot)
			wake_up(&rovr->wait);
	} while (rovr != slot);

	/* This is somewhat tricky. We rely on the fact that we are able to
	 * remove ourselves from the queues before the next reader is scheduled,
	 * otherwise it would find that we're still at the head of the queue
	 * and go to sleep again.
	 */
	if (rsock->head == slot && slot->next != NULL)
		wake_up(&slot->next->wait);

	return result;
}

/*
 * Generic RPC call routine. This handles retries and timeouts etc pp
 */
int
rpc_call(struct rpc_sock *rsock, struct sockaddr *sap, int addrlen,
		const int *sndbuf, int slen, int *rcvbuf, int rlen,
		struct rpc_timeout *strategy, int flag)
{
	struct rpc_wait		*slot;
	int			result, retries;
	unsigned long		timeout;

	timeout = strategy->init_timeout;
	retries = 0;
	slot = NULL;

	do {
		dprintk("RPC call TP1\n");
		current->timeout = jiffies + timeout;
		if (slot == NULL) {
			while ((slot = rsock->free) == NULL) {
				if (!flag) {
					current->timeout = 0;
					return -ENOBUFS;
				}
				interruptible_sleep_on(&rsock->backlog);
				if (current->timeout == 0) {
					result = -ETIMEDOUT;
					goto timedout;
				}
				if (rsock->shutdown) {
					printk("RPC: aborting call due to shutdown.\n");
					current->timeout = 0;
					return -EIO;
				}
			}
			dprintk("RPC call TP2\n");
			slot->gotit = 0;
			slot->xid = *(u32 *)sndbuf;
			slot->buf = rcvbuf;
			slot->len = rlen;
			rsock->free = slot->next;
			rpc_insque(rsock, slot);
		}

		dprintk("RPC call TP3\n");
		result = rpc_call_one(rsock, slot, sap, addrlen,
					sndbuf, slen, rcvbuf, rlen);
		if (result != -ETIMEDOUT)
			break;

timedout:
		dprintk("RPC call TP4\n");
		dprintk("RPC: rpc_call_one returned timeout.\n");
		if (strategy->exponential)
			timeout <<= 1;
		else
			timeout += strategy->increment;
		if (strategy->max_timeout && timeout >= strategy->max_timeout)
			timeout = strategy->max_timeout;
		if (strategy->retries && ++retries >= strategy->retries)
			break;
	} while (1);

	dprintk("RPC call TP5\n");
	current->timeout = 0;
	if (slot != NULL) {
		dprintk("RPC call TP6\n");
		rpc_remque(rsock, slot);
		slot->next = rsock->free;
		rsock->free = slot;

		/* wake up tasks that haven't sent anything yet. (Waking
		 * up the first one on the wait queue would be enough) */
		if (rsock->backlog)
			wake_up(&rsock->backlog);
	}

	if (rsock->shutdown)
		wake_up(&rsock->shutwait);

	return result;
}

struct rpc_sock *
rpc_makesock(struct file *file)
{
	struct rpc_sock	*rsock;
	struct rpc_wait	*slot;
	int		i;

	dprintk("RPC: make RPC socket...\n");
	if ((rsock = kmalloc(sizeof(struct rpc_sock), GFP_KERNEL)) == NULL)
		return NULL;
	memset(rsock, 0, sizeof(*rsock)); /* Nnnngh! */

	rsock->sock = &file->f_inode->u.socket_i;
	rsock->file = file;

	rsock->free = rsock->waiting;
	for (i = 0, slot = rsock->waiting; i < NRREQS-1; i++, slot++)
		slot->next = slot + 1;
	slot->next = NULL;

	/* --- taken care of by memset above ---
	rsock->backlog = NULL;
	rsock->head = rsock->tail = NULL;

	rsock->shutwait = NULL;
	rsock->shutdown = 0;
	 */

	dprintk("RPC: made socket %08lx", (long) rsock);
	return rsock;
}

int
rpc_closesock(struct rpc_sock *rsock)
{
	unsigned long	t0 = jiffies;

	rsock->shutdown = 1;
	while (rsock->head || rsock->backlog) {
		interruptible_sleep_on(&rsock->shutwait);
		if (current->signal & ~current->blocked)
			return -EINTR;
#if 1
		if (t0 && t0 - jiffies > 60 * HZ) {
			printk("RPC: hanging in rpc_closesock.\n");
			t0 = 0;
		}
#endif
	}

	kfree(rsock);
	return 0;
}

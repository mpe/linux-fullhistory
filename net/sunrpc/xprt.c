/*
 *  linux/net/sunrpc/xprt.c
 *
 *  This is a generic RPC call interface supporting congestion avoidance,
 *  and asynchronous calls.
 *
 *  The interface works like this:
 *
 *  -	When a process places a call, it allocates a request slot if
 *	one is available. Otherwise, it sleeps on the backlog queue
 *	(xprt_reserve).
 *  -	Next, the caller puts together the RPC message, stuffs it into
 *	the request struct, and calls xprt_call().
 *  -	xprt_call transmits the message and installs the caller on the
 *	socket's wait list. At the same time, it installs a timer that
 *	is run after the packet's timeout has expired.
 *  -	When a packet arrives, the data_ready handler walks the list of
 *	pending requests for that socket. If a matching XID is found, the
 *	caller is woken up, and the timer removed.
 *  -	When no reply arrives within the timeout interval, the timer is
 *	fired by the kernel and runs xprt_timer(). It either adjusts the
 *	timeout values (minor timeout) or wakes up the caller with a status
 *	of -ETIMEDOUT.
 *  -	When the caller receives a notification from RPC that a reply arrived,
 *	it should release the RPC slot, and process the reply.
 *	If the call timed out, it may choose to retry the operation by
 *	adjusting the initial timeout value, and simply calling rpc_call
 *	again.
 *
 *  Support for async RPC is done through a set of RPC-specific scheduling
 *  primitives that `transparently' work for processes as well as async
 *  tasks that rely on callbacks.
 *
 *  Copyright (C) 1995, 1996, Olaf Kirch <okir@monad.swb.de>
 *
 *  TCP callback races fixes (C) 1998 Red Hat Software <alan@redhat.com>
 *  TCP send fixes (C) 1998 Red Hat Software <alan@redhat.com>
 *  TCP NFS related read + write fixes
 *   (C) 1999 Dave Airlie, University of Limerick, Ireland <airlied@linux.ie>
 */

#define __KERNEL_SYSCALLS__

#include <linux/version.h>
#include <linux/types.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/udp.h>
#include <linux/unistd.h>
#include <linux/sunrpc/clnt.h>
#include <linux/file.h>

#include <net/sock.h>

#include <asm/uaccess.h>

#define SOCK_HAS_USER_DATA

/*
 * Local variables
 */
#ifndef SOCK_HAS_USER_DATA
static struct rpc_xprt *	sock_list = NULL;
#endif

#ifdef RPC_DEBUG
# undef  RPC_DEBUG_DATA
# define RPCDBG_FACILITY	RPCDBG_XPRT
#endif

#ifndef MAX
# define MAX(a, b)	((a) > (b)? (a) : (b))
# define MIN(a, b)	((a) < (b)? (a) : (b))
#endif

/*
 * Local functions
 */
static void	xprt_request_init(struct rpc_task *, struct rpc_xprt *);
static void	xprt_transmit_status(struct rpc_task *task);
static void	xprt_receive_status(struct rpc_task *task);
static void	xprt_reserve_status(struct rpc_task *task);
static void	xprt_reconn_timeout(struct rpc_task *task);
static void	xprt_reconn_status(struct rpc_task *task);
static struct socket *xprt_create_socket(int, struct sockaddr_in *,
					struct rpc_timeout *);

#ifdef RPC_DEBUG_DATA
/*
 * Print the buffer contents (first 128 bytes only--just enough for
 * diropres return).
 */
static void
xprt_pktdump(char *msg, u32 *packet, unsigned int count)
{
	u8	*buf = (u8 *) packet;
	int	j;

	dprintk("RPC:      %s\n", msg);
	for (j = 0; j < count && j < 128; j += 4) {
		if (!(j & 31)) {
			if (j)
				dprintk("\n");
			dprintk("0x%04x ", j);
		}
		dprintk("%02x%02x%02x%02x ",
			buf[j], buf[j+1], buf[j+2], buf[j+3]);
	}
	dprintk("\n");
}
#else
static inline void
xprt_pktdump(char *msg, u32 *packet, unsigned int count)
{
	/* NOP */
}
#endif

/*
 * Look up RPC transport given an INET socket
 */
static inline struct rpc_xprt *
xprt_from_sock(struct sock *sk)
{
#ifndef SOCK_HAS_USER_DATA
	struct rpc_xprt		*xprt;

	for (xprt = sock_list; xprt && sk != xprt->inet; xprt = xprt->link)
		;
	return xprt;
#else
	return (struct rpc_xprt *) sk->user_data;
#endif
}

/*
 *	Adjust the iovec to move on 'n' bytes
 */
 
extern inline void xprt_move_iov(struct msghdr *msg, struct iovec *niv, int amount)
{
	struct iovec *iv=msg->msg_iov;
	
	/*
	 *	Eat any sent iovecs
	 */

	while(iv->iov_len < amount)
	{
		amount-=iv->iov_len;
		iv++;
		msg->msg_iovlen--;
	}
	
	msg->msg_iov=niv;
	
	/*
	 *	And chew down the partial one
	 */

	niv[0].iov_len = iv->iov_len-amount;
	niv[0].iov_base =((unsigned char *)iv->iov_base)+amount;
	iv++;
	
	/*
	 *	And copy any others
	 */
	 
	for(amount=1;amount<msg->msg_iovlen; amount++)
	{
		niv[amount]=*iv++;
	}
}
 
/*
 * Write data to socket.
 */

static inline int
xprt_sendmsg(struct rpc_xprt *xprt)
{
	struct socket	*sock = xprt->sock;
	struct msghdr	msg;
	mm_segment_t	oldfs;
	int		result;
	struct iovec	niv[MAX_IOVEC];

	xprt_pktdump("packet data:",
				xprt->snd_buf.io_vec->iov_base,
				xprt->snd_buf.io_vec->iov_len);

	msg.msg_flags   = MSG_DONTWAIT;
	msg.msg_iov	= xprt->snd_buf.io_vec;
	msg.msg_iovlen	= xprt->snd_buf.io_nr;
	msg.msg_name	= (struct sockaddr *) &xprt->addr;
	msg.msg_namelen = sizeof(xprt->addr);
	msg.msg_control = NULL;

	/* Dont repeat bytes */
	
	if(xprt->snd_sent)
		xprt_move_iov(&msg, niv, xprt->snd_sent);
		
	oldfs = get_fs(); set_fs(get_ds());
	result = sock_sendmsg(sock, &msg, xprt->snd_buf.io_len);
	set_fs(oldfs);

	dprintk("RPC:      xprt_sendmsg(%d) = %d\n",
				xprt->snd_buf.io_len, result);

	if (result >= 0) {
		xprt->snd_buf.io_len -= result;
		xprt->snd_sent += result;
		return result;
	}

	switch (result) {
	case -ECONNREFUSED:
		/* When the server has died, an ICMP port unreachable message
		 * prompts ECONNREFUSED.
		 */
		break;
	case -EAGAIN:
		return 0;
	case -ENOTCONN: case -EPIPE:
		/* connection broken */
		break;
	default:
		printk(KERN_NOTICE "RPC: sendmsg returned error %d\n", -result);
		result = 0;
	}
	return result;
}

/*
 * Read data from socket
 */
static inline int
xprt_recvmsg(struct rpc_xprt *xprt, struct iovec *iov, int nr, int len)
{
	struct socket	*sock = xprt->sock;
	struct sockaddr_in sin;
	struct msghdr	msg;
	mm_segment_t	oldfs;
	int		result;

#if LINUX_VERSION_CODE >= 0x020100
	msg.msg_flags   = MSG_DONTWAIT;
	msg.msg_iov	= iov;
	msg.msg_iovlen	= nr;
	msg.msg_name	= &sin;
	msg.msg_namelen = sizeof(sin);
	msg.msg_control = NULL;

	oldfs = get_fs(); set_fs(get_ds());
	result = sock_recvmsg(sock, &msg, len, MSG_DONTWAIT);
	set_fs(oldfs);
#else
	int		alen = sizeof(sin);
	msg.msg_flags   = 0;
	msg.msg_iov	= iov;
	msg.msg_iovlen	= nr;
	msg.msg_name	= &sin;
	msg.msg_namelen = sizeof(sin);
	msg.msg_control = NULL;

	oldfs = get_fs(); set_fs(get_ds());
	result = sock->ops->recvmsg(sock, &msg, len, 1, 0, &alen);
	set_fs(oldfs);
#endif

	dprintk("RPC:      xprt_recvmsg(iov %p, len %d) = %d\n",
						iov, len, result);
	return result;
}


/*
 * Adjust RPC congestion window
 * We use a time-smoothed congestion estimator to avoid heavy oscillation.
 */
static void
xprt_adjust_cwnd(struct rpc_xprt *xprt, int result)
{
	unsigned long	cwnd = xprt->cwnd;

	if (xprt->nocong)
		return;
	if (result >= 0) {
		if (xprt->cong < cwnd || time_before(jiffies, xprt->congtime))
			return;
		/* The (cwnd >> 1) term makes sure
		 * the result gets rounded properly. */
		cwnd += (RPC_CWNDSCALE * RPC_CWNDSCALE + (cwnd >> 1)) / cwnd;
		if (cwnd > RPC_MAXCWND)
			cwnd = RPC_MAXCWND;
		else
			pprintk("RPC: %lu %ld cwnd\n", jiffies, cwnd);
		xprt->congtime = jiffies + ((cwnd * HZ) << 2) / RPC_CWNDSCALE;
		dprintk("RPC:      cong %08lx, cwnd was %08lx, now %08lx, "
			"time %ld ms\n", xprt->cong, xprt->cwnd, cwnd,
			(xprt->congtime-jiffies)*1000/HZ);
	} else if (result == -ETIMEDOUT) {
		if ((cwnd >>= 1) < RPC_CWNDSCALE)
			cwnd = RPC_CWNDSCALE;
		xprt->congtime = jiffies + ((cwnd * HZ) << 3) / RPC_CWNDSCALE;
		dprintk("RPC:      cong %ld, cwnd was %ld, now %ld, "
			"time %ld ms\n", xprt->cong, xprt->cwnd, cwnd,
			(xprt->congtime-jiffies)*1000/HZ);
		pprintk("RPC: %lu %ld cwnd\n", jiffies, cwnd);
	}

	xprt->cwnd = cwnd;
}

/*
 * Adjust timeout values etc for next retransmit
 */
int
xprt_adjust_timeout(struct rpc_timeout *to)
{
	if (to->to_exponential)
		to->to_current <<= 1;
	else
		to->to_current += to->to_increment;
	if (to->to_maxval && to->to_current >= to->to_maxval) {
		to->to_current = to->to_maxval;
		to->to_retries = 0;
	}
	if (!to->to_current) {
		printk(KERN_WARNING "xprt_adjust_timeout: to_current = 0!\n");
		to->to_current = 5 * HZ;
	}
	pprintk("RPC: %lu %s\n", jiffies,
			to->to_retries? "retrans" : "timeout");
	return (to->to_retries)--;
}

/*
 * Close down a transport socket
 */
static void
xprt_close(struct rpc_xprt *xprt)
{
	struct sock	*sk = xprt->inet;

#ifdef SOCK_HAS_USER_DATA
	sk->user_data    = NULL;
#endif
	sk->data_ready   = xprt->old_data_ready;
	sk->state_change = xprt->old_state_change;
	sk->write_space  = xprt->old_write_space;

	if (xprt->file)
		fput(xprt->file);
	else
		sock_release(xprt->sock);
	/*
	 *	TCP doesnt require the rpciod now - other things may
	 *	but rpciod handles that not us.
	 */
	if(xprt->stream && !xprt->connecting)
		rpciod_down();
}

/*
 * Mark a transport as disconnected
 */
static void
xprt_disconnect(struct rpc_xprt *xprt)
{
	dprintk("RPC:      disconnected transport %p\n", xprt);
	rpc_wake_up_status(&xprt->pending, -ENOTCONN);
	rpc_wake_up_status(&xprt->sending, -ENOTCONN);
	xprt->connected = 0;
}

/*
 * Reconnect a broken TCP connection.
 */
void
xprt_reconnect(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;
	struct socket	*sock;
	struct sock	*inet;
	int		status;

	dprintk("RPC: %4d xprt_reconnect %p connected %d\n",
				task->tk_pid, xprt, xprt->connected);
	task->tk_status = 0;

	if (xprt->connecting) {
		task->tk_timeout = xprt->timeout.to_maxval;
		rpc_sleep_on(&xprt->reconn, task, NULL, NULL);
		return;
	}
	xprt->connecting = 1;

	/* Create an unconnected socket */
	if (!(sock = xprt_create_socket(xprt->prot, NULL, &xprt->timeout)))
		goto defer;

#if LINUX_VERSION_CODE >= 0x020100
	inet = sock->sk;
#else
	inet = (struct sock *) sock->data;
#endif
	inet->data_ready   = xprt->inet->data_ready;
	inet->state_change = xprt->inet->state_change;
	inet->write_space  = xprt->inet->write_space;
#ifdef SOCK_HAS_USER_DATA
	inet->user_data    = xprt;
#endif

	dprintk("RPC: %4d closing old socket\n", task->tk_pid);
	xprt_disconnect(xprt);
	xprt_close(xprt);

	/* Reset to new socket and default congestion */
	xprt->sock = sock;
	xprt->inet = inet;
	xprt->cwnd = RPC_INITCWND;

	/* Now connect it asynchronously. */
	dprintk("RPC: %4d connecting new socket\n", task->tk_pid);
	status = sock->ops->connect(sock, (struct sockaddr *) &xprt->addr,
				sizeof(xprt->addr), O_NONBLOCK);
	if (status < 0) {
		if (status != -EINPROGRESS && status != -EALREADY) {
			printk("RPC: TCP connect error %d!\n", -status);
			goto defer;
		}

		dprintk("RPC: %4d connect status %d connected %d\n",
				task->tk_pid, status, xprt->connected);
		task->tk_timeout = 60 * HZ;

		start_bh_atomic();
		if (!xprt->connected) {
			rpc_sleep_on(&xprt->reconn, task,
				xprt_reconn_status, xprt_reconn_timeout);
			end_bh_atomic();
			return;
		}
		end_bh_atomic();
	}

	xprt->connecting = 0;
	rpc_wake_up(&xprt->reconn);
	return;

defer:
	task->tk_timeout = 30 * HZ;
	rpc_sleep_on(&xprt->reconn, task, NULL, NULL);
	xprt->connecting = 0;
}

/*
 * Reconnect status
 */
static void
xprt_reconn_status(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;

	dprintk("RPC: %4d xprt_reconn_status %d\n",
				task->tk_pid, task->tk_status);
	if (!xprt->connected && task->tk_status != -ETIMEDOUT) {
		task->tk_timeout = 30 * HZ;
		rpc_sleep_on(&xprt->reconn, task, NULL, xprt_reconn_timeout);
	}
}

/*
 * Reconnect timeout. We just mark the transport as not being in the
 * process of reconnecting, and leave the rest to the upper layers.
 */
static void
xprt_reconn_timeout(struct rpc_task *task)
{
	dprintk("RPC: %4d xprt_reconn_timeout %d\n",
				task->tk_pid, task->tk_status);
	task->tk_status = -ENOTCONN;
	task->tk_xprt->connecting = 0;
	task->tk_timeout = 0;
	rpc_wake_up_task(task);
}

/*
 * Look up the RPC request corresponding to a reply.
 */
static inline struct rpc_rqst *
xprt_lookup_rqst(struct rpc_xprt *xprt, u32 xid)
{
	struct rpc_task	*head, *task;
	struct rpc_rqst	*req;
	int		safe = 0;

	if ((head = xprt->pending.task) != NULL) {
		task = head;
		do {
			if ((req = task->tk_rqstp) && req->rq_xid == xid)
				return req;
			task = task->tk_next;
			if (++safe > 100) {
				printk("xprt_lookup_rqst: loop in Q!\n");
				return NULL;
			}
		} while (task != head);
	}
	dprintk("RPC:      unknown XID %08x in reply.\n", xid);
	return NULL;
}

/*
 * Complete reply received.
 * The TCP code relies on us to remove the request from xprt->pending.
 */
static inline void
xprt_complete_rqst(struct rpc_xprt *xprt, struct rpc_rqst *req, int copied)
{
	struct rpc_task	*task = req->rq_task;

	req->rq_rlen   = copied;
	req->rq_gotit  = 1;

	/* Adjust congestion window */
	xprt_adjust_cwnd(xprt, copied);

#ifdef RPC_PROFILE
	/* Profile only reads for now */
	if (copied > 1024) {
		static unsigned long	nextstat = 0;
		static unsigned long	pkt_rtt = 0, pkt_len = 0, pkt_cnt = 0;

		pkt_cnt++;
		pkt_len += req->rq_slen + copied;
		pkt_rtt += jiffies - req->rq_xtime;
		if (time_before(nextstat, jiffies)) {
			printk("RPC: %lu %ld cwnd\n", jiffies, xprt->cwnd);
			printk("RPC: %ld %ld %ld %ld stat\n",
					jiffies, pkt_cnt, pkt_len, pkt_rtt);
			pkt_rtt = pkt_len = pkt_cnt = 0;
			nextstat = jiffies + 5 * HZ;
		}
	}
#endif

	/* ... and wake up the process. */
	dprintk("RPC: %4d has input (%d bytes)\n", task->tk_pid, copied);
	task->tk_status = copied;

	rpc_wake_up_task(task);
	return;
}

/*
 * Input handler for RPC replies. Called from a bottom half and hence
 * atomic.
 */
static inline void
udp_data_ready(struct sock *sk, int len)
{
	struct rpc_task	*task;
	struct rpc_xprt	*xprt;
	struct rpc_rqst *rovr;
	struct sk_buff	*skb;
	struct iovec	iov[MAX_IOVEC];
	int		err, repsize, copied;

	dprintk("RPC:      udp_data_ready...\n");
	if (!(xprt = xprt_from_sock(sk)))
		return;
	dprintk("RPC:      udp_data_ready client %p\n", xprt);

	if ((skb = skb_recv_datagram(sk, 0, 1, &err)) == NULL)
		return;
	repsize = skb->len - 8;	/* don't account for UDP header */

	if (repsize < 4) {
		printk("RPC: impossible RPC reply size %d!\n", repsize);
		goto dropit;
	}

	/* Look up the request corresponding to the given XID */
	if (!(rovr = xprt_lookup_rqst(xprt, *(u32 *) (skb->h.raw + 8))))
		goto dropit;
	task = rovr->rq_task;

	dprintk("RPC: %4d received reply\n", task->tk_pid);
	xprt_pktdump("packet data:", (u32 *) (skb->h.raw+8), repsize);

	if ((copied = rovr->rq_rlen) > repsize)
		copied = repsize;

	/* Okay, we have it. Copy datagram... */
	memcpy(iov, rovr->rq_rvec, rovr->rq_rnr * sizeof(iov[0]));
	/* This needs to stay tied with the usermode skb_copy_dagram... */
	memcpy_tokerneliovec(iov, skb->data+8, copied);

	xprt_complete_rqst(xprt, rovr, copied);

dropit:
	skb_free_datagram(sk, skb);
	return;
}

/*
 * TCP record receive routine
 * This is not the most efficient code since we call recvfrom twice--
 * first receiving the record marker and XID, then the data.
 * 
 * The optimal solution would be a RPC support in the TCP layer, which
 * would gather all data up to the next record marker and then pass us
 * the list of all TCP segments ready to be copied.
 */
static inline int
tcp_input_record(struct rpc_xprt *xprt)
{
	struct rpc_rqst	*req;
	struct iovec	*iov;
	struct iovec	riov;
	u32		offset;
	int		result, maxcpy, reclen, avail, want;

	dprintk("RPC:      tcp_input_record\n");
	offset = xprt->tcp_offset;
	result = -EAGAIN;
	if (offset < 4 || (!xprt->tcp_more && offset < 8)) {
		want = (xprt->tcp_more? 4 : 8) - offset;
		dprintk("RPC:      reading header (%d bytes)\n", want);
		riov.iov_base = xprt->tcp_recm.data + offset;
		riov.iov_len  = want;
		result = xprt_recvmsg(xprt, &riov, 1, want);
		if (!result)
		{
			dprintk("RPC: empty TCP record.\n");
			return -ENOTCONN;
		}
		if (result < 0)
			goto done;
		offset += result;
		if (result < want) {
			result = -EAGAIN;
			goto done;
		}

		/* Get the record length and mask out the more_fragments bit */
		reclen = ntohl(xprt->tcp_reclen);
		dprintk("RPC:      reclen %08x\n", reclen);
		xprt->tcp_more = (reclen & 0x80000000)? 0 : 1;
		reclen &= 0x7fffffff;
		xprt->tcp_total += reclen;
		xprt->tcp_reclen = reclen;

		dprintk("RPC:      got xid %08x reclen %d morefrags %d\n",
			xprt->tcp_xid, xprt->tcp_reclen, xprt->tcp_more);
		if (!xprt->tcp_copied
		 && (req = xprt_lookup_rqst(xprt, xprt->tcp_xid))) {
			iov = xprt->tcp_iovec;
			memcpy(iov, req->rq_rvec, req->rq_rnr * sizeof(iov[0]));
#if 0
*(u32 *)iov->iov_base = req->rq_xid;
#endif
			iov->iov_base += 4;
			iov->iov_len  -= 4;
			xprt->tcp_copied = 4;
			xprt->tcp_rqstp  = req;
		}
	} else {
		reclen = xprt->tcp_reclen;
	}

	avail = reclen - (offset - 4);
	if ((req = xprt->tcp_rqstp) && req->rq_xid == xprt->tcp_xid
	 && req->rq_task->tk_rpcwait == &xprt->pending) {
		want = MIN(req->rq_rlen - xprt->tcp_copied, avail);

		dprintk("RPC: %4d TCP receiving %d bytes\n",
					req->rq_task->tk_pid, want);
		result = xprt_recvmsg(xprt, xprt->tcp_iovec, req->rq_rnr, want);
		if (!result && want)
			result = -EAGAIN;
		if (result < 0)
			goto done;
		xprt->tcp_copied += result;
		offset += result;
		avail  -= result;
		if (result < want) {
			result = -EAGAIN;
			goto done;
		}

		maxcpy = MIN(req->rq_rlen, xprt->tcp_total);
		if (xprt->tcp_copied == maxcpy && !xprt->tcp_more) {
			dprintk("RPC: %4d received reply complete\n",
					req->rq_task->tk_pid);
			xprt_complete_rqst(xprt, req, xprt->tcp_total);
			xprt->tcp_copied = 0;
			xprt->tcp_rqstp  = NULL;
		}
		/* Request must be re-encoded before retransmit */
		req->rq_damaged = 1;
	}

	/* Skip over any trailing bytes on short reads */
	while (avail) {
		static u8	dummy[64];

		want = MIN(avail, sizeof(dummy));
		riov.iov_base = dummy;
		riov.iov_len  = want;
		dprintk("RPC:      TCP skipping %d bytes\n", want);
		result = xprt_recvmsg(xprt, &riov, 1, want);
		if (!result && want)
			result=-EAGAIN;
		if (result < 0)
			goto done;
		offset += result;
		avail  -= result;
		if (result < want) {
			result = -EAGAIN;
			goto done;
		}
	}
	if (!xprt->tcp_more)
		xprt->tcp_total = 0;
	offset = 0;

done:
	dprintk("RPC:      tcp_input_record done (off %d total %d copied %d)\n",
			offset, xprt->tcp_total, xprt->tcp_copied);
	xprt->tcp_offset = offset;
	return result;
}

/*
 *	TCP task queue stuff
 */
 
static struct rpc_xprt *rpc_xprt_pending = NULL;	/* Chain by rx_pending of rpc_xprt's */

/*
 *	This is protected from tcp_data_ready and the stack as its run
 *	inside of the RPC I/O daemon
 */

void rpciod_tcp_dispatcher(void)
{
	struct rpc_xprt *xprt;
	int result;

	dprintk("rpciod_tcp_dispatcher: Queue Running\n");
	
	/*
	 *	Empty each pending socket
	 */
	 
	while((xprt=rpc_xprt_pending)!=NULL)
	{
		int safe_retry=0;
		
		rpc_xprt_pending=xprt->rx_pending;
		xprt->rx_pending_flag=0;
		
		dprintk("rpciod_tcp_dispatcher: Processing %p\n", xprt);
		
		do 
		{
			if (safe_retry++ > 50)
				break;
			result = tcp_input_record(xprt);
		}
		while (result >= 0);
	
		switch (result) {
			case -EAGAIN:
				continue;
			case -ENOTCONN:
			case -EPIPE:
				xprt_disconnect(xprt);
				continue;
			default:
				printk(KERN_WARNING "RPC: unexpected error %d from tcp_input_record\n",
					result);
		}
	}
}


extern inline void tcp_rpciod_queue(void)
{
	rpciod_wake_up();
}

/*
 *	data_ready callback for TCP. We can't just jump into the
 *	tcp recvmsg functions inside of the network receive bh or
 * 	bad things occur. We queue it to pick up after networking
 *	is done.
 */
 
static void tcp_data_ready(struct sock *sk, int len)
{
	struct rpc_xprt	*xprt;

	dprintk("RPC:      tcp_data_ready...\n");
	if (!(xprt = xprt_from_sock(sk)))
	{
		printk("Not a socket with xprt %p\n", sk);
		return;
	}
	dprintk("RPC:      tcp_data_ready client %p\n", xprt);
	dprintk("RPC:      state %x conn %d dead %d zapped %d\n",
				sk->state, xprt->connected,
				sk->dead, sk->zapped);
	/*
	 *	If we are not waiting for the RPC bh run then
	 *	we are now
	 */
	if (!xprt->rx_pending_flag)
	{
		int start_queue=0;

		dprintk("RPC:     xprt queue %p\n", rpc_xprt_pending);
		if(rpc_xprt_pending==NULL)
			start_queue=1;
		xprt->rx_pending_flag=1;
		xprt->rx_pending=rpc_xprt_pending;
		rpc_xprt_pending=xprt;
		if (start_queue)
		  {
		    tcp_rpciod_queue();
		    start_queue=0;
		  }
	}
	else
		dprintk("RPC:     xprt queued already %p\n", xprt);
}


static void
tcp_state_change(struct sock *sk)
{
	struct rpc_xprt	*xprt;

	if (!(xprt = xprt_from_sock(sk)))
		return;
	dprintk("RPC:      tcp_state_change client %p...\n", xprt);
	dprintk("RPC:      state %x conn %d dead %d zapped %d\n",
				sk->state, xprt->connected,
				sk->dead, sk->zapped);

	if (sk->state == TCP_ESTABLISHED && !xprt->connected) {
		xprt->connected = 1;
		xprt->connecting = 0;
		rpc_wake_up(&xprt->reconn);
	} else if (sk->zapped) {
		rpc_wake_up_status(&xprt->pending, -ENOTCONN);
		rpc_wake_up_status(&xprt->sending, -ENOTCONN);
		rpc_wake_up_status(&xprt->reconn,  -ENOTCONN);
	}
}

static void
tcp_write_space(struct sock *sk)
{
	struct rpc_xprt	*xprt;

	if (!(xprt = xprt_from_sock(sk)))
		return;
	if(xprt->snd_sent && xprt->snd_task)
		dprintk("RPC: write space\n");
	if(xprt->write_space == 0)
	{
		xprt->write_space = 1;
		if (xprt->snd_task && !RPC_IS_RUNNING(xprt->snd_task))
		{
			if(xprt->snd_sent)
				dprintk("RPC: Write wakeup snd_sent =%d\n",
					xprt->snd_sent);
			rpc_wake_up_task(xprt->snd_task);			
		}
	}
}

/*
 * RPC receive timeout handler.
 */
static void
xprt_timer(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;

	if (req) {
		xprt_adjust_cwnd(task->tk_xprt, -ETIMEDOUT);
	}

	dprintk("RPC: %4d xprt_timer (%s request)\n",
		task->tk_pid, req ? "pending" : "backlogged");

	task->tk_status  = -ETIMEDOUT;
	task->tk_timeout = 0;
	rpc_wake_up_task(task);
}

/*
 * (Partly) transmit the RPC packet
 * Note that task->tk_status is either 0 or negative on return.
 * Only when the reply is received will the status be set to a
 * positive value.
 */
static inline int
xprt_transmit_some(struct rpc_xprt *xprt, struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	int		result;

	task->tk_status = 0;
	if ((result = xprt_sendmsg(xprt)) >= 0) {
		if (!xprt->snd_buf.io_len || !xprt->stream) {
			rpc_wake_up_next(&xprt->sending);
			return req->rq_slen;
		}
		result = -EAGAIN;
	} else if (xprt->stream) {
		if (result == -ENOTCONN || result == -EPIPE) {
			xprt_disconnect(xprt);
			result = -ENOTCONN;
		}
	}
	return task->tk_status = result;
}

/*
 * Place the actual RPC call.
 * We have to copy the iovec because sendmsg fiddles with its contents.
 */
void
xprt_transmit(struct rpc_task *task)
{
	struct rpc_timeout *timeo;
	struct rpc_rqst	*req = task->tk_rqstp;
	struct rpc_xprt	*xprt = req->rq_xprt;
	int status;

	dprintk("RPC: %4d xprt_transmit(%x)\n", task->tk_pid, 
				*(u32 *)(req->rq_svec[0].iov_base));

	if (xprt->shutdown) {
		task->tk_status = -EIO;
		return;
	}

	/* If we're not already in the process of transmitting our call,
	 * set up everything as needed. */
	if (xprt->snd_task != task) {
		/* Write the record marker */
		if (xprt->stream) {
			u32	marker;

			if (!xprt->connected) {
				task->tk_status = -ENOTCONN;
				return;
			}
			marker = htonl(0x80000000|(req->rq_slen-4));
			*((u32 *) req->rq_svec[0].iov_base) = marker;
		}

		/* Reset timeout parameters */
		timeo = &req->rq_timeout;
		if (timeo->to_retries < 0) {
			dprintk("RPC: %4d xprt_transmit reset timeo\n",
						task->tk_pid);
			timeo->to_retries = xprt->timeout.to_retries;
			timeo->to_current = timeo->to_initval;
		}

#ifdef RPC_PROFILE
		req->rq_xtime = jiffies;
#endif
		req->rq_gotit = 0;

		if (xprt->snd_task) {
			dprintk("RPC: %4d TCP write queue full (task %d)\n",
					task->tk_pid, xprt->snd_task->tk_pid);
			rpc_sleep_on(&xprt->sending, task,
					xprt_transmit_status, NULL);
			return;
		}
		xprt->snd_buf  = req->rq_snd_buf;
		xprt->snd_task = task;
		xprt->snd_sent = 0;
	}

	/* For fast networks/servers we have to put the request on
	 * the pending list now:
	 */
	start_bh_atomic();
	status = rpc_add_wait_queue(&xprt->pending, task);
	if (!status)
		task->tk_callback = NULL;
	end_bh_atomic();

	if (status)
	{
		printk(KERN_WARNING "RPC: failed to add task to queue: error: %d!\n", status);
		task->tk_status = status;
		return;
	}

	/* Continue transmitting the packet/record. We must be careful
	 * to cope with writespace callbacks arriving _after_ we have
	 * called xprt_sendmsg().
	 */
	while (1) {
		xprt->write_space = 0;
		if (xprt_transmit_some(xprt, task) != -EAGAIN) {
			dprintk("RPC: %4d xmit complete\n", task->tk_pid);
			xprt->snd_task = NULL;
			return;
		}

		/*d*/dprintk("RPC: %4d xmit incomplete (%d left of %d)\n",
				task->tk_pid, xprt->snd_buf.io_len,
				req->rq_slen);
		task->tk_status = 0;
		start_bh_atomic();
		if (!xprt->write_space) {
			/* Remove from pending */
			rpc_remove_wait_queue(task);
			rpc_sleep_on(&xprt->sending, task,
					xprt_transmit_status, NULL);
			end_bh_atomic();
			return;
		}
		end_bh_atomic();
	}
}

/*
 * This callback is invoked when the sending task is forced to sleep
 * because the TCP write buffers are full
 */
static void
xprt_transmit_status(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_client->cl_xprt;

	dprintk("RPC: %4d transmit_status %d\n", task->tk_pid, task->tk_status);
	if (xprt->snd_task == task) 
	{
		if (task->tk_status < 0)
		{
			xprt->snd_task = NULL;
			xprt_disconnect(xprt);
		}
		else
			xprt_transmit(task);
	}
}

/*
 * Wait for the reply to our call.
 * When the callback is invoked, the congestion window should have
 * been updated already.
 */
void
xprt_receive(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	struct rpc_xprt	*xprt = req->rq_xprt;

	dprintk("RPC: %4d xprt_receive\n", task->tk_pid);
	if (xprt->connected == 0) {
		task->tk_status = -ENOTCONN;
		return;
	}

	/*
	 * Wait until rq_gotit goes non-null, or timeout elapsed.
	 */
	task->tk_timeout = req->rq_timeout.to_current;

	start_bh_atomic();
	if (!req->rq_gotit) {
		rpc_sleep_on(&xprt->pending, task,
				xprt_receive_status, xprt_timer);
	}
	end_bh_atomic();

	dprintk("RPC: %4d xprt_receive returns %d\n",
				task->tk_pid, task->tk_status);
}

static void
xprt_receive_status(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;

	if (xprt->stream && xprt->tcp_rqstp == task->tk_rqstp)
		xprt->tcp_rqstp = NULL;
}

/*
 * Reserve an RPC call slot.
 */
int
xprt_reserve(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;

	/* We already have an initialized request. */
	if (task->tk_rqstp)
		return 0;

	dprintk("RPC: %4d xprt_reserve cong = %ld cwnd = %ld\n",
				task->tk_pid, xprt->cong, xprt->cwnd);
	if ((!RPCXPRT_CONGESTED(xprt) && xprt->free)) {
		xprt_reserve_status(task);
		task->tk_timeout = 0;
	} else if (!task->tk_timeout) {
		task->tk_status = -ENOBUFS;
	} else {
		dprintk("RPC:      xprt_reserve waiting on backlog\n");
		rpc_sleep_on(&xprt->backlog, task, xprt_reserve_status, NULL);
	}
	dprintk("RPC: %4d xprt_reserve returns %d\n",
				task->tk_pid, task->tk_status);
	return task->tk_status;
}

/*
 * Reservation callback
 */
static void
xprt_reserve_status(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;
	struct rpc_rqst	*req;

	if (xprt->shutdown) {
		task->tk_status = -EIO;
	} else if (task->tk_status < 0) {
		/* NOP */
	} else if (task->tk_rqstp) {
		/* We've already been given a request slot: NOP */
	} else if (!RPCXPRT_CONGESTED(xprt)) {
		/* OK: There's room for us. Grab a free slot and bump
		 * congestion value */
		req = xprt->free;
		if (!req)
			goto bad_list;
		if (req->rq_xid)
			goto bad_used;
		xprt->free     = req->rq_next;
		xprt->cong    += RPC_CWNDSCALE;
		task->tk_rqstp = req;
		req->rq_next   = NULL;
		xprt_request_init(task, xprt);
	} else {
		task->tk_status = -EAGAIN;
	}

	if (xprt->free && !RPCXPRT_CONGESTED(xprt))
		rpc_wake_up_next(&xprt->backlog);

	return;

bad_list:
	printk(KERN_ERR 
		"RPC: %4d inconsistent free list (cong %ld cwnd %ld)\n",
		task->tk_pid, xprt->cong, xprt->cwnd);
	rpc_debug = ~0;
	goto bummer;
bad_used:
	printk(KERN_ERR "RPC: used rqst slot %p on free list!\n", req);
bummer:
	task->tk_status = -EIO;
	xprt->free = NULL;
	return;
}

/*
 * Initialize RPC request
 */
static void
xprt_request_init(struct rpc_task *task, struct rpc_xprt *xprt)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	static u32	xid = 0;

	if (!xid)
		xid = jiffies;

	dprintk("RPC: %4d reserved req %p xid %08x\n", task->tk_pid, req, xid);
	task->tk_status = 0;
	req->rq_gotit	= 0;
	req->rq_timeout = xprt->timeout;
	req->rq_task	= task;
	req->rq_xprt    = xprt;
	req->rq_xid     = xid++;
}

/*
 * Release an RPC call slot
 */
void
xprt_release(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;
	struct rpc_rqst	*req;

	if (!(req = task->tk_rqstp))
		return;
	task->tk_rqstp = NULL;
	memset(req, 0, sizeof(*req));	/* mark unused */

	dprintk("RPC: %4d release request %p\n", task->tk_pid, req);

	/* remove slot from queue of pending */
	start_bh_atomic();
	if (task->tk_rpcwait) {
		printk("RPC: task of released request still queued!\n");
#ifdef RPC_DEBUG
		printk("RPC: (task is on %s)\n", rpc_qname(task->tk_rpcwait));
#endif
		rpc_del_timer(task);
		rpc_remove_wait_queue(task);
	}
	end_bh_atomic();

	/* Decrease congestion value. */
	xprt->cong -= RPC_CWNDSCALE;

#if 0
	/* If congestion threshold is not yet reached, pass on the request slot.
	 * This looks kind of kludgy, but it guarantees backlogged requests
	 * are served in order.
	 * N.B. This doesn't look completely safe, as the task is still
	 * on the backlog list after wake-up.
	 */
	if (!RPCXPRT_CONGESTED(xprt)) {
		struct rpc_task	*next = rpc_wake_up_next(&xprt->backlog);

		if (next && next->tk_rqstp == 0) {
			xprt->cong += RPC_CWNDSCALE;
			next->tk_rqstp = req;
			xprt_request_init(next, xprt);
			return;
		}
	}
#endif

	req->rq_next = xprt->free;
	xprt->free   = req;

	/* If not congested, wake up the next backlogged process */
	if (!RPCXPRT_CONGESTED(xprt))
		rpc_wake_up_next(&xprt->backlog);
}

/*
 * Set default timeout parameters
 */
void
xprt_default_timeout(struct rpc_timeout *to, int proto)
{
	if (proto == IPPROTO_UDP)
		xprt_set_timeout(to, 5,  5 * HZ);
	else
		xprt_set_timeout(to, 5, 15 * HZ);
}

/*
 * Set constant timeout
 */
void
xprt_set_timeout(struct rpc_timeout *to, unsigned int retr, unsigned long incr)
{
	to->to_current   = 
	to->to_initval   = 
	to->to_increment = incr;
	to->to_maxval    = incr * retr;
	to->to_resrvval  = incr * retr;
	to->to_retries   = retr;
	to->to_exponential = 0;
}

/*
 * Initialize an RPC client
 */
static struct rpc_xprt *
xprt_setup(struct socket *sock, int proto,
			struct sockaddr_in *ap, struct rpc_timeout *to)
{
	struct rpc_xprt	*xprt;
	struct rpc_rqst	*req;
	struct sock	*inet;
	int		i;

	dprintk("RPC:      setting up %s transport...\n",
				proto == IPPROTO_UDP? "UDP" : "TCP");

#if LINUX_VERSION_CODE >= 0x020100
	inet = sock->sk;
#else
	inet = (struct sock *) sock->data;
#endif

	if ((xprt = kmalloc(sizeof(struct rpc_xprt), GFP_KERNEL)) == NULL)
		return NULL;
	memset(xprt, 0, sizeof(*xprt)); /* Nnnngh! */

	xprt->file = NULL;
	xprt->sock = sock;
	xprt->inet = inet;
	xprt->addr = *ap;
	xprt->prot = proto;
	xprt->stream = (proto == IPPROTO_TCP)? 1 : 0;
	xprt->cwnd = RPC_INITCWND;
#ifdef SOCK_HAS_USER_DATA
	inet->user_data = xprt;
#else
	xprt->link = sock_list;
	sock_list = xprt;
#endif
	xprt->old_data_ready = inet->data_ready;
	xprt->old_state_change = inet->state_change;
	xprt->old_write_space = inet->write_space;
	if (proto == IPPROTO_UDP) {
		inet->data_ready = udp_data_ready;
	} else {
		inet->data_ready = tcp_data_ready;
		inet->state_change = tcp_state_change;
		inet->write_space = tcp_write_space;
		xprt->nocong = 1;
	}
	xprt->connected = 1;

	/* Set timeout parameters */
	if (to) {
		xprt->timeout = *to;
		xprt->timeout.to_current = to->to_initval;
		xprt->timeout.to_resrvval = to->to_maxval << 1;
	} else {
		xprt_default_timeout(&xprt->timeout, xprt->prot);
	}

	xprt->pending = RPC_INIT_WAITQ("xprt_pending");
	xprt->sending = RPC_INIT_WAITQ("xprt_sending");
	xprt->backlog = RPC_INIT_WAITQ("xprt_backlog");
	xprt->reconn  = RPC_INIT_WAITQ("xprt_reconn");

	/* initialize free list */
	for (i = 0, req = xprt->slot; i < RPC_MAXREQS-1; i++, req++)
		req->rq_next = req + 1;
	req->rq_next = NULL;
	xprt->free = xprt->slot;

	dprintk("RPC:      created transport %p\n", xprt);
	
	/*
	 *	TCP requires the rpc I/O daemon is present
	 */
	if(proto==IPPROTO_TCP)
		rpciod_up();
	return xprt;
}

/*
 * Create and initialize an RPC client given an open file.
 * This is obsolete now.
 */
#if 0
struct rpc_xprt *
xprt_create(struct file *file, struct sockaddr_in *ap, struct rpc_timeout *to)
{
	struct rpc_xprt	*xprt;
	struct socket	*sock;
	int		proto;

	if (!file) {
		printk("RPC: file == NULL in xprt_create!\n");
		return NULL;
	}

	sock = &file->f_inode->u.socket_i;
	if (sock->ops->family != PF_INET) {
		printk(KERN_WARNING "RPC: only INET sockets supported\n");
		return NULL;
	}

	proto = (sock->type == SOCK_DGRAM)? IPPROTO_UDP : IPPROTO_TCP;
	if ((xprt = xprt_setup(sock, proto, ap, to)) != NULL) {
		xprt->file = file;
		file->f_count++;
	}

	return xprt;
}
#endif

/*
 * Bind to a reserved port
 */
static inline int
xprt_bindresvport(struct socket *sock)
{
	struct sockaddr_in myaddr;
	int		err, port;

	memset(&myaddr, 0, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
	port = 800;
	do {
		myaddr.sin_port = htons(port);
		err = sock->ops->bind(sock, (struct sockaddr *) &myaddr,
						sizeof(myaddr));
	} while (err == -EADDRINUSE && --port > 0);

	if (err < 0)
		printk("RPC: Can't bind to reserved port (%d).\n", -err);

	return err;
}

/*
 * Create a client socket given the protocol and peer address.
 */
static struct socket *
xprt_create_socket(int proto, struct sockaddr_in *sap, struct rpc_timeout *to)
{
	struct socket	*sock;
	int		type, err;

	dprintk("RPC:      xprt_create_socket(%08lx, %s %d)\n",
			   sap? ntohl(sap->sin_addr.s_addr) : 0,
			   (proto == IPPROTO_UDP)? "udp" : "tcp", proto);

	type = (proto == IPPROTO_UDP)? SOCK_DGRAM : SOCK_STREAM;
	if ((err = sock_create(PF_INET, type, proto, &sock)) < 0) {
		printk("RPC: can't create socket (%d).\n", -err);
		goto failed;
	}

	/* If the caller has root privs, bind to a reserved port */
	if (!current->fsuid && xprt_bindresvport(sock) < 0)
		goto failed;

	if (type == SOCK_STREAM && sap) {
		err = sock->ops->connect(sock, (struct sockaddr *) sap,
						sizeof(*sap), 0);
		if (err < 0) {
			printk("RPC: TCP connect failed (%d).\n", -err);
			goto failed;
		}
	}

	return sock;

failed:
	sock_release(sock);
	return NULL;
}

/*
 * Create an RPC client transport given the protocol and peer address.
 */
struct rpc_xprt *
xprt_create_proto(int proto, struct sockaddr_in *sap, struct rpc_timeout *to)
{
	struct socket	*sock;
	struct rpc_xprt	*xprt;

	dprintk("RPC:      xprt_create_proto called\n");

	if (!(sock = xprt_create_socket(proto, sap, to)))
		return NULL;

	if (!(xprt = xprt_setup(sock, proto, sap, to)))
		sock_release(sock);

	return xprt;
}

/*
 * Prepare for transport shutdown.
 */
void
xprt_shutdown(struct rpc_xprt *xprt)
{
	xprt->shutdown = 1;
	rpc_wake_up(&xprt->sending);
	rpc_wake_up(&xprt->pending);
	rpc_wake_up(&xprt->backlog);
	rpc_wake_up(&xprt->reconn);
}

/*
 * Destroy an RPC transport, killing off all requests.
 */
int
xprt_destroy(struct rpc_xprt *xprt)
{
#ifndef SOCK_HAS_USER_DATA
	struct rpc_xprt	**q;

	for (q = &sock_list; *q && *q != xprt; q = &((*q)->link))
		;
	if (!*q) {
		printk(KERN_WARNING "xprt_destroy: unknown socket!\n");
		return -EIO;	/* why is there no EBUGGYSOFTWARE */
	}
	*q = xprt->link;
#endif

	dprintk("RPC:      destroying transport %p\n", xprt);
	xprt_close(xprt);
	kfree(xprt);

	return 0;
}

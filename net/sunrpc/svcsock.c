/*
 * linux/net/sunrpc/svcsock.c
 *
 * These are the RPC server socket internals.
 *
 * The server scheduling algorithm does not always distribute the load
 * evenly when servicing a single client. May need to modify the
 * svc_sock_enqueue procedure...
 *
 * TCP support is largely untested and may be a little slow. The problem
 * is that we currently do two separate recvfrom's, one for the 4-byte
 * record length, and the second for the actual record. This could possibly
 * be improved by always reading a minimum size of around 100 bytes and
 * tucking any superfluous bytes away in a temporary store. Still, that
 * leaves write requests out in the rain. An alternative may be to peek at
 * the first skb in the queue, and if it matches the next TCP sequence
 * number, to extract the record marker. Yuck.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/udp.h>
#include <linux/version.h>
#include <linux/unistd.h>
#include <linux/malloc.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/ip.h>
#if LINUX_VERSION_CODE >= 0x020100
#include <asm/uaccess.h>
#endif

#include <linux/sunrpc/types.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/stats.h>


#define RPCDBG_FACILITY	RPCDBG_SVCSOCK


static struct svc_sock *svc_setup_socket(struct svc_serv *, struct socket *,
					 int *errp, int pmap_reg);
static void		svc_udp_data_ready(struct sock *, int);
static int		svc_udp_recvfrom(struct svc_rqst *);
static int		svc_udp_sendto(struct svc_rqst *);


/*
 * Queue up an idle server thread.
 */
static inline void
svc_serv_enqueue(struct svc_serv *serv, struct svc_rqst *rqstp)
{
	rpc_append_list(&serv->sv_threads, rqstp);
}

/*
 * Dequeue an nfsd thread.
 */
static inline void
svc_serv_dequeue(struct svc_serv *serv, struct svc_rqst *rqstp)
{
	rpc_remove_list(&serv->sv_threads, rqstp);
}

/*
 * Release an skbuff after use
 */
static inline void
svc_release_skb(struct svc_rqst *rqstp)
{
	struct sk_buff *skb = rqstp->rq_skbuff;

	if (!skb)
		return;
	rqstp->rq_skbuff = NULL;

	dprintk("svc: service %p, releasing skb %p\n", rqstp, skb);
	skb_free_datagram(rqstp->rq_sock->sk_sk, skb);
}

/*
 * Queue up a socket with data pending. If there are idle nfsd
 * processes, wake 'em up.
 * When calling this function, you should make sure it can't be interrupted
 * by the network bottom half.
 */
static void
svc_sock_enqueue(struct svc_sock *svsk)
{
	struct svc_serv	*serv = svsk->sk_server;
	struct svc_rqst	*rqstp;

	if (serv->sv_threads && serv->sv_sockets)
		printk(KERN_ERR
			"svc_sock_enqueue: threads and sockets both waiting??\n");

	if (svsk->sk_busy) {
		/* Don't enqueue socket while daemon is receiving */
		dprintk("svc: socket %p busy, not enqueued\n", svsk->sk_sk);
		return;
	}

	/* Mark socket as busy. It will remain in this state until the
	 * server has processed all pending data and put the socket back
	 * on the idle list.
	 */
	svsk->sk_busy = 1;

	if ((rqstp = serv->sv_threads) != NULL) {
		dprintk("svc: socket %p served by daemon %p\n",
			svsk->sk_sk, rqstp);
		svc_serv_dequeue(serv, rqstp);
		if (rqstp->rq_sock)
			printk(KERN_ERR 
				"svc_sock_enqueue: server %p, rq_sock=%p!\n",
				rqstp, rqstp->rq_sock);
		rqstp->rq_sock = svsk;
		svsk->sk_inuse++;
		wake_up(&rqstp->rq_wait);
	} else {
		dprintk("svc: socket %p put into queue\n", svsk->sk_sk);
		rpc_append_list(&serv->sv_sockets, svsk);
		svsk->sk_qued = 1;
	}
}

/*
 * Dequeue the first socket.
 */
static inline struct svc_sock *
svc_sock_dequeue(struct svc_serv *serv)
{
	struct svc_sock	*svsk;

	start_bh_atomic();
	if ((svsk = serv->sv_sockets) != NULL)
		rpc_remove_list(&serv->sv_sockets, svsk);
	end_bh_atomic();

	if (svsk) {
		dprintk("svc: socket %p dequeued, inuse=%d\n",
			svsk->sk_sk, svsk->sk_inuse);
		svsk->sk_qued = 0;
	}

	return svsk;
}

/*
 * Having read count bytes from a socket, check whether it
 * needs to be re-enqueued.
 */
static inline void
svc_sock_received(struct svc_sock *svsk, int count)
{
	start_bh_atomic();
	if ((svsk->sk_data -= count) < 0) {
		printk(KERN_NOTICE "svc: sk_data negative!\n");
		svsk->sk_data = 0;
	}
	svsk->sk_rqstp = NULL; /* XXX */
	svsk->sk_busy = 0;
	if (svsk->sk_conn || svsk->sk_data || svsk->sk_close) {
		dprintk("svc: socket %p re-enqueued after receive\n",
						svsk->sk_sk);
		svc_sock_enqueue(svsk);
	}
	end_bh_atomic();
}

/*
 * Dequeue a new connection.
 */
static inline void
svc_sock_accepted(struct svc_sock *svsk)
{
	start_bh_atomic();
        svsk->sk_busy = 0;
        svsk->sk_conn--;
        if (svsk->sk_conn || svsk->sk_data || svsk->sk_close) {
                dprintk("svc: socket %p re-enqueued after accept\n",
						svsk->sk_sk);
                svc_sock_enqueue(svsk);
        }
	end_bh_atomic();
}

/*
 * Release a socket after use.
 */
static inline void
svc_sock_release(struct svc_rqst *rqstp)
{
	struct svc_sock	*svsk = rqstp->rq_sock;

	if (!svsk)
		return;
	svc_release_skb(rqstp);
	rqstp->rq_sock = NULL;
	if (!--(svsk->sk_inuse) && svsk->sk_dead) {
		dprintk("svc: releasing dead socket\n");
		sock_release(svsk->sk_sock);
		kfree(svsk);
	}
}

/*
 * External function to wake up a server waiting for data
 */
void
svc_wake_up(struct svc_serv *serv)
{
	struct svc_rqst	*rqstp;

	if ((rqstp = serv->sv_threads) != NULL) {
		dprintk("svc: daemon %p woken up.\n", rqstp);
		/*
		svc_serv_dequeue(serv, rqstp);
		rqstp->rq_sock = NULL;
		 */
		wake_up(&rqstp->rq_wait);
	}
}

/*
 * Generic sendto routine
 */
static int
svc_sendto(struct svc_rqst *rqstp, struct iovec *iov, int nr)
{
	mm_segment_t	oldfs;
	struct socket	*sock = rqstp->rq_sock->sk_sock;
	struct msghdr	msg;
	int		i, buflen, len;

	for (i = buflen = 0; i < nr; i++)
		buflen += iov[i].iov_len;

	msg.msg_name    = &rqstp->rq_addr;
	msg.msg_namelen = sizeof(rqstp->rq_addr);
	msg.msg_iov     = iov;
	msg.msg_iovlen  = nr;
	msg.msg_control = 0;

#if LINUX_VERSION_CODE >= 0x020100
	msg.msg_flags	= MSG_DONTWAIT;

	oldfs = get_fs(); set_fs(KERNEL_DS);
	len = sock_sendmsg(sock, &msg, buflen);
	set_fs(oldfs);
#else
	msg.msg_flags	= 0;

	oldfs = get_fs(); set_fs(KERNEL_DS);
	len = sock->ops->sendmsg(sock, &msg, buflen, 1, 0);
	set_fs(oldfs);
#endif

	dprintk("svc: socket %p sendto([%p %d... ], %d, %d) = %d\n",
			rqstp->rq_sock,
			iov[0].iov_base, iov[0].iov_len, nr,
			buflen, len);

	return len;
}

/*
 * Check input queue length
 */
static int
svc_recv_available(struct svc_sock *svsk)
{
	mm_segment_t	oldfs;
	struct socket	*sock = svsk->sk_sock;
	int		avail, err;

	oldfs = get_fs(); set_fs(KERNEL_DS);
	err = sock->ops->ioctl(sock, TIOCINQ, (unsigned long) &avail);
	set_fs(oldfs);

	return (err >= 0)? avail : err;
}

/*
 * Generic recvfrom routine.
 */
static int
svc_recvfrom(struct svc_rqst *rqstp, struct iovec *iov, int nr, int buflen)
{
	mm_segment_t	oldfs;
	struct msghdr	msg;
	struct socket	*sock;
	int		len;

	rqstp->rq_addrlen = sizeof(rqstp->rq_addr);
	sock = rqstp->rq_sock->sk_sock;

	msg.msg_name    = &rqstp->rq_addr;
	msg.msg_namelen = sizeof(rqstp->rq_addr);
	msg.msg_iov     = iov;
	msg.msg_iovlen  = nr;
	msg.msg_control = 0;

#if LINUX_VERSION_CODE >= 0x020100
	msg.msg_flags	= MSG_DONTWAIT;

	oldfs = get_fs(); set_fs(KERNEL_DS);
	len = sock_recvmsg(sock, &msg, buflen, MSG_DONTWAIT);
	set_fs(oldfs);
#else
	msg.msg_flags	= 0;

	oldfs = get_fs(); set_fs(KERNEL_DS);
	len = sock->ops->recvmsg(sock, &msg, buflen, 0, 1, &rqstp->rq_addrlen);
	set_fs(oldfs);
#endif

	dprintk("svc: socket %p recvfrom(%p, %d) = %d\n", rqstp->rq_sock,
				iov[0].iov_base, iov[0].iov_len, len);

	return len;
}

/*
 * INET callback when data has been received on the socket.
 */
static void
svc_udp_data_ready(struct sock *sk, int count)
{
	struct svc_sock	*svsk = (struct svc_sock *)(sk->user_data);

	if (!svsk)
		return;
	dprintk("svc: socket %p(inet %p), count=%d, busy=%d\n",
		svsk, sk, count, svsk->sk_busy);
	svsk->sk_data = 1;
	svc_sock_enqueue(svsk);
}

/*
 * Receive a datagram from a UDP socket.
 */
static int
svc_udp_recvfrom(struct svc_rqst *rqstp)
{
	struct svc_sock	*svsk = rqstp->rq_sock;
	struct svc_serv	*serv = svsk->sk_server;
	struct sk_buff	*skb;
	u32		*data;
	int		err, len;

	svsk->sk_data = 0;
	while ((skb = skb_recv_datagram(svsk->sk_sk, 0, 1, &err)) == NULL) {
		svc_sock_received(svsk, 0);
		if (err == -EAGAIN)
			return err;
		/* possibly an icmp error */
		dprintk("svc: recvfrom returned error %d\n", -err);
	}

	/* There may be more data */
	svsk->sk_data = 1;

	len  = skb->len - sizeof(struct udphdr);
	data = (u32 *) (skb->h.raw + sizeof(struct udphdr));

	rqstp->rq_skbuff      = skb;
	rqstp->rq_argbuf.base = data;
	rqstp->rq_argbuf.buf  = data;
	rqstp->rq_argbuf.len  = (len >> 2);
	/* rqstp->rq_resbuf      = rqstp->rq_defbuf; */
	rqstp->rq_prot        = IPPROTO_UDP;

	/* Get sender address */
	rqstp->rq_addr.sin_family = AF_INET;
	rqstp->rq_addr.sin_port = skb->h.uh->source;
#if LINUX_VERSION_CODE >= 0x020100
	rqstp->rq_addr.sin_addr.s_addr = skb->nh.iph->saddr;
#else
	rqstp->rq_addr.sin_addr.s_addr = skb->saddr;
#endif

	if (serv->sv_stats)
		serv->sv_stats->netudpcnt++;

	/* One down, maybe more to go... */
	svsk->sk_sk->stamp = skb->stamp;
	svc_sock_received(svsk, 0);

	return len;
}

static int
svc_udp_sendto(struct svc_rqst *rqstp)
{
	struct svc_buf	*bufp = &rqstp->rq_resbuf;
	int		error;

	/* Set up the first element of the reply iovec.
	 * Any other iovecs that may be in use have been taken
	 * care of by the server implementation itself.
	 */
	/* bufp->base = bufp->area; */
	bufp->iov[0].iov_base = bufp->base;
	bufp->iov[0].iov_len  = bufp->len << 2;

	error = svc_sendto(rqstp, bufp->iov, bufp->nriov);
	if (error == -ECONNREFUSED)
		/* ICMP error on earlier request. */
		error = svc_sendto(rqstp, bufp->iov, bufp->nriov);
	else if (error == -EAGAIN)
		/* Ignore and wait for re-xmit */
		error = 0;

	return error;
}

static int
svc_udp_init(struct svc_sock *svsk)
{
	svsk->sk_sk->data_ready = svc_udp_data_ready;
	svsk->sk_recvfrom = svc_udp_recvfrom;
	svsk->sk_sendto = svc_udp_sendto;

	return 0;
}

/*
 * A state change on a listening socket means there's a connection
 * pending.
 */
static void
svc_tcp_state_change1(struct sock *sk)
{
	struct svc_sock	*svsk;

	dprintk("svc: socket %p TCP (listen) state change %d\n",
			sk, sk->state);

	if  (sk->state != TCP_ESTABLISHED) {
		/* Aborted connection, SYN_RECV or whatever... */
		return;
	}
	if (!(svsk = (struct svc_sock *) sk->user_data)) {
		printk("svc: socket %p: no user data\n", sk);
		return;
	}
	svsk->sk_conn++;
	svc_sock_enqueue(svsk);
}

/*
 * A state change on a connected socket means it's dying or dead.
 */
static void
svc_tcp_state_change2(struct sock *sk)
{
	struct svc_sock	*svsk;

	dprintk("svc: socket %p TCP (connected) state change %d (svsk %p)\n",
			sk, sk->state, sk->user_data);

	if (!(svsk = (struct svc_sock *) sk->user_data)) {
		printk("svc: socket %p: no user data\n", sk);
		return;
	}
	svsk->sk_close = 1;
	svc_sock_enqueue(svsk);
}

static void
svc_tcp_data_ready(struct sock *sk, int count)
{
	struct svc_sock *	svsk;

	/* Disconnect signalled through data_ready?!? */
	if (sk->state != TCP_ESTABLISHED) {
		svc_tcp_state_change2(sk);
		return;
	}

	dprintk("svc: socket %p TCP data ready (svsk %p)\n",
			sk, sk->user_data);
	if (!(svsk = (struct svc_sock *)(sk->user_data)))
		return;
	svsk->sk_data++;
	svc_sock_enqueue(svsk);
}

/*
 * Accept a TCP connection
 */
static void
svc_tcp_accept(struct svc_sock *svsk)
{
	struct sockaddr_in sin;
	struct svc_serv	*serv = svsk->sk_server;
	struct socket	*sock = svsk->sk_sock;
	struct socket	*newsock;
	struct proto_ops *ops;
	struct svc_sock	*newsvsk;
	int		err, slen;

	dprintk("svc: tcp_accept %p sock %p\n", svsk, sock);
	if (!sock)
		return;

	if (!(newsock = sock_alloc())) {
		printk(KERN_WARNING "%s: no more sockets!\n", serv->sv_name);
		return;
	}
	dprintk("svc: tcp_accept %p allocated\n", newsock);

	newsock->type = sock->type;
	if ((err = sock->ops->dup(newsock, sock)) < 0) {
		printk(KERN_WARNING "%s: socket dup failed (err %d)!\n",
					serv->sv_name, -err);
		goto failed;
	}

	ops = newsock->ops;
	if ((err = ops->accept(sock, newsock, O_NONBLOCK)) < 0) {
		printk(KERN_WARNING "%s: accept failed (err %d)!\n",
					serv->sv_name, -err);
		goto failed;		/* aborted connection or whatever */
	}

	slen = sizeof(sin);
	err = ops->getname(newsock, (struct sockaddr *) &sin, &slen, 1);
	if (err < 0) {
		printk(KERN_WARNING "%s: peername failed (err %d)!\n",
					serv->sv_name, -err);
		goto failed;		/* aborted connection or whatever */
	}

	/* Ideally, we would want to reject connections from unauthorized
	 * hosts here, but we have no generic client tables. For now,
	 * we just punt connects from unprivileged ports. */
	if (ntohs(sin.sin_port) >= 1024) {
		printk(KERN_WARNING
			"%s: connect from unprivileged port: %08lx:%d",
			serv->sv_name, 
			ntohl(sin.sin_addr.s_addr), ntohs(sin.sin_port));
		goto failed;
	}

	dprintk("%s: connect from %08lx:%04x\n", serv->sv_name,
			ntohl(sin.sin_addr.s_addr), ntohs(sin.sin_port));

	if (!(newsvsk = svc_setup_socket(serv, newsock, &err, 0)))
		goto failed;

	/* Precharge. Data may have arrived on the socket before we
	 * installed the data_ready callback. 
	 */
	newsvsk->sk_data = 1;
	newsvsk->sk_temp = 1;
	svc_sock_enqueue(newsvsk);

	if (serv->sv_stats)
		serv->sv_stats->nettcpconn++;

	return;

failed:
	sock_release(newsock);
	return;
}

/*
 * Receive data from a TCP socket.
 */
static int
svc_tcp_recvfrom(struct svc_rqst *rqstp)
{
	struct svc_sock	*svsk = rqstp->rq_sock;
	struct svc_serv	*serv = svsk->sk_server;
	struct svc_buf	*bufp = &rqstp->rq_argbuf;
	int		len, ready;

	dprintk("svc: tcp_recv %p data %d conn %d close %d\n",
			svsk, svsk->sk_data, svsk->sk_conn, svsk->sk_close);

	if (svsk->sk_close) {
		svc_delete_socket(svsk);
		return 0;
	}

	if (svsk->sk_conn) {
		svc_tcp_accept(svsk);
		svc_sock_accepted(svsk);
		return 0;
	}

	ready = svsk->sk_data;

	/* Receive data. If we haven't got the record length yet, get
	 * the next four bytes. Otherwise try to gobble up as much as
	 * possible up to the complete record length.
	 */
	if (svsk->sk_tcplen < 4) {
		unsigned long	want = 4 - svsk->sk_tcplen;
		struct iovec	iov;

		iov.iov_base = ((u32 *) &svsk->sk_reclen) + svsk->sk_tcplen;
		iov.iov_len  = want;
		if ((len = svc_recvfrom(rqstp, &iov, 1, want)) < 0)
			goto error;
		svsk->sk_tcplen += len;

		svsk->sk_reclen = ntohl(svsk->sk_reclen);
		if (!(svsk->sk_reclen & 0x80000000)) {
			/* FIXME: shutdown socket */
			printk(KERN_NOTICE "RPC: bad TCP reclen %08lx",
					(unsigned long) svsk->sk_reclen);
			return -EIO;
		}
		svsk->sk_reclen &= 0x7fffffff;
		dprintk("svc: TCP record, %ld bytes\n", svsk->sk_reclen);
	}

	/* Check whether enough data is available */
	len = svc_recv_available(svsk);
	if (len < 0)
		goto error;

	if (len < svsk->sk_reclen) {
		dprintk("svc: incomplete TCP record (%d of %ld)\n",
				len, svsk->sk_reclen);
		svc_sock_received(svsk, ready);
		len = -EAGAIN;	/* record not complete */
	}

	/* Frob argbuf */
	bufp->iov[0].iov_base += 4;
	bufp->iov[0].iov_len  -= 4;

	/* Now receive data */
	len = svc_recvfrom(rqstp, bufp->iov, bufp->nriov, svsk->sk_reclen);
	if (len < 0)
		goto error;

	dprintk("svc: TCP complete record (%d bytes)\n", len);

	/* Position reply write pointer immediately after
	 * record length */
	rqstp->rq_resbuf.buf += 1;
	rqstp->rq_resbuf.len  = 1;

	rqstp->rq_skbuff      = 0;
	rqstp->rq_argbuf.buf += 1;
	rqstp->rq_argbuf.len  = (len >> 2);
	rqstp->rq_prot	      = IPPROTO_TCP;

	/* Reset TCP read info */
	svsk->sk_reclen = 0;
	svsk->sk_tcplen = 0;

	svc_sock_received(svsk, 1);
	if (serv->sv_stats)
		serv->sv_stats->nettcpcnt++;

	return len;

error:
	if (len == -EAGAIN) {
		dprintk("RPC: TCP recvfrom got EAGAIN\n");
		svc_sock_received(svsk, ready); /* Clear data ready */
	} else {
		printk(KERN_NOTICE "%s: recvfrom returned errno %d\n",
					svsk->sk_server->sv_name, -len);
		svc_sock_received(svsk, 0);
	}

	return len;
}

/*
 * Send out data on TCP socket.
 * FIXME: Make the sendto call non-blocking in order not to hang
 * a daemon on a dead client. Requires write queue maintenance.
 */
static int
svc_tcp_sendto(struct svc_rqst *rqstp)
{
	struct svc_buf	*bufp = &rqstp->rq_resbuf;

	/* Set up the first element of the reply iovec.
	 * Any other iovecs that may be in use have been taken
	 * care of by the server implementation itself.
	 */
	bufp->iov[0].iov_base = bufp->base;
	bufp->iov[0].iov_len  = bufp->len << 2;
	bufp->base[0] = htonl(0x80000000|((bufp->len << 2) - 4));

	return svc_sendto(rqstp, bufp->iov, bufp->nriov);
}

static int
svc_tcp_init(struct svc_sock *svsk)
{
	struct sock	*sk = svsk->sk_sk;

	svsk->sk_recvfrom = svc_tcp_recvfrom;
	svsk->sk_sendto = svc_tcp_sendto;

	if (sk->state == TCP_LISTEN) {
		dprintk("setting up TCP socket for listening\n");
		sk->state_change = svc_tcp_state_change1;
	} else {
		dprintk("setting up TCP socket for reading\n");
		sk->state_change = svc_tcp_state_change2;
		sk->data_ready = svc_tcp_data_ready;

		svsk->sk_reclen = 0;
		svsk->sk_tcplen = 0;
	}

	return 0;
}

/*
 * Receive the next request on any socket.
 */
int
svc_recv(struct svc_serv *serv, struct svc_rqst *rqstp, long timeout)
{
	struct svc_sock		*svsk;
	int			len;
	struct wait_queue	wait = { current, NULL };

	dprintk("svc: server %p waiting for data (to = %ld)\n",
		rqstp, timeout);

	if (rqstp->rq_sock)
		printk(KERN_ERR 
			"svc_recv: service %p, socket not NULL!\n",
			 rqstp);
	if (waitqueue_active(&rqstp->rq_wait))
		printk(KERN_ERR 
			"svc_recv: service %p, wait queue active!\n",
			 rqstp);

again:
	/* Initialize the buffers */
	rqstp->rq_argbuf = rqstp->rq_defbuf;
	rqstp->rq_resbuf = rqstp->rq_defbuf;

	if (signalled())
		return -EINTR;

	start_bh_atomic();
	if ((svsk = svc_sock_dequeue(serv)) != NULL) {
		rqstp->rq_sock = svsk;
		svsk->sk_inuse++;
	} else {
		/* No data pending. Go to sleep */
		svc_serv_enqueue(serv, rqstp);

		/*
		 * We have to be able to interrupt this wait
		 * to bring down the daemons ...
		 */
		current->state = TASK_INTERRUPTIBLE;
		add_wait_queue(&rqstp->rq_wait, &wait);
		end_bh_atomic();
		schedule_timeout(timeout);

		remove_wait_queue(&rqstp->rq_wait, &wait);

		start_bh_atomic();
		if (!(svsk = rqstp->rq_sock)) {
			svc_serv_dequeue(serv, rqstp);
			end_bh_atomic();
			dprintk("svc: server %p, no data yet\n", rqstp);
			return signalled()? -EINTR : -EAGAIN;
		}
	}
	end_bh_atomic();

	dprintk("svc: server %p, socket %p, inuse=%d\n",
		 rqstp, svsk, svsk->sk_inuse);
	len = svsk->sk_recvfrom(rqstp);
	dprintk("svc: got len=%d\n", len);

	/* No data, incomplete (TCP) read, or accept() */
	if (len == 0 || len == -EAGAIN) {
		svc_sock_release(rqstp);
		goto again;
	}

	rqstp->rq_secure  = ntohs(rqstp->rq_addr.sin_port) < 1024;
	rqstp->rq_userset = 0;
	rqstp->rq_verfed  = 0;

	svc_getlong(&rqstp->rq_argbuf, rqstp->rq_xid);
	svc_putlong(&rqstp->rq_resbuf, rqstp->rq_xid);

	/* Assume that the reply consists of a single buffer. */
	rqstp->rq_resbuf.nriov = 1;

	if (serv->sv_stats)
		serv->sv_stats->netcnt++;
	return len;
}

/* 
 * Drop request
 */
void
svc_drop(struct svc_rqst *rqstp)
{
	dprintk("svc: socket %p dropped request\n", rqstp->rq_sock);
	svc_sock_release(rqstp);
}

/*
 * Return reply to client.
 */
int
svc_send(struct svc_rqst *rqstp)
{
	struct svc_sock	*svsk;
	int		len;

	if ((svsk = rqstp->rq_sock) == NULL) {
		printk(KERN_WARNING "NULL socket pointer in %s:%d\n",
				__FILE__, __LINE__);
		return -EFAULT;
	}

	/* release the receive skb before sending the reply */
	svc_release_skb(rqstp);

	len = svsk->sk_sendto(rqstp);
	svc_sock_release(rqstp);

	if (len == -ECONNREFUSED || len == -ENOTCONN || len == -EAGAIN)
		return 0;
	return len;
}

/*
 * Initialize socket for RPC use and create svc_sock struct
 * XXX: May want to setsockopt SO_SNDBUF and SO_RCVBUF.
 */
static struct svc_sock *
svc_setup_socket(struct svc_serv *serv, struct socket *sock,
					int *errp, int pmap_register)
{
	struct svc_sock	*svsk;
	struct sock	*inet;

	dprintk("svc: svc_setup_socket %p\n", sock);
	if (!(svsk = kmalloc(sizeof(*svsk), GFP_KERNEL))) {
		*errp = -ENOMEM;
		return NULL;
	}
	memset(svsk, 0, sizeof(*svsk));

#if LINUX_VERSION_CODE >= 0x020100
	inet = sock->sk;
#else
	inet = (struct sock *) sock->data;
#endif
	inet->user_data = svsk;
	svsk->sk_sock = sock;
	svsk->sk_sk = inet;
	svsk->sk_ostate = inet->state_change;
	svsk->sk_odata = inet->data_ready;
	svsk->sk_server = serv;

	/* Initialize the socket */
	if (sock->type == SOCK_DGRAM)
		*errp = svc_udp_init(svsk);
	else
		*errp = svc_tcp_init(svsk);
if (svsk->sk_sk == NULL)
	printk(KERN_WARNING "svsk->sk_sk == NULL after svc_prot_init!\n");

	/* Register socket with portmapper */
	if (*errp >= 0 && pmap_register)
		*errp = svc_register(serv, inet->protocol, ntohs(inet->sport));

	if (*errp < 0) {
		inet->user_data = NULL;
		kfree(svsk);
		return NULL;
	}

	svsk->sk_list = serv->sv_allsocks;
	serv->sv_allsocks = svsk;

	dprintk("svc: svc_setup_socket created %p (inet %p)\n",
				svsk, svsk->sk_sk);
	return svsk;
}

/*
 * Create socket for RPC service.
 */
static int
svc_create_socket(struct svc_serv *serv, int protocol, struct sockaddr_in *sin)
{
	struct svc_sock	*svsk;
	struct socket	*sock;
	int		error;
	int		type;

	dprintk("svc: svc_create_socket(%s, %d, %08lx:%d)\n",
				serv->sv_program->pg_name, protocol,
				ntohl(sin->sin_addr.s_addr),
				ntohs(sin->sin_port));

	if (protocol != IPPROTO_UDP && protocol != IPPROTO_TCP) {
		printk(KERN_WARNING "svc: only UDP and TCP "
				"sockets supported\n");
		return -EINVAL;
	}
	type = (protocol == IPPROTO_UDP)? SOCK_DGRAM : SOCK_STREAM;

	if ((error = sock_create(PF_INET, type, protocol, &sock)) < 0)
		return error;

	if (sin != NULL) {
		error = sock->ops->bind(sock, (struct sockaddr *) sin,
						sizeof(*sin));
		if (error < 0)
			goto bummer;
	}

	if (protocol == IPPROTO_TCP) {
		if ((error = sock->ops->listen(sock, 5)) < 0)
			goto bummer;
		sock->flags |= SO_ACCEPTCON;
	}

	if ((svsk = svc_setup_socket(serv, sock, &error, 1)) != NULL)
		return 0;

bummer:
	dprintk("svc: svc_create_socket error = %d\n", -error);
	sock_release(sock);
	return error;
}

/*
 * Remove a dead socket
 */
void
svc_delete_socket(struct svc_sock *svsk)
{
	struct svc_sock	**rsk;
	struct svc_serv	*serv;
	struct sock	*sk;

	dprintk("svc: svc_delete_socket(%p)\n", svsk);

	serv = svsk->sk_server;
	sk = svsk->sk_sk;

	sk->state_change = svsk->sk_ostate;
	sk->data_ready = svsk->sk_odata;

	for (rsk = &serv->sv_allsocks; *rsk; rsk = &(*rsk)->sk_list) {
		if (*rsk == svsk)
			break;
	}
	if (!*rsk)
		return;
	*rsk = svsk->sk_list;

	if (svsk->sk_qued)
		rpc_remove_list(&serv->sv_sockets, svsk);
	svsk->sk_dead = 1;

	if (!svsk->sk_inuse) {
		sock_release(svsk->sk_sock);
		kfree(svsk);
	} else {
		printk(KERN_NOTICE "svc: server socket destroy delayed\n");
		/* svsk->sk_server = NULL; */
	}
}

/*
 * Make a socket for nfsd and lockd
 */
int
svc_makesock(struct svc_serv *serv, int protocol, unsigned short port)
{
	struct sockaddr_in	sin;

	dprintk("svc: creating socket proto = %d\n", protocol);
	sin.sin_family      = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port        = htons(port);
	return svc_create_socket(serv, protocol, &sin);
}


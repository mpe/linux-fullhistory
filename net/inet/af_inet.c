/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		AF_INET protocol family socket handler.
 *
 * Version:	@(#)af_inet.c	(from sock.c) 1.0.17	06/02/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Alan Cox, <A.Cox@swansea.ac.uk>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

#include <asm/segment.h>
#include <asm/system.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include "ip.h"
#include "protocol.h"
#include "arp.h"
#include "rarp.h"
#include "route.h"
#include "tcp.h"
#include "udp.h"
#include <linux/skbuff.h>
#include "sock.h"
#include "raw.h"
#include "icmp.h"

#define min(a,b)	((a)<(b)?(a):(b))

extern struct proto packet_prot;


/*
 *	See if a socket number is in use.
 */
 
static int sk_inuse(struct proto *prot, int num)
{
	struct sock *sk;

	for(sk = prot->sock_array[num & (SOCK_ARRAY_SIZE -1 )];
		sk != NULL;  sk=sk->next) 
	{
		if (sk->num == num) 
			return(1);
	}
	return(0);
}


/*
 *	Pick a new socket number
 */

unsigned short get_new_socknum(struct proto *prot, unsigned short base)
{
	static int start=0;

	/*
	 * Used to cycle through the port numbers so the
	 * chances of a confused connection drop.
	 */
	 
	int i, j;
	int best = 0;
	int size = 32767; /* a big num. */
	struct sock *sk;

	if (base == 0) 
		base = PROT_SOCK+1+(start % 1024);
	if (base <= PROT_SOCK) 
	{
		base += PROT_SOCK+(start % 1024);
	}

	/* Now look through the entire array and try to find an empty ptr. */
	for(i=0; i < SOCK_ARRAY_SIZE; i++) 
	{
		j = 0;
		sk = prot->sock_array[(i+base+1) &(SOCK_ARRAY_SIZE -1)];
		while(sk != NULL) 
		{
			sk = sk->next;
			j++;
		}
		if (j == 0) 
		{
			start =(i+1+start )%1024;
			return(i+base+1);
		}
		if (j < size) 
		{
			best = i;
			size = j;
		}
	}

	/* Now make sure the one we want is not in use. */

	while(sk_inuse(prot, base +best+1)) 
	{
		best += SOCK_ARRAY_SIZE;
	}
	return(best+base+1);
}

/*
 *	Add a socket into the socket tables by number.
 */

void put_sock(unsigned short num, struct sock *sk)
{
	struct sock *sk1;
	struct sock *sk2;
	int mask;

	sk->num = num;
	sk->next = NULL;
	num = num &(SOCK_ARRAY_SIZE -1);

	/* We can't have an interupt re-enter here. */
	cli();
	if (sk->prot->sock_array[num] == NULL) 
	{
		sk->prot->sock_array[num] = sk;
		sti();
		return;
	}
	sti();
	for(mask = 0xff000000; mask != 0xffffffff; mask = (mask >> 8) | mask) 
	{
		if ((mask & sk->saddr) &&
		    (mask & sk->saddr) != (mask & 0xffffffff)) 
		{
			mask = mask << 8;
			break;
		}
	}
	cli();
	sk1 = sk->prot->sock_array[num];
	for(sk2 = sk1; sk2 != NULL; sk2=sk2->next) 
	{
		if (!(sk2->saddr & mask)) 
		{
			if (sk2 == sk1) 
			{
				sk->next = sk->prot->sock_array[num];
				sk->prot->sock_array[num] = sk;
				sti();
				return;
			}
			sk->next = sk2;
			sk1->next= sk;
			sti();
			return;
		}
		sk1 = sk2;
	}

	/* Goes at the end. */
	sk->next = NULL;
	sk1->next = sk;
	sti();
}

/*
 *	Remove a socket from the socket tables.
 */

static void remove_sock(struct sock *sk1)
{
	struct sock *sk2;

	if (!sk1->prot) 
	{
		printk("sock.c: remove_sock: sk1->prot == NULL\n");
		return;
	}

	/* We can't have this changing out from under us. */
	cli();
	sk2 = sk1->prot->sock_array[sk1->num &(SOCK_ARRAY_SIZE -1)];
	if (sk2 == sk1) 
	{
		sk1->prot->sock_array[sk1->num &(SOCK_ARRAY_SIZE -1)] = sk1->next;
		sti();
		return;
	}

	while(sk2 && sk2->next != sk1) 
	{
		sk2 = sk2->next;
	}

	if (sk2) 
	{
		sk2->next = sk1->next;
		sti();
		return;
	}
	sti();
}

/*
 *	Destroy an AF_INET socket
 */
 
void destroy_sock(struct sock *sk)
{
	struct sk_buff *skb;

  	sk->inuse = 1;			/* just to be safe. */

  	/* Incase it's sleeping somewhere. */
  	if (!sk->dead) 
  		sk->write_space(sk);

  	remove_sock(sk);
  
  	/* Now we can no longer get new packets. */
  	delete_timer(sk);

	while ((skb = tcp_dequeue_partial(sk)) != NULL) {
		IS_SKB(skb);
		kfree_skb(skb, FREE_WRITE);
	}

	/* Cleanup up the write buffer. */
  	while((skb = skb_dequeue(&sk->write_queue)) != NULL) {
		IS_SKB(skb);
		kfree_skb(skb, FREE_WRITE);
  	}

  	while((skb=skb_dequeue(&sk->receive_queue))!=NULL) {
	/*
	 * This will take care of closing sockets that were
	 * listening and didn't accept everything.
	 */
		if (skb->sk != NULL && skb->sk != sk) 
		{
			IS_SKB(skb);
			skb->sk->dead = 1;
			skb->sk->prot->close(skb->sk, 0);
		}
		IS_SKB(skb);
		kfree_skb(skb, FREE_READ);
	}

	/* Now we need to clean up the send head. */
	cli();
	for(skb = sk->send_head; skb != NULL; )
	{
		struct sk_buff *skb2;

		/*
		 * We need to remove skb from the transmit queue,
		 * or maybe the arp queue.
		 */
		if (skb->next  && skb->prev) {
/*			printk("destroy_sock: unlinked skb\n");*/
			IS_SKB(skb);
			skb_unlink(skb);
		}
		skb->dev = NULL;
		skb2 = skb->link3;
		kfree_skb(skb, FREE_WRITE);
		skb = skb2;
	}
	sk->send_head = NULL;
	sti();

  	/* And now the backlog. */
  	while((skb=skb_dequeue(&sk->back_log))!=NULL) 
  	{
		/* this should never happen. */
/*		printk("cleaning back_log\n");*/
		kfree_skb(skb, FREE_READ);
	}

	/* Now if it has a half accepted/ closed socket. */
	if (sk->pair) 
	{
		sk->pair->dead = 1;
		sk->pair->prot->close(sk->pair, 0);
		sk->pair = NULL;
  	}

	/*
	 * Now if everything is gone we can free the socket
	 * structure, otherwise we need to keep it around until
	 * everything is gone.
	 */

	  if (sk->dead && sk->rmem_alloc == 0 && sk->wmem_alloc == 0) 
	  {
		kfree_s((void *)sk,sizeof(*sk));
	  } 
	  else 
	  {
		/* this should never happen. */
		/* actually it can if an ack has just been sent. */
		sk->destroy = 1;
		sk->ack_backlog = 0;
		sk->inuse = 0;
		reset_timer(sk, TIME_DESTROY, SOCK_DESTROY_TIME);
  	}
}

/*
 *	The routines beyond this point handle the behaviour of an AF_INET
 *	socket object. Mostly it punts to the subprotocols of IP to do
 *	the work.
 */
 
static int inet_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk;

	sk = (struct sock *) sock->data;

	switch(cmd) 
	{
		case F_SETOWN:
			/*
			 * This is a little restrictive, but it's the only
			 * way to make sure that you can't send a sigurg to
			 * another process.
			 */
			if (!suser() && current->pgrp != -arg &&
				current->pid != arg) return(-EPERM);
			sk->proc = arg;
			return(0);
		case F_GETOWN:
			return(sk->proc);
		default:
			return(-EINVAL);
	}
}

/*
 *	Set socket options on an inet socket.
 */
 
static int inet_setsockopt(struct socket *sock, int level, int optname,
		    char *optval, int optlen)
{
  	struct sock *sk = (struct sock *) sock->data;  
	if (level == SOL_SOCKET)
		return sock_setsockopt(sk,level,optname,optval,optlen);
	if (sk->prot->setsockopt==NULL)
		return(-EOPNOTSUPP);
	else
		return sk->prot->setsockopt(sk,level,optname,optval,optlen);
}



static int inet_getsockopt(struct socket *sock, int level, int optname,
		    char *optval, int *optlen)
{
  	struct sock *sk = (struct sock *) sock->data;  	
  	if (level == SOL_SOCKET) 
  		return sock_getsockopt(sk,level,optname,optval,optlen);
  	if(sk->prot->getsockopt==NULL)  	
  		return(-EOPNOTSUPP);
  	else
  		return sk->prot->getsockopt(sk,level,optname,optval,optlen);
}


static int inet_autobind(struct sock *sk)
{
	/* We may need to bind the socket. */
	if (sk->num == 0) 
	{
		sk->num = get_new_socknum(sk->prot, 0);
		if (sk->num == 0) 
			return(-EAGAIN);
		put_sock(sk->num, sk);
		sk->dummy_th.source = ntohs(sk->num);
	}
	return 0;
}

static int inet_listen(struct socket *sock, int backlog)
{
	struct sock *sk = (struct sock *) sock->data;

	if(inet_autobind(sk)!=0)
		return -EAGAIN;

	/* We might as well re use these. */ 
	sk->max_ack_backlog = backlog;
	if (sk->state != TCP_LISTEN) 
	{
		sk->ack_backlog = 0;
		sk->state = TCP_LISTEN;
	}
	return(0);
}

/*
 *	Default callbacks for user INET sockets. These just wake up
 *	the user owning the socket.
 */

static void def_callback1(struct sock *sk)
{
	if(!sk->dead)
		wake_up_interruptible(sk->sleep);
}

static void def_callback2(struct sock *sk,int len)
{
	if(!sk->dead)
		wake_up_interruptible(sk->sleep);
}


/*
 *	Create an inet socket.
 *
 *	FIXME: Gcc would generate much better code if we set the parameters
 *	up in in-memory structure order. Gcc68K even more so
 */

static int inet_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	struct proto *prot;
	int err;

	sk = (struct sock *) kmalloc(sizeof(*sk), GFP_KERNEL);
	if (sk == NULL) 
		return(-ENOBUFS);
	sk->num = 0;
	sk->reuse = 0;
	switch(sock->type) 
	{
		case SOCK_STREAM:
		case SOCK_SEQPACKET:
			if (protocol && protocol != IPPROTO_TCP) 
			{
				kfree_s((void *)sk, sizeof(*sk));
				return(-EPROTONOSUPPORT);
			}
			protocol = IPPROTO_TCP;
			sk->no_check = TCP_NO_CHECK;
			prot = &tcp_prot;
			break;

		case SOCK_DGRAM:
			if (protocol && protocol != IPPROTO_UDP) 
			{
				kfree_s((void *)sk, sizeof(*sk));
				return(-EPROTONOSUPPORT);
			}
			protocol = IPPROTO_UDP;
			sk->no_check = UDP_NO_CHECK;
			prot=&udp_prot;
			break;
      
		case SOCK_RAW:
			if (!suser()) 
			{
				kfree_s((void *)sk, sizeof(*sk));
				return(-EPERM);
			}
			if (!protocol) 
			{
				kfree_s((void *)sk, sizeof(*sk));
				return(-EPROTONOSUPPORT);
			}
			prot = &raw_prot;
			sk->reuse = 1;
			sk->no_check = 0;	/*
						 * Doesn't matter no checksum is
						 * preformed anyway.
						 */
			sk->num = protocol;
			break;

		case SOCK_PACKET:
			if (!suser()) 
			{
				kfree_s((void *)sk, sizeof(*sk));
				return(-EPERM);
			}
			if (!protocol) 
			{
				kfree_s((void *)sk, sizeof(*sk));
				return(-EPROTONOSUPPORT);
			}
			prot = &packet_prot;
			sk->reuse = 1;
			sk->no_check = 0;	/* Doesn't matter no checksum is
						 * preformed anyway.
						 */
			sk->num = protocol;
			break;

		default:
			kfree_s((void *)sk, sizeof(*sk));
			return(-ESOCKTNOSUPPORT);
	}
	sk->socket = sock;
#ifdef CONFIG_TCP_NAGLE_OFF
	sk->nonagle = 1;
#else    
	sk->nonagle = 0;
#endif  
	sk->type = sock->type;
	sk->stamp.tv_sec=0;
	sk->protocol = protocol;
	sk->wmem_alloc = 0;
	sk->rmem_alloc = 0;
	sk->sndbuf = SK_WMEM_MAX;
	sk->rcvbuf = SK_RMEM_MAX;
	sk->pair = NULL;
	sk->opt = NULL;
	sk->write_seq = 0;
	sk->acked_seq = 0;
	sk->copied_seq = 0;
	sk->fin_seq = 0;
	sk->urg_seq = 0;
	sk->urg_data = 0;
	sk->proc = 0;
	sk->rtt = 0;				/*TCP_WRITE_TIME << 3;*/
	sk->rto = TCP_TIMEOUT_INIT;		/*TCP_WRITE_TIME*/
	sk->mdev = 0;
	sk->backoff = 0;
	sk->packets_out = 0;
	sk->cong_window = 1; /* start with only sending one packet at a time. */
	sk->cong_count = 0;
	sk->ssthresh = 0;
	sk->max_window = 0;
	sk->urginline = 0;
	sk->intr = 0;
	sk->linger = 0;
	sk->destroy = 0;
	sk->priority = 1;
	sk->shutdown = 0;
	sk->keepopen = 0;
	sk->zapped = 0;
	sk->done = 0;
	sk->ack_backlog = 0;
	sk->window = 0;
	sk->bytes_rcv = 0;
	sk->state = TCP_CLOSE;
	sk->dead = 0;
	sk->ack_timed = 0;
	sk->partial = NULL;
	sk->user_mss = 0;
	sk->debug = 0;

	/* this is how many unacked bytes we will accept for this socket.  */
	sk->max_unacked = 2048; /* needs to be at most 2 full packets. */

	/* how many packets we should send before forcing an ack. 
	   if this is set to zero it is the same as sk->delay_acks = 0 */
	sk->max_ack_backlog = 0;
	sk->inuse = 0;
	sk->delay_acks = 0;
	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->receive_queue);
	sk->mtu = 576;
	sk->prot = prot;
	sk->sleep = sock->wait;
	sk->daddr = 0;
	sk->saddr = ip_my_addr();
	sk->err = 0;
	sk->next = NULL;
	sk->pair = NULL;
	sk->send_tail = NULL;
	sk->send_head = NULL;
	sk->timeout = 0;
	sk->broadcast = 0;
	sk->localroute = 0;
	sk->timer.data = (unsigned long)sk;
	sk->timer.function = &net_timer;
	skb_queue_head_init(&sk->back_log);
	sk->blog = 0;
	sock->data =(void *) sk;
	sk->dummy_th.doff = sizeof(sk->dummy_th)/4;
	sk->dummy_th.res1=0;
	sk->dummy_th.res2=0;
	sk->dummy_th.urg_ptr = 0;
	sk->dummy_th.fin = 0;
	sk->dummy_th.syn = 0;
	sk->dummy_th.rst = 0;
	sk->dummy_th.psh = 0;
	sk->dummy_th.ack = 0;
	sk->dummy_th.urg = 0;
	sk->dummy_th.dest = 0;
	sk->ip_tos=0;
	sk->ip_ttl=64;
  	
	sk->state_change = def_callback1;
	sk->data_ready = def_callback2;
	sk->write_space = def_callback1;
	sk->error_report = def_callback1;

	if (sk->num) 
	{
	/*
	 * It assumes that any protocol which allows
	 * the user to assign a number at socket
	 * creation time automatically
	 * shares.
	 */
		put_sock(sk->num, sk);
		sk->dummy_th.source = ntohs(sk->num);
	}

	if (sk->prot->init) 
	{
		err = sk->prot->init(sk);
		if (err != 0) 
		{
			destroy_sock(sk);
			return(err);
		}
	}
	return(0);
}


/*
 *	Duplicate a socket.
 */
 
static int inet_dup(struct socket *newsock, struct socket *oldsock)
{
	return(inet_create(newsock,((struct sock *)(oldsock->data))->protocol));
}


/*
 *	The peer socket should always be NULL (or else). When we call this
 *	function we are destroying the object and from then on nobody
 *	should refer to it.
 */
 
static int inet_release(struct socket *sock, struct socket *peer)
{
	struct sock *sk = (struct sock *) sock->data;
	if (sk == NULL) 
		return(0);

	sk->state_change(sk);

	/* Start closing the connection.  This may take a while. */

	/*
	 * If linger is set, we don't return until the close
	 * is complete.  Other wise we return immediately. The
	 * actually closing is done the same either way.
	 */

	if (sk->linger == 0) 
	{
		sk->prot->close(sk,0);
		sk->dead = 1;
	} 
	else 
	{
		sk->prot->close(sk, 0);
		cli();
		if (sk->lingertime)
			current->timeout = jiffies + HZ*sk->lingertime;
		while(sk->state != TCP_CLOSE && current->timeout>0) 
		{
			interruptible_sleep_on(sk->sleep);
			if (current->signal & ~current->blocked) 
			{
				break;
#if 0
				/* not working now - closes can't be restarted */
				sti();
				current->timeout=0;
				return(-ERESTARTSYS);
#endif
			}
		}
		current->timeout=0;
		sti();
		sk->dead = 1;
	}
	sk->inuse = 1;

	/* This will destroy it. */
	release_sock(sk);
	sock->data = NULL;
	return(0);
}


/* this needs to be changed to dissallow
   the rebinding of sockets.   What error
   should it return? */

static int inet_bind(struct socket *sock, struct sockaddr *uaddr,
	       int addr_len)
{
	struct sockaddr_in addr;
	struct sock *sk=(struct sock *)sock->data, *sk2;
	unsigned short snum;
	int err;
	int chk_addr_ret;

	/* check this error. */
	if (sk->state != TCP_CLOSE)
		return(-EIO);
	if (sk->num != 0) 
		return(-EINVAL);

	err=verify_area(VERIFY_READ, uaddr, addr_len);
	if(err)
  		return err;
	memcpy_fromfs(&addr, uaddr, min(sizeof(addr), addr_len));

	snum = ntohs(addr.sin_port);

	/*
	 * We can't just leave the socket bound wherever it is, it might
	 * be bound to a privileged port. However, since there seems to
	 * be a bug here, we will leave it if the port is not privileged.
	 */
	if (snum == 0) 
	{
		snum = get_new_socknum(sk->prot, 0);
	}
	if (snum < PROT_SOCK && !suser()) 
		return(-EACCES);

	chk_addr_ret = ip_chk_addr(addr.sin_addr.s_addr);
	if (addr.sin_addr.s_addr != 0 && chk_addr_ret != IS_MYADDR)
		return(-EADDRNOTAVAIL);	/* Source address MUST be ours! */
  	
	if (chk_addr_ret || addr.sin_addr.s_addr == 0)
		sk->saddr = addr.sin_addr.s_addr;

	/* Make sure we are allowed to bind here. */
	cli();
outside_loop:
	for(sk2 = sk->prot->sock_array[snum & (SOCK_ARRAY_SIZE -1)];
					sk2 != NULL; sk2 = sk2->next) 
	{
/* should be below! */
		if (sk2->num != snum) continue;
		if (sk2->dead) 
		{
			destroy_sock(sk2);
			goto outside_loop;
		}
		if (!sk->reuse) 
		{
			sti();
			return(-EADDRINUSE);
		}
		
		if (sk2->num != snum) 
			continue;		/* more than one */
		if (sk2->saddr != sk->saddr) 
			continue;	/* socket per slot ! -FB */
		if (!sk2->reuse) 
		{
			sti();
			return(-EADDRINUSE);
		}
	}
	sti();

	remove_sock(sk);
	put_sock(snum, sk);
	sk->dummy_th.source = ntohs(sk->num);
	sk->daddr = 0;
	sk->dummy_th.dest = 0;
	return(0);
}

/*
 *	Handle sk->err properly. The cli/sti matter.
 */
 
static int inet_error(struct sock *sk)
{
	unsigned long flags;
	int err;
	save_flags(flags);
	cli();	
	err=sk->err;
	sk->err=0;
	sti();
	return -err;
}

/*
 *	Connect to a remote host. There is regretably still a little
 *	TCP 'magic' in here.
 */
 
static int inet_connect(struct socket *sock, struct sockaddr * uaddr,
		  int addr_len, int flags)
{
	struct sock *sk=(struct sock *)sock->data;
	int err;
	sock->conn = NULL;

	if (sock->state == SS_CONNECTING && sk->state == TCP_ESTABLISHED)
	{
		sock->state = SS_CONNECTED;
		/* Connection completing after a connect/EINPROGRESS/select/connect */
		return 0;	/* Rock and roll */
	}

	if (sock->state == SS_CONNECTING && sk->protocol == IPPROTO_TCP && (flags & O_NONBLOCK))
		return -EALREADY;	/* Connecting is currently in progress */
  	
	if (sock->state != SS_CONNECTING) 
	{
		/* We may need to bind the socket. */
		if(inet_autobind(sk)!=0)
			return(-EAGAIN);
		if (sk->prot->connect == NULL) 
			return(-EOPNOTSUPP);
		err = sk->prot->connect(sk, (struct sockaddr_in *)uaddr, addr_len);
		if (err < 0) 
			return(err);
  		sock->state = SS_CONNECTING;
	}

	if (sk->state != TCP_ESTABLISHED &&(flags & O_NONBLOCK)) 
	  	return(-EINPROGRESS);

	cli(); /* avoid the race condition */
	while(sk->state == TCP_SYN_SENT || sk->state == TCP_SYN_RECV) 
	{
		interruptible_sleep_on(sk->sleep);
		if (current->signal & ~current->blocked) 
		{
			sti();
			return(-ERESTARTSYS);
		}
		/* This fixes a nasty in the tcp/ip code. There is a hideous hassle with
		   icmp error packets wanting to close a tcp or udp socket. */
		if(sk->err && sk->protocol == IPPROTO_TCP)
		{
			sti();
			sock->state = SS_UNCONNECTED;
			err = -sk->err;
			sk->err=0;
			return err; /* set by tcp_err() */
		}
	}
	sti();
	sock->state = SS_CONNECTED;

	if (sk->state != TCP_ESTABLISHED && sk->err) 
	{
		sock->state = SS_UNCONNECTED;
		err=sk->err;
		sk->err=0;
		return(-err);
	}
	return(0);
}


static int inet_socketpair(struct socket *sock1, struct socket *sock2)
{
	 return(-EOPNOTSUPP);
}


/*
 *	FIXME: Get BSD behaviour
 */

static int inet_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sock *sk1, *sk2;
	int err;

	sk1 = (struct sock *) sock->data;

	/*
	 * We've been passed an extra socket.
	 * We need to free it up because the tcp module creates
	 * it's own when it accepts one.
	 */
	if (newsock->data)
	{
	  	struct sock *sk=(struct sock *)newsock->data;
	  	newsock->data=NULL;
	  	sk->dead = 1;
	  	destroy_sock(sk);
	}
  
	if (sk1->prot->accept == NULL) 
		return(-EOPNOTSUPP);

	/* Restore the state if we have been interrupted, and then returned. */
	if (sk1->pair != NULL ) 
	{
		sk2 = sk1->pair;
		sk1->pair = NULL;
	} 
	else
	{
		sk2 = sk1->prot->accept(sk1,flags);
		if (sk2 == NULL) 
		{
			if (sk1->err <= 0)
				printk("Warning sock.c:sk1->err <= 0.  Returning non-error.\n");
			err=sk1->err;
			sk1->err=0;
			return(-err);
		}
	}
	newsock->data = (void *)sk2;
	sk2->sleep = newsock->wait;
	newsock->conn = NULL;
	if (flags & O_NONBLOCK) 
		return(0);

	cli(); /* avoid the race. */
	while(sk2->state == TCP_SYN_RECV) 
	{
		interruptible_sleep_on(sk2->sleep);
		if (current->signal & ~current->blocked) 
		{
			sti();
			sk1->pair = sk2;
			sk2->sleep = NULL;
			newsock->data = NULL;
			return(-ERESTARTSYS);
		}
	}
	sti();

	if (sk2->state != TCP_ESTABLISHED && sk2->err > 0) 
	{
		err = -sk2->err;
		sk2->err=0;
		destroy_sock(sk2);
		newsock->data = NULL;
		return(err);
	}
	newsock->state = SS_CONNECTED;
	return(0);
}


/*
 *	This does both peername and sockname.
 */
 
static int inet_getname(struct socket *sock, struct sockaddr *uaddr,
		 int *uaddr_len, int peer)
{
	struct sockaddr_in sin;
	struct sock *sk;
	int len;
	int err;
  
  
	err = verify_area(VERIFY_WRITE,uaddr_len,sizeof(long));
	if(err)
		return err;
  	
	len=get_fs_long(uaddr_len);
  
	err = verify_area(VERIFY_WRITE, uaddr, len);
	if(err)
		return err;
  	
	/* Check this error. */
	if (len < sizeof(sin)) 
		return(-EINVAL);

	sin.sin_family = AF_INET;
	sk = (struct sock *) sock->data;
	if (peer) 
	{
		if (!tcp_connected(sk->state)) 
			return(-ENOTCONN);
		sin.sin_port = sk->dummy_th.dest;
		sin.sin_addr.s_addr = sk->daddr;
	} 
	else 
	{
		sin.sin_port = sk->dummy_th.source;
		if (sk->saddr == 0) 
			sin.sin_addr.s_addr = ip_my_addr();
		else 
			sin.sin_addr.s_addr = sk->saddr;
	}
	len = sizeof(sin);
	memcpy_tofs(uaddr, &sin, sizeof(sin));
	put_fs_long(len, uaddr_len);
	return(0);
}


/*
 *	The assorted BSD I/O operations
 */


static int inet_recv(struct socket *sock, void *ubuf, int size, int noblock,
	  unsigned flags)
{
	struct sock *sk = (struct sock *) sock->data;
	int err;
	
	if(sk->err)
		return inet_error(sk);
	if(size<0)
		return -EINVAL;
	if(size==0)
		return 0;
	err=verify_area(VERIFY_WRITE,ubuf,size);
	if(err)
		return err;

	/* We may need to bind the socket. */
	if(inet_autobind(sk))
		return(-EAGAIN);	
	return(sk->prot->read(sk, (unsigned char *) ubuf, size, noblock, flags));
}


static int inet_read(struct socket *sock, char *ubuf, int size, int noblock)
{
	return inet_recv(sock,ubuf,size,noblock,0);
}

static int inet_send(struct socket *sock, void *ubuf, int size, int noblock, 
	       unsigned flags)
{
	struct sock *sk = (struct sock *) sock->data;
	int err;
	if (sk->shutdown & SEND_SHUTDOWN) 
	{
		send_sig(SIGPIPE, current, 1);
		return(-EPIPE);
	}
	if(sk->err)
		return inet_error(sk);
	if(size<0)
		return -EINVAL;
	if(size==0)
		return 0;
	err=verify_area(VERIFY_READ,ubuf,size);
	if(err)
		return err;
	/* We may need to bind the socket. */
	if(inet_autobind(sk)!=0)
		return(-EAGAIN);
	return(sk->prot->write(sk, (unsigned char *) ubuf, size, noblock, flags));
}

static int inet_write(struct socket *sock, char *ubuf, int size, int noblock)
{
	return inet_send(sock,ubuf,size,noblock,0);
}

static int inet_sendto(struct socket *sock, void *ubuf, int size, int noblock, 
	    unsigned flags, struct sockaddr *sin, int addr_len)
{
	int err;
	struct sock *sk = (struct sock *) sock->data;
	if (sk->shutdown & SEND_SHUTDOWN) 
	{
		send_sig(SIGPIPE, current, 1);
		return(-EPIPE);
	}
	if (sk->prot->sendto == NULL) 
		return(-EOPNOTSUPP);
	if(sk->err)
		return inet_error(sk);
	if(size<0)
		return -EINVAL;
	if(size==0)
		return 0;
	err=verify_area(VERIFY_READ,ubuf,size);
	if(err)
		return err;

	/* We may need to bind the socket. */
	
	if(inet_autobind(sk)!=0)
		return -EAGAIN;
	return(sk->prot->sendto(sk, (unsigned char *) ubuf, size, noblock, flags, 
			   (struct sockaddr_in *)sin, addr_len));
}


static int inet_recvfrom(struct socket *sock, void *ubuf, int size, int noblock, 
		   unsigned flags, struct sockaddr *sin, int *addr_len )
{
	struct sock *sk = (struct sock *) sock->data;
	int err;
	
	if (sk->prot->recvfrom == NULL) 
		return(-EOPNOTSUPP);
	if(sk->err)
		return inet_error(sk);
	if(size<0)
		return -EINVAL;
	if(size==0)
		return 0;
	err=verify_area(VERIFY_READ,ubuf,size);
	if(err)
		return err;

	/* We may need to bind the socket. */
	if(inet_autobind(sk)!=0)
		return(-EAGAIN);
	return(sk->prot->recvfrom(sk, (unsigned char *) ubuf, size, noblock, flags,
			     (struct sockaddr_in*)sin, addr_len));
}


static int inet_shutdown(struct socket *sock, int how)
{
	struct sock *sk=(struct sock*)sock->data;

	/*
	 * This should really check to make sure
	 * the socket is a TCP socket. (WHY AC...)
	 */
	how++; /* maps 0->1 has the advantage of making bit 1 rcvs and
		       1->2 bit 2 snds.
		       2->3 */
	if ((how & ~SHUTDOWN_MASK) || how==0)	/* MAXINT->0 */
		return(-EINVAL);
	if (sock->state == SS_CONNECTING && sk->state == TCP_ESTABLISHED)
		sock->state = SS_CONNECTED;
	if (!tcp_connected(sk->state)) 
		return(-ENOTCONN);
	sk->shutdown |= how;
	if (sk->prot->shutdown)
		sk->prot->shutdown(sk, how);
	return(0);
}


static int inet_select(struct socket *sock, int sel_type, select_table *wait )
{
	struct sock *sk=(struct sock *) sock->data;
	if (sk->prot->select == NULL) 
	{
		return(0);
	}
	return(sk->prot->select(sk, sel_type, wait));
}

/*
 *	ioctl() calls you can issue on an INET socket. Most of these are
 *	device configuration and stuff and very rarely used. Some ioctls
 *	pass on to the socket itself.
 *
 *	NOTE: I like the idea of a module for the config stuff. ie ifconfig
 *	loads the devconfigure module does its configuring and unloads it.
 *	Theres a good 20K of config code hanging around the kernel.
 */

static int inet_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk=(struct sock *)sock->data;
	int err;

	switch(cmd) 
	{
		case FIOSETOWN:
		case SIOCSPGRP:
			err=verify_area(VERIFY_READ,(int *)arg,sizeof(long));
			if(err)
				return err;
			sk->proc = get_fs_long((int *) arg);
			return(0);
		case FIOGETOWN:
		case SIOCGPGRP:
			err=verify_area(VERIFY_WRITE,(void *) arg, sizeof(long));
			if(err)
				return err;
			put_fs_long(sk->proc,(int *)arg);
			return(0);			
		case SIOCGSTAMP:
			if(sk->stamp.tv_sec==0)
				return -ENOENT;
			err=verify_area(VERIFY_WRITE,(void *)arg,sizeof(struct timeval));
			if(err)
				return err;
			memcpy_tofs((void *)arg,&sk->stamp,sizeof(struct timeval));
			return 0;
		case SIOCADDRT: case SIOCADDRTOLD:
		case SIOCDELRT: case SIOCDELRTOLD:
			return(ip_rt_ioctl(cmd,(void *) arg));
		case SIOCDARP:
		case SIOCGARP:
		case SIOCSARP:
			return(arp_ioctl(cmd,(void *) arg));
#ifdef CONFIG_INET_RARP			
		case SIOCDRARP:
		case SIOCGRARP:
		case SIOCSRARP:
			return(rarp_ioctl(cmd,(void *) arg));
#endif
		case SIOCGIFCONF:
		case SIOCGIFFLAGS:
		case SIOCSIFFLAGS:
		case SIOCGIFADDR:
		case SIOCSIFADDR:
		case SIOCGIFDSTADDR:
		case SIOCSIFDSTADDR:
		case SIOCGIFBRDADDR:
		case SIOCSIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCSIFNETMASK:
		case SIOCGIFMETRIC:
		case SIOCSIFMETRIC:
		case SIOCGIFMEM:
		case SIOCSIFMEM:
		case SIOCGIFMTU:
		case SIOCSIFMTU:
		case SIOCSIFLINK:
		case SIOCGIFHWADDR:
		case SIOCSIFHWADDR:
		case OLD_SIOCGIFHWADDR:
		case SIOCSIFMAP:
		case SIOCGIFMAP:
		case SIOCDEVPRIVATE:
			return(dev_ioctl(cmd,(void *) arg));

		default:
			if (sk->prot->ioctl==NULL) 
				return(-EINVAL);
			return(sk->prot->ioctl(sk, cmd, arg));
	}
	/*NOTREACHED*/
	return(0);
}

/*
 * This routine must find a socket given a TCP or UDP header.
 * Everyhting is assumed to be in net order.
 */

struct sock *get_sock(struct proto *prot, unsigned short num,
				unsigned long raddr,
				unsigned short rnum, unsigned long laddr)
{
	struct sock *s;
	unsigned short hnum;

	hnum = ntohs(num);

	/*
	 * SOCK_ARRAY_SIZE must be a power of two.  This will work better
	 * than a prime unless 3 or more sockets end up using the same
	 * array entry.  This should not be a problem because most
	 * well known sockets don't overlap that much, and for
	 * the other ones, we can just be careful about picking our
	 * socket number when we choose an arbitrary one.
	 */

	for(s = prot->sock_array[hnum & (SOCK_ARRAY_SIZE - 1)];
			s != NULL; s = s->next) 
	{
		if (s->num != hnum) 
			continue;
		if(s->dead && (s->state == TCP_CLOSE))
			continue;
		if(prot == &udp_prot)
			return s;
		if(ip_addr_match(s->daddr,raddr)==0)
			continue;
		if (s->dummy_th.dest != rnum && s->dummy_th.dest != 0) 
			continue;
		if(ip_addr_match(s->saddr,laddr) == 0)
			continue;
		return(s);
  	}
  	return(NULL);
}

static struct proto_ops inet_proto_ops = {
	AF_INET,

	inet_create,
	inet_dup,
	inet_release,
	inet_bind,
	inet_connect,
	inet_socketpair,
	inet_accept,
	inet_getname, 
	inet_read,
	inet_write,
	inet_select,
	inet_ioctl,
	inet_listen,
	inet_send,
	inet_recv,
	inet_sendto,
	inet_recvfrom,
	inet_shutdown,
	inet_setsockopt,
	inet_getsockopt,
	inet_fcntl,
};

extern unsigned long seq_offset;

/*
 *	Called by socket.c on kernel startup.  
 */
 
void inet_proto_init(struct net_proto *pro)
{
	struct inet_protocol *p;
	int i;

	printk("Swansea University Computer Society NET3.014\n");

	/*
	 *	Tell SOCKET that we are alive... 
	 */
   
  	(void) sock_register(inet_proto_ops.family, &inet_proto_ops);

  	seq_offset = CURRENT_TIME*250;

	/*
	 *	Add all the protocols. 
	 */
	 
	for(i = 0; i < SOCK_ARRAY_SIZE; i++) 
	{
		tcp_prot.sock_array[i] = NULL;
		udp_prot.sock_array[i] = NULL;
		raw_prot.sock_array[i] = NULL;
  	}

	printk("IP Protocols: ");
	for(p = inet_protocol_base; p != NULL;) 
	{
		struct inet_protocol *tmp = (struct inet_protocol *) p->next;
		inet_add_protocol(p);
		printk("%s%s",p->name,tmp?", ":"\n");
		p = tmp;
	}
	/*
	 *	Set the ARP module up
	 */
	arp_init();
  	/*
  	 *	Set the IP module up
  	 */
	ip_init();
}


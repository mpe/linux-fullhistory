/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Generic socket support routines. Memory allocators, socket lock/release
 *		handler for protocols to use and generic option handler.
 *
 *
 * Version:	@(#)sock.c	1.0.17	06/02/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Alan Cox, <A.Cox@swansea.ac.uk>
 *
 * Fixes:
 *		Alan Cox	: 	Numerous verify_area() problems
 *		Alan Cox	:	Connecting on a connecting socket
 *					now returns an error for tcp.
 *		Alan Cox	:	sock->protocol is set correctly.
 *					and is not sometimes left as 0.
 *		Alan Cox	:	connect handles icmp errors on a
 *					connect properly. Unfortunately there
 *					is a restart syscall nasty there. I
 *					can't match BSD without hacking the C
 *					library. Ideas urgently sought!
 *		Alan Cox	:	Disallow bind() to addresses that are
 *					not ours - especially broadcast ones!!
 *		Alan Cox	:	Socket 1024 _IS_ ok for users. (fencepost)
 *		Alan Cox	:	sock_wfree/sock_rfree don't destroy sockets,
 *					instead they leave that for the DESTROY timer.
 *		Alan Cox	:	Clean up error flag in accept
 *		Alan Cox	:	TCP ack handling is buggy, the DESTROY timer
 *					was buggy. Put a remove_sock() in the handler
 *					for memory when we hit 0. Also altered the timer
 *					code. The ACK stuff can wait and needs major 
 *					TCP layer surgery.
 *		Alan Cox	:	Fixed TCP ack bug, removed remove sock
 *					and fixed timer/inet_bh race.
 *		Alan Cox	:	Added zapped flag for TCP
 *		Alan Cox	:	Move kfree_skb into skbuff.c and tidied up surplus code
 *		Alan Cox	:	for new sk_buff allocations wmalloc/rmalloc now call alloc_skb
 *		Alan Cox	:	kfree_s calls now are kfree_skbmem so we can track skb resources
 *		Alan Cox	:	Supports socket option broadcast now as does udp. Packet and raw need fixing.
 *		Alan Cox	:	Added RCVBUF,SNDBUF size setting. It suddenly occurred to me how easy it was so...
 *		Rick Sladkey	:	Relaxed UDP rules for matching packets.
 *		C.E.Hawkins	:	IFF_PROMISC/SIOCGHWADDR support
 *	Pauline Middelink	:	identd support
 *		Alan Cox	:	Fixed connect() taking signals I think.
 *		Alan Cox	:	SO_LINGER supported
 *		Alan Cox	:	Error reporting fixes
 *		Anonymous	:	inet_create tidied up (sk->reuse setting)
 *		Alan Cox	:	inet sockets don't set sk->type!
 *		Alan Cox	:	Split socket option code
 *		Alan Cox	:	Callbacks
 *		Alan Cox	:	Nagle flag for Charles & Johannes stuff
 *		Alex		:	Removed restriction on inet fioctl
 *		Alan Cox	:	Splitting INET from NET core
 *		Alan Cox	:	Fixed bogus SO_TYPE handling in getsockopt()
 *		Adam Caldwell	:	Missing return in SO_DONTROUTE/SO_DEBUG code
 *		Alan Cox	:	Split IP from generic code
 *		Alan Cox	:	New kfree_skbmem()
 *		Alan Cox	:	Make SO_DEBUG superuser only.
 *		Alan Cox	:	Allow anyone to clear SO_DEBUG
 *					(compatibility fix)
 *		Alan Cox	:	Added optimistic memory grabbing for AF_UNIX throughput.
 *		Alan Cox	:	Allocator for a socket is settable.
 *		Alan Cox	:	SO_ERROR includes soft errors.
 *		Alan Cox	:	Allow NULL arguments on some SO_ opts
 *		Alan Cox	: 	Generic socket allocation to make hooks
 *					easier (suggested by Craig Metz).
 *
 * To Fix:
 *
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
#include <net/ip.h>
#include <net/protocol.h>
#include <net/arp.h>
#include <net/rarp.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/raw.h>
#include <net/icmp.h>

#define min(a,b)	((a)<(b)?(a):(b))

/*
 *	This is meant for all protocols to use and covers goings on
 *	at the socket level. Everything here is generic.
 */

int sock_setsockopt(struct sock *sk, int level, int optname,
		char *optval, int optlen)
{
	int val;
	int valbool;
	int err;
	struct linger ling;

	/*
	 *	Options without arguments
	 */

#ifdef SO_DONTLINGER		/* Compatibility item... */
	switch(optname)
	{
		case SO_DONTLINGER:
			sk->linger=0;
			return 0;
	}
#endif	
		
  	if (optval == NULL) 
  		return(-EINVAL);

  	err=verify_area(VERIFY_READ, optval, sizeof(int));
  	if(err)
  		return err;
  	
  	val = get_user((int *)optval);
  	valbool = val?1:0;
  	
  	switch(optname) 
  	{
		case SO_DEBUG:	
			if(val && !suser())
				return(-EPERM);
			sk->debug=valbool;
			return 0;
		case SO_REUSEADDR:
			sk->reuse = valbool;
			return(0);
		case SO_TYPE:
		case SO_ERROR:
		  	return(-ENOPROTOOPT);
		case SO_DONTROUTE:
			sk->localroute=valbool;
			return 0;
		case SO_BROADCAST:
			sk->broadcast=valbool;
			return 0;
		case SO_SNDBUF:
			if(val > SK_WMEM_MAX*2)
				val = SK_WMEM_MAX*2;
			if(val < 256)
				val = 256;
			if(val > 65535)
				val = 65535;
			sk->sndbuf = val;
			return 0;

		case SO_RCVBUF:
			if(val > SK_RMEM_MAX*2)
			 	val = SK_RMEM_MAX*2;
			if(val < 256)
				val = 256;
			if(val > 65535)
				val = 65535;
			sk->rcvbuf = val;
			return(0);

		case SO_KEEPALIVE:
			sk->keepopen = valbool;
			return(0);

	 	case SO_OOBINLINE:
			sk->urginline = valbool;
			return(0);

	 	case SO_NO_CHECK:
			sk->no_check = valbool;
			return(0);

		case SO_PRIORITY:
			if (val >= 0 && val < DEV_NUMBUFFS) 
			{
				sk->priority = val;
			} 
			else 
			{
				return(-EINVAL);
			}
			return(0);


		case SO_LINGER:
			err=verify_area(VERIFY_READ,optval,sizeof(ling));
			if(err)
				return err;
			memcpy_fromfs(&ling,optval,sizeof(ling));
			if(ling.l_onoff==0)
				sk->linger=0;
			else
			{
				sk->lingertime=ling.l_linger;
				sk->linger=1;
			}
			return 0;

		case SO_BSDCOMPAT:
			sk->bsdism = valbool;
			return 0;
			
		default:
		  	return(-ENOPROTOOPT);
  	}
}


int sock_getsockopt(struct sock *sk, int level, int optname,
		   char *optval, int *optlen)
{		
  	int val;
  	int err;
  	struct linger ling;

  	switch(optname) 
  	{
		case SO_DEBUG:		
			val = sk->debug;
			break;
		
		case SO_DONTROUTE:
			val = sk->localroute;
			break;
		
		case SO_BROADCAST:
			val= sk->broadcast;
			break;

		case SO_SNDBUF:
			val=sk->sndbuf;
			break;
		
		case SO_RCVBUF:
			val =sk->rcvbuf;
			break;

		case SO_REUSEADDR:
			val = sk->reuse;
			break;

		case SO_KEEPALIVE:
			val = sk->keepopen;
			break;

		case SO_TYPE:
			val = sk->type;		  		
			break;

		case SO_ERROR:
			val = sock_error(sk);
			if(val==0)
				val=xchg(&sk->err_soft,0);
			break;

		case SO_OOBINLINE:
			val = sk->urginline;
			break;
	
		case SO_NO_CHECK:
			val = sk->no_check;
			break;

		case SO_PRIORITY:
			val = sk->priority;
			break;
		
		case SO_LINGER:	
			err=verify_area(VERIFY_WRITE,optval,sizeof(ling));
			if(err)
				return err;
			err=verify_area(VERIFY_WRITE,optlen,sizeof(int));
			if(err)
				return err;
			put_fs_long(sizeof(ling),(unsigned long *)optlen);
			ling.l_onoff=sk->linger;
			ling.l_linger=sk->lingertime;
			memcpy_tofs(optval,&ling,sizeof(ling));
			return 0;
		
		case SO_BSDCOMPAT:
			val = sk->bsdism;
			break;

		default:
			return(-ENOPROTOOPT);
	}
	err=verify_area(VERIFY_WRITE, optlen, sizeof(int));
	if(err)
  		return err;
  	put_fs_long(sizeof(int),(unsigned long *) optlen);

  	err=verify_area(VERIFY_WRITE, optval, sizeof(int));
  	if(err)
  		return err;
  	put_fs_long(val,(unsigned long *)optval);

  	return(0);
}

struct sock *sk_alloc(int priority)
{
	struct sock *sk=(struct sock *)kmalloc(sizeof(*sk), priority);
	if(!sk)
		return NULL;
	memset(sk, 0, sizeof(*sk));
	return sk;
}

void sk_free(struct sock *sk)
{
	kfree_s(sk,sizeof(*sk));
}


struct sk_buff *sock_wmalloc(struct sock *sk, unsigned long size, int force, int priority)
{
	if (sk) {
		if (force || sk->wmem_alloc + size < sk->sndbuf) {
			struct sk_buff * skb = alloc_skb(size, priority);
			if (skb)
				atomic_add(skb->truesize, &sk->wmem_alloc);
			return skb;
		}
		return NULL;
	}
	return alloc_skb(size, priority);
}

struct sk_buff *sock_rmalloc(struct sock *sk, unsigned long size, int force, int priority)
{
	if (sk) {
		if (force || sk->rmem_alloc + size < sk->rcvbuf) {
			struct sk_buff *skb = alloc_skb(size, priority);
			if (skb)
				atomic_add(skb->truesize, &sk->rmem_alloc);
			return skb;
		}
		return NULL;
	}
	return alloc_skb(size, priority);
}


unsigned long sock_rspace(struct sock *sk)
{
	int amt;

	if (sk != NULL) 
	{
		if (sk->rmem_alloc >= sk->rcvbuf-2*MIN_WINDOW) 
			return(0);
		amt = min((sk->rcvbuf-sk->rmem_alloc)/2-MIN_WINDOW, MAX_WINDOW);
		if (amt < 0) 
			return(0);
		return(amt);
	}
	return(0);
}


unsigned long sock_wspace(struct sock *sk)
{
	if (sk != NULL) 
	{
		if (sk->shutdown & SEND_SHUTDOWN)
			return(0);
		if (sk->wmem_alloc >= sk->sndbuf)
			return(0);
		return sk->sndbuf - sk->wmem_alloc;
	}
	return(0);
}


void sock_wfree(struct sock *sk, struct sk_buff *skb)
{
	int s=skb->truesize;
#if CONFIG_SKB_CHECK
	IS_SKB(skb);
#endif
	kfree_skbmem(skb);
	if (sk) 
	{
		/* In case it might be waiting for more memory. */
		sk->write_space(sk);
		atomic_sub(s, &sk->wmem_alloc);
	}
}


void sock_rfree(struct sock *sk, struct sk_buff *skb)
{
	int s=skb->truesize;
#if CONFIG_SKB_CHECK
	IS_SKB(skb);
#endif	
	kfree_skbmem(skb);
	if (sk) 
	{
		atomic_sub(s, &sk->rmem_alloc);
	}
}

/*
 *	Generic send/receive buffer handlers
 */

struct sk_buff *sock_alloc_send_skb(struct sock *sk, unsigned long size, unsigned long fallback, int noblock, int *errcode)
{
	struct sk_buff *skb;
	int err;

	do
	{
		if(sk->err!=0)
		{
			cli();
			err= -sk->err;
			sk->err=0;
			sti();
			*errcode=err;
			return NULL;
		}
		
		if(sk->shutdown&SEND_SHUTDOWN)
		{
			*errcode=-EPIPE;
			return NULL;
		}
		
		if(!fallback)
			skb = sock_wmalloc(sk, size, 0, sk->allocation);
		else
		{
			/* The buffer get won't block, or use the atomic queue. It does
			   produce annoying no free page messages still.... */
			skb = sock_wmalloc(sk, size, 0 , GFP_BUFFER);
			if(!skb)
				skb=sock_wmalloc(sk, fallback, 0, GFP_KERNEL);
		}
		
		/*
		 *	This means we have too many buffers for this socket already.
		 */
		 
		if(skb==NULL)
		{
			unsigned long tmp;

			sk->socket->flags |= SO_NOSPACE;
			if(noblock)
			{
				*errcode=-EAGAIN;
				return NULL;
			}
			if(sk->shutdown&SEND_SHUTDOWN)
			{
				*errcode=-EPIPE;
				return NULL;
			}
			tmp = sk->wmem_alloc;
			cli();
			if(sk->shutdown&SEND_SHUTDOWN)
			{
				sti();
				*errcode=-EPIPE;
				return NULL;
			}
			
#if 1
			if( tmp <= sk->wmem_alloc)
#else
			/* ANK: Line above seems either incorrect
			 *	or useless. sk->wmem_alloc has a tiny chance to change
			 *	between tmp = sk->w... and cli(),
			 *	but it might(?) change earlier. In real life
			 *	it does not (I never seen the message).
			 *	In any case I'd delete this check at all, or
			 *	change it to:
			 */
			if (sk->wmem_alloc + size >= sk->sndbuf) 
#endif
			{
				sk->socket->flags &= ~SO_NOSPACE;
				interruptible_sleep_on(sk->sleep);
				if (current->signal & ~current->blocked) 
				{
					sti();
					*errcode = -ERESTARTSYS;
					return NULL;
				}
			}
			sti();
		}
	}
	while(skb==NULL);
		
	return skb;
}


void __release_sock(struct sock *sk)
{
#ifdef CONFIG_INET
	if (!sk->prot || !sk->prot->rcv)
		return;
		
	/* See if we have any packets built up. */
	start_bh_atomic();
	while (!skb_queue_empty(&sk->back_log)) {
		struct sk_buff * skb = sk->back_log.next;
		__skb_unlink(skb, &sk->back_log);
		sk->prot->rcv(skb, skb->dev, (struct options*)skb->proto_priv,
			      skb->saddr, skb->len, skb->daddr, 1,
			      /* Only used for/by raw sockets. */
			      (struct inet_protocol *)sk->pair); 
	}
	end_bh_atomic();
#endif  
}

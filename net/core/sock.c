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
 *		Michael Pall	:	SO_ERROR returns positive errno again
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

#include <asm/uaccess.h>
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

int sock_setsockopt(struct socket *sock, int level, int optname,
		    char *optval, int optlen)
{
	struct sock *sk=sock->sk;
	int val;
	int valbool;
	int err;
	struct linger ling;
	int ret = 0;

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
  	
	err = get_user(val, (int *)optval);
	if (err)
		return err;
	
  	valbool = val?1:0;
  	
  	switch(optname) 
  	{
		case SO_DEBUG:	
			if(val && !suser())
			{
				ret = -EPERM;
			}
			else
				sk->debug=valbool;
			break;
		case SO_REUSEADDR:
			sk->reuse = valbool;
			break;
		case SO_TYPE:
		case SO_ERROR:
			ret = -ENOPROTOOPT;
		  	break;
		case SO_DONTROUTE:
			sk->localroute=valbool;
			break;
		case SO_BROADCAST:
			sk->broadcast=valbool;
			break;
		case SO_SNDBUF:
			if(val > SK_WMEM_MAX*2)
				val = SK_WMEM_MAX*2;
			if(val < 256)
				val = 256;
			if(val > 65535)
				val = 65535;
			sk->sndbuf = val;
			break;

		case SO_RCVBUF:
			if(val > SK_RMEM_MAX*2)
			 	val = SK_RMEM_MAX*2;
			if(val < 256)
				val = 256;
			if(val > 65535)
				val = 65535;
			sk->rcvbuf = val;
			break;

		case SO_KEEPALIVE:
#ifdef CONFIG_INET
			if (sk->protocol == IPPROTO_TCP)
			{
				tcp_set_keepalive(sk, valbool);
			}
#endif
			sk->keepopen = valbool;
			break;

	 	case SO_OOBINLINE:
			sk->urginline = valbool;
			break;

	 	case SO_NO_CHECK:
			sk->no_check = valbool;
			break;

		case SO_PRIORITY:
			if (val >= 0 && val < DEV_NUMBUFFS) 
			{
				sk->priority = val;
			} 
			else
			{
				return(-EINVAL);
			}
			break;


		case SO_LINGER:
			err = copy_from_user(&ling,optval,sizeof(ling));
			if (err)
			{
				ret = -EFAULT;
				break;
			}
			if(ling.l_onoff==0)
				sk->linger=0;
			else
			{
				sk->lingertime=ling.l_linger;
				sk->linger=1;
			}
			break;

		case SO_BSDCOMPAT:
			sk->bsdism = valbool;
			break;

		case SO_PASSCRED:
			sock->passcred = valbool;
			break;

		/* We implementation the SO_SNDLOWAT etc to
		   not be settable (1003.1g 5.3) */
		default:
		  	return(-ENOPROTOOPT);
  	}
	return ret;
}


int sock_getsockopt(struct socket *sock, int level, int optname,
		    char *optval, int *optlen)
{
	struct sock *sk = sock->sk;
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
			val = -sock_error(sk);
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
			err = put_user(sizeof(ling), optlen);
			if (!err) {
				ling.l_onoff=sk->linger;
				ling.l_linger=sk->lingertime;
				err = copy_to_user(optval,&ling,sizeof(ling));
				if (err)
				    err = -EFAULT;
			}
			return err;
		
		case SO_BSDCOMPAT:
			val = sk->bsdism;
			break;
			
		case SO_RCVTIMEO:
		case SO_SNDTIMEO:
		{
			static struct timeval tm={0,0};
			int err=copy_to_user(optval,&tm,sizeof(tm));
			if(err!=sizeof(struct timeval))
				return -EFAULT;
			return 0;
		}
		case SO_RCVLOWAT:
		case SO_SNDLOWAT:
			val=1;

		case SO_PASSCRED:
			val = sock->passcred;
			break;

		case SO_PEERCRED:
			err = put_user(sizeof(sk->peercred), optlen);
			if (!err)
				err = copy_to_user((void*)optval, &sk->peercred, sizeof(struct ucred));
			return err;

		default:
			return(-ENOPROTOOPT);
	}
  	err = put_user(sizeof(int), optlen);
	if (!err)
		err = put_user(val,(unsigned int *)optval);

  	return err;
}

/*
 *	All socket objects are allocated here. This is for future
 *	usage.
 */
 
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

/*
 *	Simple resource managers for sockets.
 */
 
void sock_wfree(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
#if CONFIG_SKB_CHECK
	IS_SKB(skb);
#endif
#if 1
	if (!sk) {
		printk("sock_wfree: sk==NULL\n");
		return;
	}
#endif
	/* In case it might be waiting for more memory. */
	atomic_sub(skb->truesize, &sk->wmem_alloc);
	sk->write_space(sk);
}


void sock_rfree(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
#if CONFIG_SKB_CHECK
	IS_SKB(skb);
#endif	
#if 1
	if (!sk) {
		printk("sock_rfree: sk==NULL\n");
		return;
	}
#endif
	atomic_sub(skb->truesize, &sk->rmem_alloc);
}


struct sk_buff *sock_wmalloc(struct sock *sk, unsigned long size, int force, int priority)
{
	if (force || sk->wmem_alloc < sk->sndbuf) {
		struct sk_buff * skb = alloc_skb(size, priority);
		if (skb) {
			atomic_add(skb->truesize, &sk->wmem_alloc);
			skb->destructor = sock_wfree;
			skb->sk = sk;
		}
		return skb;
	}
	return NULL;
}

struct sk_buff *sock_rmalloc(struct sock *sk, unsigned long size, int force, int priority)
{
	if (force || sk->rmem_alloc < sk->rcvbuf) {
		struct sk_buff *skb = alloc_skb(size, priority);
		if (skb) {
			atomic_add(skb->truesize, &sk->rmem_alloc);
			skb->destructor = sock_rfree;
			skb->sk = sk;
		}
		return skb;
	}
	return NULL;
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
			/*
			 *	FIXME: Check 1003.1g should we deliver
			 *	a signal here ???
			 */
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
	if (!sk->prot || !sk->backlog_rcv)
		return;
		
	/* See if we have any packets built up. */
	start_bh_atomic();
	while (!skb_queue_empty(&sk->back_log)) {
		struct sk_buff * skb = sk->back_log.next;
		__skb_unlink(skb, &sk->back_log);
		sk->backlog_rcv(sk, skb);
	}
	end_bh_atomic();
#endif  
}


/*
 *	Generic socket manager library. Most simpler socket families
 *	use this to manage their socket lists. At some point we should
 *	hash these. By making this generic we get the lot hashed for free.
 */
 
void sklist_remove_socket(struct sock **list, struct sock *sk)
{
	unsigned long flags;
	struct sock *s;

	save_flags(flags);
	cli();

	s= *list;
	if(s==sk)
	{
		*list = s->next;
		restore_flags(flags);
		return;
	}
	while(s && s->next)
	{
		if(s->next==sk)
		{
			s->next=sk->next;
			restore_flags(flags);
			return;
		}
		s=s->next;
	}
	restore_flags(flags);
}

void sklist_insert_socket(struct sock **list, struct sock *sk)
{
	unsigned long flags;
	save_flags(flags);
	cli();
	sk->next= *list;
	*list=sk;
	restore_flags(flags);
}

/*
 *	This is only called from user mode. Thus it protects itself against
 *	interrupt users but doesn't worry about being called during work.
 *	Once it is removed from the queue no interrupt or bottom half will
 *	touch it and we are (fairly 8-) ) safe.
 */

void sklist_destroy_socket(struct sock **list, struct sock *sk);

/*
 *	Handler for deferred kills.
 */

static void sklist_destroy_timer(unsigned long data)
{
	struct sock *sk=(struct sock *)data;
	sklist_destroy_socket(NULL,sk);
}

/*
 *	Destroy a socket. We pass NULL for a list if we know the
 *	socket is not on a list.
 */
 
void sklist_destroy_socket(struct sock **list,struct sock *sk)
{
	struct sk_buff *skb;
	if(list)
		sklist_remove_socket(list, sk);

	while((skb=skb_dequeue(&sk->receive_queue))!=NULL)
	{
		kfree_skb(skb,FREE_READ);
	}

	if(sk->wmem_alloc == 0 && sk->rmem_alloc == 0 && sk->dead)
	{
		sk_free(sk);
	}
	else
	{
		/*
		 *	Someone is using our buffers still.. defer
		 */
		init_timer(&sk->timer);
		sk->timer.expires=jiffies+10*HZ;
		sk->timer.function=sklist_destroy_timer;
		sk->timer.data = (unsigned long)sk;
		add_timer(&sk->timer);
	}
}


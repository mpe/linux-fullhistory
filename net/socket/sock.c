/*
 *	NET2Debugged, generic socket properties.
 *
 *	This module provides the generic option control and memory handling
 *	for a socket of any kind.
 *
 * Version:	@(#)sock.c	1.28	26/12/93
 *
 * Authors:	Alan Cox <iiitac@pyr.swan.ac.uk>
 *
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

#include "inet.h"
#include "dev.h"
#include "ip.h"
#include "protocol.h"
#include "arp.h"
#include "route.h"
#include "tcp.h"
#include "udp.h"
#include "skbuff.h"
#include "sock.h"
#include "raw.h"
#include "icmp.h"

static __inline__ int 
min(unsigned int a, unsigned int b)
{
 	if (a < b) 
 		return(a);
	return(b);
}

#ifdef SOCK_DEBUG

void print_sk(struct sock *sk)
{
  	if (!sk) 
  	{
		printk("  print_sk(NULL)\n");
		return;
  	}
  	printk("  wmem_alloc = %lu\n", sk->wmem_alloc);
  	printk("  rmem_alloc = %lu\n", sk->rmem_alloc);
  	printk("  send_head = %p\n", sk->send_head);
  	printk("  state = %d\n",sk->state);
  	printk("  wback = %p, rqueue = %p\n", sk->wback, sk->rqueue);
  	printk("  wfront = %p\n", sk->wfront);
  	printk("  daddr = %lX, saddr = %lX\n", sk->daddr,sk->saddr);
  	printk("  num = %d", sk->num);
  	printk(" next = %p\n", sk->next);
  	printk("  send_seq = %ld, acked_seq = %ld, copied_seq = %ld\n",
		  sk->send_seq, sk->acked_seq, sk->copied_seq);
	printk("  rcv_ack_seq = %ld, window_seq = %ld, fin_seq = %ld\n",
		  sk->rcv_ack_seq, sk->window_seq, sk->fin_seq);
  	printk("  prot = %p\n", sk->prot);
  	printk("  pair = %p, back_log = %p\n", sk->pair,sk->back_log);
  	printk("  inuse = %d , blog = %d\n", sk->inuse, sk->blog);
  	printk("  dead = %d delay_acks=%d\n", sk->dead, sk->delay_acks);
  	printk("  retransmits = %ld, timeout = %d\n", sk->retransmits, sk->timeout);
  	printk("  cong_window = %d, packets_out = %d\n", sk->cong_window,
		  sk->packets_out);
	printk("  urg = %d shutdown=%d\n", sk->urg, sk->shutdown);
}


void print_skb(struct sk_buff *skb)
{
  	if (!skb) 
  	{
		printk("  print_skb(NULL)\n");
		return;
  	}
  	printk("  prev = %p, next = %p\n", skb->prev, skb->next);
  	printk("  sk = %p link3 = %p\n", skb->sk, skb->link3);
  	printk("  mem_addr = %p, mem_len = %lu\n", skb->mem_addr, skb->mem_len);
  	printk("  used = %d free = %d\n", skb->used,skb->free);
}


#endif

/*
 *	This is meant for all protocols to use and covers goings on
 *	at the socket level. Everything here is generic.
 */

int sock_setsockopt(struct sock *sk, int level, int optname,
		char *optval, int optlen)
{
	int val;
	int err;
	struct linger ling;

  	if (optval == NULL) 
  		return(-EINVAL);

  	err=verify_area(VERIFY_READ, optval, sizeof(int));
  	if(err)
  		return err;
  	
  	val = get_fs_long((unsigned long *)optval);
  	switch(optname) 
  	{
		case SO_TYPE:
		case SO_ERROR:
		  	return(-ENOPROTOOPT);

		case SO_DEBUG:	
			sk->debug=val?1:0;
		case SO_DONTROUTE:	/* Still to be implemented */
			return(0);
		case SO_BROADCAST:
			sk->broadcast=val?1:0;
			return 0;
		case SO_SNDBUF:
			if(val>32767)
				val=32767;
			if(val<256)
				val=256;
			sk->sndbuf=val;
			return 0;
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
		case SO_RCVBUF:
			if(val>32767)
				val=32767;
			if(val<256)
				val=256;
			sk->rcvbuf=val;
			return(0);

		case SO_REUSEADDR:
			if (val) 
				sk->reuse = 1;
			else 
				sk->reuse = 0;
			return(0);

		case SO_KEEPALIVE:
			if (val)
				sk->keepopen = 1;
			else 
				sk->keepopen = 0;
			return(0);

	 	case SO_OOBINLINE:
			if (val) 
				sk->urginline = 1;
			else 
				sk->urginline = 0;
			return(0);

	 	case SO_NO_CHECK:
			if (val) 
				sk->no_check = 1;
			else 
				sk->no_check = 0;
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
		
		case SO_DONTROUTE:	/* One last option to implement */
			val = 0;
			break;
		
		case SO_BROADCAST:
			val= sk->broadcast;
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
			if (sk->prot == &tcp_prot) 
				val = SOCK_STREAM;
		  	else 
		  		val = SOCK_DGRAM;
			break;

		case SO_ERROR:
			val = sk->err;
			sk->err = 0;
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


void *sock_wmalloc(struct sock *sk, unsigned long size, int force,
	     int priority)
{
  	if (sk) 
  	{
		if (sk->wmem_alloc + size < sk->sndbuf || force) 
		{
			cli();
			sk->wmem_alloc+= size;
			sti();
			return(alloc_skb(size, priority));
		}
		DPRINTF((DBG_INET, "sock_wmalloc(%X,%d,%d,%d) returning NULL\n",
						sk, size, force, priority));
		return(NULL);
  	}
  	return(alloc_skb(size, priority));
}


void *sock_rmalloc(struct sock *sk, unsigned long size, int force, int priority)
{
  	if (sk) 
  	{
		if (sk->rmem_alloc + size < sk->rcvbuf || force) 
		{
			void *c = alloc_skb(size, priority);
			cli();
			if (c) 
				sk->rmem_alloc += size;
			sti();
			return(c);
		}
		DPRINTF((DBG_INET, "sock_rmalloc(%X,%d,%d,%d) returning NULL\n",
						sk,size,force, priority));
		return(NULL);
	  }
	  return(alloc_skb(size, priority));
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
		return(sk->sndbuf-sk->wmem_alloc );
  	}
  	return(0);
}


void sock_wfree(struct sock *sk, void *mem, unsigned long size)
{
	struct sk_buff *skb;
  	DPRINTF((DBG_INET, "sock_wfree(sk=%X, mem=%X, size=%d)\n", sk, mem, size));

  	IS_SKB(mem);
	
	skb=mem;
	
  	kfree_skbmem(mem, size);
  	if (sk) 
  	{
		sk->wmem_alloc -= size;

		/* In case it might be waiting for more memory. */
		if (!sk->dead) 
			sk->write_space(sk);
		if (sk->destroy && sk->wmem_alloc == 0 && sk->rmem_alloc == 0) 
		{
			DPRINTF((DBG_INET,
				"recovered lost memory, sock = %X\n", sk));
		}
		return;
  	}
}


void sock_rfree(struct sock *sk, void *mem, unsigned long size)
{
	struct sk_buff *skb;
	
  	DPRINTF((DBG_INET, "sock_rfree(sk=%X, mem=%X, size=%d)\n", sk, mem, size));

  	IS_SKB(mem);
  	skb=mem;
  	
  	kfree_skbmem(mem, size);
  	
  	if (sk) 
  	{
		sk->rmem_alloc -= size;
		if (sk->destroy && sk->wmem_alloc == 0 && sk->rmem_alloc == 0) 
		{
			DPRINTF((DBG_INET,"recovered lot memory, sock = %X\n", sk));
		}
  	}
}


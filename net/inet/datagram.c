/*
 *	SUCS	NET2 Debugged.
 *
 *	Generic datagram handling routines. These are generic for all protocols. Possibly a generic IP version on top
 *	of these would make sense. Not tonight however 8-). 
 *	This is used because UDP, RAW, PACKET and the to be released IPX layer all have identical select code and mostly
 *	identical recvfrom() code. So we share it here. The select was shared before but buried in udp.c so I moved it.
 *
 *	Authors:	Alan Cox <iiitac@pyr.swan.ac.uk>. (datagram_select() from old udp.c code)
 *
 *	Fixes:
 *		Alan Cox	:	NULL return from skb_peek_copy() understood
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/sched.h>
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
 

/*
 *	Get a datagram skbuff, understands the peeking, nonblocking wakeups and possible
 *	races. This replaces identical code in packet,raw and udp, as well as the yet to
 *	be released IPX support. It also finally fixes the long standing peek and read
 *	race for datagram sockets. If you alter this routine remember it must be
 *	re-entrant.
 */

struct sk_buff *skb_recv_datagram(struct sock *sk, unsigned flags, int noblock, int *err)
{  
	struct sk_buff *skb;
	
	/* Socket is inuse - so the timer doesn't attack it */
  	sk->inuse = 1;
  	while(sk->rqueue == NULL) 	/* No data */
  	{
  		/* If we are shutdown then no more data is going to appear. We are done */
		if (sk->shutdown & RCV_SHUTDOWN) 
		{
			release_sock(sk);
			*err=0;
			return NULL;
		}

		/* User doesn't want to wait */
		if (noblock) 
		{
			release_sock(sk);
			*err=-EAGAIN;
			return NULL;
		}
		release_sock(sk);
		
		/* Interrupts off so that no packet arrives before we begin sleeping. 
		   Otherwise we might miss our wake up */
		cli();
		if (sk->rqueue == NULL) 
		{
			interruptible_sleep_on(sk->sleep);
			/* Signals may need a restart of the syscall */
			if (current->signal & ~current->blocked) 
			{
				sti();
				*err=-ERESTARTSYS;
				return(NULL);
			}
			if(sk->err != 0)	/* Error while waiting for packet
						   eg an icmp sent earlier by the
						   peer has finaly turned up now */
			{
				*err = -sk->err;
				sti();
				sk->err=0;
				return NULL;
			}
		}
		sk->inuse = 1;
		sti();
	  }
	  /* Again only user level code calls this function, so nothing interrupt level
	     will suddenely eat the rqueue */
	  if (!(flags & MSG_PEEK)) 
	  	skb=skb_dequeue(&sk->rqueue);
	  else
	  {
	  	skb=skb_peek_copy(&sk->rqueue);	/* We make a copy with interrupts off. Its the only
	  					   way to be safe as this code is re-entrant */
	  	if(skb==NULL)	/* shouldn't happen but .. */
	  		*err=-ENOMEM;
	  }
	  return skb;
}	


/*
 *	Datagram select: Again totally generic. Moved from udp.c
 */
 
int datagram_select(struct sock *sk, int sel_type, select_table *wait)
{
	select_wait(sk->sleep, wait);
	switch(sel_type) 
	{
		case SEL_IN:
			if (sk->rqueue != NULL || sk->err != 0) 
			{	/* This appears to be consistent
				   with other stacks */
				return(1);
			}
			return(0);

		case SEL_OUT:
			if (sk->prot->wspace(sk) >= MIN_WRITE_SPACE) 
			{
				return(1);
			}
			return(0);
	
		case SEL_EX:
			if (sk->err) 
				return(1); /* Socket has gone into error state (eg icmp error) */
			return(0);
  	}
 	return(0);
}

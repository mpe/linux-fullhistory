/*
 *	SUCS NET3:
 *
 *	Generic datagram handling routines. These are generic for all protocols. Possibly a generic IP version on top
 *	of these would make sense. Not tonight however 8-).
 *	This is used because UDP, RAW, PACKET, DDP, IPX, AX.25 and NetROM layer all have identical select code and mostly
 *	identical recvmsg() code. So we share it here. The select was shared before but buried in udp.c so I moved it.
 *
 *	Authors:	Alan Cox <alan@cymru.net>. (datagram_select() from old udp.c code)
 *
 *	Fixes:
 *		Alan Cox	:	NULL return from skb_peek_copy() understood
 *		Alan Cox	:	Rewrote skb_read_datagram to avoid the skb_peek_copy stuff.
 *		Alan Cox	:	Added support for SOCK_SEQPACKET. IPX can no longer use the SO_TYPE hack but
 *					AX.25 now works right, and SPX is feasible.
 *		Alan Cox	:	Fixed write select of non IP protocol crash.
 *		Florian  La Roche:	Changed for my new skbuff handling.
 *		Darryl Miles	:	Fixed non-blocking SOCK_SEQPACKET.
 *		Linus Torvalds	:	BSD semantic fixes.
 *		Alan Cox	:	Datagram iovec handling
 *		Darryl Miles	:	Fixed non-blocking SOCK_STREAM.
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <net/sock.h>


/*
 * Wait for a packet..
 *
 * Interrupts off so that no packet arrives before we begin sleeping.
 * Otherwise we might miss our wake up
 */

static inline void wait_for_packet(struct sock * sk)
{
	unsigned long flags;

	release_sock(sk);
	save_flags(flags);
	cli();
	if (skb_peek(&sk->receive_queue) == NULL)
		interruptible_sleep_on(sk->sleep);
	restore_flags(flags);
	lock_sock(sk);
}

/*
 *	Is a socket 'connection oriented' ?
 */
 
static inline int connection_based(struct sock *sk)
{
	if(sk->type==SOCK_SEQPACKET || sk->type==SOCK_STREAM)
		return 1;
	return 0;
}

/*
 *	Get a datagram skbuff, understands the peeking, nonblocking wakeups and possible
 *	races. This replaces identical code in packet,raw and udp, as well as the IPX
 *	AX.25 and Appletalk. It also finally fixes the long standing peek and read
 *	race for datagram sockets. If you alter this routine remember it must be
 *	re-entrant.
 *
 *	This function will lock the socket if a skb is returned, so the caller
 *	needs to unlock the socket in that case (usually by calling skb_free_datagram)
 */

struct sk_buff *skb_recv_datagram(struct sock *sk, unsigned flags, int noblock, int *err)
{
	int error;
	struct sk_buff *skb;

	lock_sock(sk);
restart:
	while(skb_queue_empty(&sk->receive_queue))	/* No data */
	{
		/* Socket errors? */
		error = sock_error(sk);
		if (error)
			goto no_packet;

		/* Socket shut down? */
		if (sk->shutdown & RCV_SHUTDOWN)
			goto no_packet;

		/* Sequenced packets can come disconnected. If so we report the problem */
		error = -ENOTCONN;
		if(connection_based(sk) && sk->state!=TCP_ESTABLISHED)
			goto no_packet;

		/* User doesn't want to wait */
		error = -EAGAIN;
		if (noblock)
			goto no_packet;

		/* handle signals */
		error = -ERESTARTSYS;
		if (current->signal & ~current->blocked)
			goto no_packet;

		wait_for_packet(sk);
	  }

	/* Again only user level code calls this function, so nothing interrupt level
	   will suddenly eat the receive_queue */
	if (flags & MSG_PEEK)
	{
		unsigned long flags;
		save_flags(flags);
		cli();
		skb=skb_peek(&sk->receive_queue);
		if(skb!=NULL)
			skb->users++;
		restore_flags(flags);
		if(skb==NULL)		/* shouldn't happen but .. */
			goto restart;
		return skb;
	}
	skb = skb_dequeue(&sk->receive_queue);
	if (!skb)	/* Avoid race if someone beats us to the data */
		goto restart;
	skb->users++;
	return skb;

no_packet:
	release_sock(sk);
	*err = error;
	return NULL;
}

void skb_free_datagram(struct sock * sk, struct sk_buff *skb)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	skb->users--;
	if(skb->users <= 0) {
		/* See if it needs destroying */
		/* Been dequeued by someone - ie it's read */
		if(!skb->next && !skb->prev)
			kfree_skb(skb,FREE_READ);
	}
	restore_flags(flags);
	release_sock(sk);
}

/*
 *	Copy a datagram to a linear buffer.
 */

void skb_copy_datagram(struct sk_buff *skb, int offset, char *to, int size)
{
	memcpy_tofs(to,skb->h.raw+offset,size);
}


/*
 *	Copy a datagram to an iovec.
 */
 
void skb_copy_datagram_iovec(struct sk_buff *skb, int offset, struct iovec *to, int size)
{
	memcpy_toiovec(to,skb->h.raw+offset,size);
}

/*
 *	Datagram select: Again totally generic. Moved from udp.c
 *	Now does seqpacket.
 */

int datagram_select(struct sock *sk, int sel_type, select_table *wait)
{
	select_wait(sk->sleep, wait);
	switch(sel_type)
	{
		case SEL_IN:
			if (sk->err)
				return 1;
			if (sk->shutdown & RCV_SHUTDOWN)
				return 1;
			if (connection_based(sk) && sk->state==TCP_CLOSE)
			{
				/* Connection closed: Wake up */
				return(1);
			}
			if (skb_peek(&sk->receive_queue) != NULL)
			{	/* This appears to be consistent
				   with other stacks */
				return(1);
			}
			return(0);

		case SEL_OUT:
			if (sk->err)
				return 1;
			if (sk->shutdown & SEND_SHUTDOWN)
				return 1;
			if (connection_based(sk) && sk->state==TCP_SYN_SENT)
			{
				/* Connection still in progress */
				break;
			}
			if (sk->prot && sock_wspace(sk) >= MIN_WRITE_SPACE)
			{
				return(1);
			}
			if (sk->prot==NULL && sk->sndbuf-sk->wmem_alloc >= MIN_WRITE_SPACE)
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

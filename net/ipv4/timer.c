/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		TIMER - implementation of software timers for IP.
 *
 * Version:	@(#)timer.c	1.0.7	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Fred Baumgarten, <dc6iq@insu1.etec.uni-karlsruhe.de>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *
 * Fixes:
 *		Alan Cox	:	To avoid destroying a wait queue as we use it
 *					we defer destruction until the destroy timer goes
 *					off.
 *		Alan Cox	:	Destroy socket doesn't write a status value to the
 *					socket buffer _AFTER_ freeing it! Also sock ensures
 *					the socket will get removed BEFORE this is called
 *					otherwise if the timer TIME_DESTROY occurs inside
 *					of inet_bh() with this socket being handled it goes
 *					BOOM! Have to stop timer going off if net_bh is
 *					active or the destroy causes crashes.
 *		Alan Cox	:	Cleaned up unused code.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <asm/system.h>
#include <linux/interrupt.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/arp.h>

void delete_timer (struct sock *t)
{
	unsigned long flags;

	save_flags (flags);
	cli();

	t->timeout = 0;
	del_timer (&t->timer);

	restore_flags (flags);
}

void reset_timer (struct sock *t, int timeout, unsigned long len)
{
	delete_timer (t);
	t->timeout = timeout;
#if 1
  /* FIXME: ??? */
	if ((int) len < 0)	/* prevent close to infinite timers. THEY _DO_ */
		len = 3;	/* happen (negative values ?) - don't ask me why ! -FB */
#endif
	t->timer.expires = jiffies+len;
	add_timer (&t->timer);
}


/*
 *	Now we will only be called whenever we need to do
 *	something, but we must be sure to process all of the
 *	sockets that need it.
 */

void net_timer (unsigned long data)
{
	struct sock *sk = (struct sock*)data;
	int why = sk->timeout;

	/* 
	 * only process if socket is not in use
	 */

	if (sk->users)
	{
		sk->timer.expires = jiffies+HZ;
		add_timer(&sk->timer);
		sti();
		return;
	}

	/* Always see if we need to send an ack. */

	if (sk->ack_backlog && !sk->zapped) 
	{
		sk->prot->read_wakeup (sk);
		if (! sk->dead)
		sk->data_ready(sk,0);
	}

	/* Now we need to figure out why the socket was on the timer. */

	switch (why) 
	{
		case TIME_DONE:
			/* If the socket hasn't been closed off, re-try a bit later */
			if (!sk->dead) {
				reset_timer(sk, TIME_DONE, TCP_DONE_TIME);
				break;
			}

			if (sk->state != TCP_CLOSE) 
			{
				printk ("non CLOSE socket in time_done\n");
				break;
			}
			destroy_sock (sk);
			break;

		case TIME_DESTROY:
		/*
		 *	We've waited for a while for all the memory associated with
		 *	the socket to be freed.
		 */

			destroy_sock(sk);
			break;

		case TIME_CLOSE:
			/* We've waited long enough, close the socket. */
			sk->state = TCP_CLOSE;
			delete_timer (sk);
			if (!sk->dead)
				sk->state_change(sk);
			sk->shutdown = SHUTDOWN_MASK;
			reset_timer (sk, TIME_DONE, TCP_DONE_TIME);
			break;

		default:
			printk ("net_timer: timer expired - reason %d is unknown\n", why);
			break;
	}
}


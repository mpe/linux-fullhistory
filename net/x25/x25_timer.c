/*
 *	X.25 Packet Layer release 001
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 2.1.15 or higher
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	X.25 001	Jonathan Naylor	Started coding.
 */

#include <linux/config.h>
#if defined(CONFIG_X25) || defined(CONFIG_X25_MODULE)
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/x25.h>

static void x25_timer(unsigned long);

/*
 *	Linux set/reset timer routines
 */
void x25_set_timer(struct sock *sk)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();

	del_timer(&sk->timer);

	restore_flags(flags);

	sk->timer.next     = sk->timer.prev = NULL;	
	sk->timer.data     = (unsigned long)sk;
	sk->timer.function = &x25_timer;
	sk->timer.expires  = jiffies + 100;

	add_timer(&sk->timer);
}

static void x25_reset_timer(struct sock *sk)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();

	del_timer(&sk->timer);

	restore_flags(flags);

	sk->timer.data     = (unsigned long)sk;
	sk->timer.function = &x25_timer;
	sk->timer.expires  = jiffies + 100;

	add_timer(&sk->timer);
}

/*
 *	X.25 TIMER 
 *
 *	This routine is called every second. Decrement timer by this
 *	amount - if expired then process the event.
 */
static void x25_timer(unsigned long param)
{
	struct sock *sk = (struct sock *)param;

	switch (sk->protinfo.x25->state) {
		case X25_STATE_0:
			/* Magic here: If we listen() and a new link dies before it
			   is accepted() it isn't 'dead' so doesn't get removed. */
			if (sk->destroy || (sk->state == TCP_LISTEN && sk->dead)) {
				del_timer(&sk->timer);
				x25_destroy_socket(sk);
				return;
			}
			break;

		case X25_STATE_3:
			/*
			 * Check for the state of the receive buffer.
			 */
			if (sk->rmem_alloc < (sk->rcvbuf / 2) && (sk->protinfo.x25->condition & X25_COND_OWN_RX_BUSY)) {
				sk->protinfo.x25->condition &= ~X25_COND_OWN_RX_BUSY;
				sk->protinfo.x25->condition &= ~X25_COND_ACK_PENDING;
				sk->protinfo.x25->vl         = sk->protinfo.x25->vr;
				sk->protinfo.x25->timer      = 0;
				x25_write_internal(sk, X25_RR);
				break;
			}
			/*
			 * Check for frames to transmit.
			 */
			x25_kick(sk);
			break;

		default:
			break;
	}

	if (sk->protinfo.x25->timer == 0 || --sk->protinfo.x25->timer > 0) {
		x25_reset_timer(sk);
		return;
	}

	/*
	 * Timer has expired, it may have been T2, T21, T22, or T23. We can tell
	 * by the state machine state.
	 */
	switch (sk->protinfo.x25->state) {
		case X25_STATE_3:	/* T2 */
			if (sk->protinfo.x25->condition & X25_COND_ACK_PENDING) {
				sk->protinfo.x25->condition &= ~X25_COND_ACK_PENDING;
				x25_enquiry_response(sk);
			}
			break;

		case X25_STATE_1:	/* T21 */
		case X25_STATE_4:	/* T22 */
			x25_write_internal(sk, X25_CLEAR_REQUEST);
			sk->protinfo.x25->state = X25_STATE_2;
			sk->protinfo.x25->timer = sk->protinfo.x25->t23;
			break;

		case X25_STATE_2:	/* T23 */
			x25_clear_queues(sk);
			sk->protinfo.x25->state = X25_STATE_0;
			sk->state               = TCP_CLOSE;
			sk->err                 = ETIMEDOUT;
			sk->shutdown           |= SEND_SHUTDOWN;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead                = 1;
			break;
	}

	x25_set_timer(sk);
}

#endif

/*
 *	NET/ROM release 006
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	NET/ROM 001	Jonathan(G4KLX)	Cloned from ax25_timer.c
 */

#include <linux/config.h>
#if defined(CONFIG_NETROM) || defined(CONFIG_NETROM_MODULE)
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
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/netrom.h>

static void nr_timer(unsigned long);

/*
 *	Linux set timer
 */
void nr_set_timer(struct sock *sk)
{
	unsigned long flags;

	save_flags(flags); cli();
	del_timer(&sk->timer);
	restore_flags(flags);

	sk->timer.data     = (unsigned long)sk;
	sk->timer.function = &nr_timer;
	sk->timer.expires  = jiffies + (HZ / 10);

	add_timer(&sk->timer);
}

/*
 *	NET/ROM TIMER 
 *
 *	This routine is called every 100ms. Decrement timer by this
 *	amount - if expired then process the event.
 */
static void nr_timer(unsigned long param)
{
	struct sock *sk = (struct sock *)param;

	switch (sk->protinfo.nr->state) {
		case NR_STATE_0:
			/* Magic here: If we listen() and a new link dies before it
			   is accepted() it isn't 'dead' so doesn't get removed. */
			if (sk->destroy || (sk->state == TCP_LISTEN && sk->dead)) {
				del_timer(&sk->timer);
				nr_destroy_socket(sk);
				return;
			}
			break;

		case NR_STATE_3:
			/*
			 * Check for the state of the receive buffer.
			 */
			if (atomic_read(&sk->rmem_alloc) < (sk->rcvbuf / 2) &&
			    (sk->protinfo.nr->condition & NR_COND_OWN_RX_BUSY)) {
				sk->protinfo.nr->condition &= ~NR_COND_OWN_RX_BUSY;
				sk->protinfo.nr->condition &= ~NR_COND_ACK_PENDING;
				sk->protinfo.nr->vl         = sk->protinfo.nr->vr;
				nr_write_internal(sk, NR_INFOACK);
				break;
			}
			/*
			 * Check for frames to transmit.
			 */
			nr_kick(sk);
			break;

		default:
			break;
	}

	if (sk->protinfo.nr->t2timer > 0 && --sk->protinfo.nr->t2timer == 0) {
		if (sk->protinfo.nr->state == NR_STATE_3) {
			if (sk->protinfo.nr->condition & NR_COND_ACK_PENDING) {
				sk->protinfo.nr->condition &= ~NR_COND_ACK_PENDING;
				nr_enquiry_response(sk);
			}
		}
	}

	if (sk->protinfo.nr->t4timer > 0 && --sk->protinfo.nr->t4timer == 0) {
		sk->protinfo.nr->condition &= ~NR_COND_PEER_RX_BUSY;
	}

	if (sk->protinfo.nr->t1timer == 0 || --sk->protinfo.nr->t1timer > 0) {
		nr_set_timer(sk);
		return;
	}

	switch (sk->protinfo.nr->state) {
		case NR_STATE_1: 
			if (sk->protinfo.nr->n2count == sk->protinfo.nr->n2) {
				nr_clear_queues(sk);
				sk->protinfo.nr->state = NR_STATE_0;
				sk->state              = TCP_CLOSE;
				sk->err                = ETIMEDOUT;
				sk->shutdown          |= SEND_SHUTDOWN;
				if (!sk->dead)
					sk->state_change(sk);
				sk->dead               = 1;
			} else {
				sk->protinfo.nr->n2count++;
				nr_write_internal(sk, NR_CONNREQ);
			}
			break;

		case NR_STATE_2:
			if (sk->protinfo.nr->n2count == sk->protinfo.nr->n2) {
				nr_clear_queues(sk);
				sk->protinfo.nr->state = NR_STATE_0;
				sk->state              = TCP_CLOSE;
				sk->err                = ETIMEDOUT;
				sk->shutdown          |= SEND_SHUTDOWN;
				if (!sk->dead)
					sk->state_change(sk);
				sk->dead               = 1;
			} else {
				sk->protinfo.nr->n2count++;
				nr_write_internal(sk, NR_DISCREQ);
			}
			break;

		case NR_STATE_3:
			if (sk->protinfo.nr->n2count == sk->protinfo.nr->n2) {
				nr_clear_queues(sk);
				sk->protinfo.nr->state = NR_STATE_0;
				sk->state              = TCP_CLOSE;
				sk->err                = ETIMEDOUT;
				sk->shutdown          |= SEND_SHUTDOWN;
				if (!sk->dead)
					sk->state_change(sk);
				sk->dead               = 1;
			} else {
				sk->protinfo.nr->n2count++;
				nr_requeue_frames(sk);
			}
			break;
	}

	sk->protinfo.nr->t1timer = sk->protinfo.nr->t1;

	nr_set_timer(sk);
}

#endif

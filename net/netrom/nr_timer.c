/*
 *	NET/ROM release 003
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 1.2.1 or higher/ NET3.029
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
#ifdef CONFIG_NETROM
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
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/netrom.h>

static void nr_timer(unsigned long);

/*
 *	Linux set/reset timer routines
 */
void nr_set_timer(struct sock *sk)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();
	del_timer(&sk->timer);
	restore_flags(flags);

	sk->timer.next     = sk->timer.prev = NULL;	
	sk->timer.data     = (unsigned long)sk;
	sk->timer.function = &nr_timer;

	sk->timer.expires = jiffies+10;
	add_timer(&sk->timer);
}

static void nr_reset_timer(struct sock *sk)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();
	del_timer(&sk->timer);
	restore_flags(flags);

	sk->timer.data     = (unsigned long)sk;
	sk->timer.function = &nr_timer;
	sk->timer.expires  = jiffies+10;
	add_timer(&sk->timer);
}

/*
 *	NET/ROM TIMER 
 *
 *	This routine is called every 500ms. Decrement timer by this
 *	amount - if expired then process the event.
 */
static void nr_timer(unsigned long param)
{
	struct sock *sk = (struct sock *)param;

	switch (sk->nr->state) {
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
			if (sk->rmem_alloc < (sk->rcvbuf / 2) && (sk->nr->condition & OWN_RX_BUSY_CONDITION)) {
				sk->nr->condition &= ~OWN_RX_BUSY_CONDITION;
				nr_write_internal(sk, NR_INFOACK);
				sk->nr->condition &= ~ACK_PENDING_CONDITION;
				sk->nr->vl = sk->nr->vr;
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

	if (sk->nr->t2timer > 0 && --sk->nr->t2timer == 0) {
		if (sk->nr->state == NR_STATE_3) {
			if (sk->nr->condition & ACK_PENDING_CONDITION) {
				sk->nr->condition &= ~ACK_PENDING_CONDITION;
				nr_enquiry_response(sk);
			}
		}
	}

	if (sk->nr->t4timer > 0 && --sk->nr->t4timer == 0) {
		sk->nr->condition &= ~PEER_RX_BUSY_CONDITION;
	}

	if (sk->nr->t1timer == 0 || --sk->nr->t1timer > 0) {
		nr_reset_timer(sk);
		return;
	}

	switch (sk->nr->state) {
		case NR_STATE_1: 
			if (sk->nr->n2count == sk->nr->n2) {
				nr_clear_queues(sk);
				sk->nr->state = NR_STATE_0;
				sk->state     = TCP_CLOSE;
				sk->err       = ETIMEDOUT;
				if (!sk->dead)
					sk->state_change(sk);
				sk->dead      = 1;
			} else {
				sk->nr->n2count++;
				nr_write_internal(sk, NR_CONNREQ);
			}
			break;

		case NR_STATE_2:
			if (sk->nr->n2count == sk->nr->n2) {
				nr_clear_queues(sk);
				sk->nr->state = NR_STATE_0;
				sk->state     = TCP_CLOSE;
				sk->err       = ETIMEDOUT;
				if (!sk->dead)
					sk->state_change(sk);
				sk->dead      = 1;
			} else {
				sk->nr->n2count++;
				nr_write_internal(sk, NR_DISCREQ);
			}
			break;

		case NR_STATE_3:
			if (sk->nr->n2count == sk->nr->n2) {
				nr_clear_queues(sk);
				sk->nr->state = NR_STATE_0;
				sk->state     = TCP_CLOSE;
				sk->err       = ETIMEDOUT;
				if (!sk->dead)
					sk->state_change(sk);
				sk->dead      = 1;
			} else {
				sk->nr->n2count++;
				nr_requeue_frames(sk);
			}
			break;
	}

	sk->nr->t1timer = sk->nr->t1 = nr_calculate_t1(sk);

	nr_set_timer(sk);
}

#endif

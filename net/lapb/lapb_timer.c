/*
 *	LAPB release 001
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
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
 *	LAPB 001	Jonathan Naylor	Started Coding
 */

#include <linux/config.h>
#if defined(CONFIG_LAPB) || defined(CONFIG_LAPB_MODULE)
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
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/lapb.h>

static void lapb_timer(unsigned long);

/*
 *	Linux set timer
 */
void lapb_set_timer(lapb_cb *lapb)
{
	unsigned long flags;	

	save_flags(flags); cli();
	del_timer(&lapb->timer);
	restore_flags(flags);

	lapb->timer.data     = (unsigned long)lapb;
	lapb->timer.function = &lapb_timer;
	lapb->timer.expires  = jiffies + 10;

	add_timer(&lapb->timer);
}

/*
 *	LAPB TIMER 
 *
 *	This routine is called every 100ms. Decrement timer by this
 *	amount - if expired then process the event.
 */
static void lapb_timer(unsigned long param)
{
	lapb_cb *lapb = (lapb_cb *)param;
	struct sk_buff *skb;

	/*
	 *	Process all packet received since the last clock tick.
	 */
	while ((skb = skb_dequeue(&lapb->input_queue)) != NULL)
		lapb_data_input(lapb, skb);

	/*
	 *	If in a data transfer state, transmit any data.
	 */
	if (lapb->state == LAPB_STATE_3)
		lapb_kick(lapb);

	/*
	 *	T2 expiry code.
	 */
	if (lapb->t2timer > 0 && --lapb->t2timer == 0) {
		if (lapb->state == LAPB_STATE_3) {
			if (lapb->condition & LAPB_ACK_PENDING_CONDITION) {
				lapb->condition &= ~LAPB_ACK_PENDING_CONDITION;
				lapb_timeout_response(lapb);
			}
		}
	}

	/*
	 *	If T1 isn't running, or hasn't timed out yet, keep going.
	 */
	if (lapb->t1timer == 0 || --lapb->t1timer > 0) {
		lapb_set_timer(lapb);
		return;
	}

	/*
	 *	T1 has expired.
	 */
	switch (lapb->state) {

		/*
		 *	If we are a DCE, keep going DM .. DM .. DM
		 */
		case LAPB_STATE_0:
			if (lapb->mode & LAPB_DCE)
				lapb_send_control(lapb, LAPB_DM, LAPB_POLLOFF, LAPB_RESPONSE);
			break;

		/*
		 *	Awaiting connection state, send SABM(E), up to N2 times.
		 */
		case LAPB_STATE_1: 
			if (lapb->n2count == lapb->n2) {
				lapb_clear_queues(lapb);
				lapb->state   = LAPB_STATE_0;
				lapb->t2timer = 0;
				lapb_disconnect_indication(lapb, LAPB_TIMEDOUT);
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S1 -> S0\n", lapb->token);
#endif
			} else {
				lapb->n2count++;
				if (lapb->mode & LAPB_EXTENDED) {
#if LAPB_DEBUG > 1
					printk(KERN_DEBUG "lapb: (%p) S1 TX SABME(1)\n", lapb->token);
#endif
					lapb_send_control(lapb, LAPB_SABME, LAPB_POLLON, LAPB_COMMAND);
				} else {
#if LAPB_DEBUG > 1
					printk(KERN_DEBUG "lapb: (%p) S1 TX SABM(1)\n", lapb->token);
#endif
					lapb_send_control(lapb, LAPB_SABM, LAPB_POLLON, LAPB_COMMAND);
				}
			}
			break;

		/*
		 *	Awaiting disconnection state, send DISC, up to N2 times.
		 */
		case LAPB_STATE_2:
			if (lapb->n2count == lapb->n2) {
				lapb_clear_queues(lapb);
				lapb->state   = LAPB_STATE_0;
				lapb->t2timer = 0;
				lapb_disconnect_confirmation(lapb, LAPB_TIMEDOUT);
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S2 -> S0\n", lapb->token);
#endif
			} else {
				lapb->n2count++;
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S2 TX DISC(1)\n", lapb->token);
#endif
				lapb_send_control(lapb, LAPB_DISC, LAPB_POLLON, LAPB_COMMAND);
			}
			break;

		/*
		 *	Data transfer state, restransmit I frames, up to N2 times.
		 */
		case LAPB_STATE_3:
			if (lapb->n2count == lapb->n2) {
				lapb_clear_queues(lapb);
				lapb->state   = LAPB_STATE_0;
				lapb->t2timer = 0;
				lapb_disconnect_indication(lapb, LAPB_TIMEDOUT);
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S3 -> S0\n", lapb->token);
#endif
			} else {
				lapb->n2count++;
				lapb_requeue_frames(lapb);
			}
			break;

		/*
		 *	Frame reject state, restransmit FRMR frames, up to N2 times.
		 */
		case LAPB_STATE_4:
			if (lapb->n2count == lapb->n2) {
				lapb_clear_queues(lapb);
				lapb->state   = LAPB_STATE_0;
				lapb->t2timer = 0;
				lapb_disconnect_indication(lapb, LAPB_TIMEDOUT);
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S4 -> S0\n", lapb->token);
#endif
			} else {
				lapb->n2count++;
				lapb_transmit_frmr(lapb);
			}
			break;
	}

	lapb->t1timer = lapb->t1;

	lapb_set_timer(lapb);
}

#endif

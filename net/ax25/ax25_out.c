/*
 *	AX.25 release 029
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
 *	Most of this code is based on the SDL diagrams published in the 7th
 *	ARRL Computer Networking Conference papers. The diagrams have mistakes
 *	in them, but are mostly correct. Before you modify the code could you
 *	read the SDL diagrams as the code is not obvious and probably very
 *	easy to break;
 *
 *	History
 *	AX.25 028a	Jonathan(G4KLX)	New state machine based on SDL diagrams.
 *	AX.25 029	Alan(GW4PTS)	Switched to KA9Q constant names.
 *			Jonathan(G4KLX)	Only poll when window is full.
 */

#include <linux/config.h>
#ifdef CONFIG_AX25
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

int ax25_output(ax25_cb *ax25, struct sk_buff *skb)
{
	skb_queue_tail(&ax25->write_queue, skb);	/* Throw it on the queue */

	if (ax25->state == AX25_STATE_3 || ax25->state == AX25_STATE_4)
		ax25_kick(ax25);

	return 0;
}

/* 
 *  This procedure is passed a buffer descriptor for an iframe. It builds
 *  the rest of the control part of the frame and then writes it out.
 */
static void ax25_send_iframe(ax25_cb *ax25, struct sk_buff *skb, int poll_bit)
{
	unsigned char *frame;

	if (skb == NULL)
		return;
	
	frame = skb->h.raw;	/* KISS + header */

	*frame = I;
	*frame |= poll_bit;
	*frame |= (ax25->vr << 5);
	*frame |= (ax25->vs << 1);

	ax25_transmit_buffer(ax25, skb, C_COMMAND);	
}

void ax25_kick(ax25_cb *ax25)
{
	struct sk_buff *skb, *skbn;
	int last = 1;
	unsigned short start, end, next;

	del_timer(&ax25->timer);

	start = (skb_peek(&ax25->ack_queue) == NULL) ? ax25->va : ax25->vs;
	end   = (ax25->va + ax25->window) % MODULUS;

	if (!(ax25->condition & PEER_RX_BUSY_CONDITION) &&
	    start != end                                   &&
	    skb_peek(&ax25->write_queue) != NULL) {

		ax25->vs = start;

		/*
		 * Transmit data until either we're out of data to send or
		 * the window is full. Send a poll on the final I frame if
		 * the window is filled.
		 */
		do {
			/*
			 * Dequeue the frame and copy it.
			 */
			skb  = skb_dequeue(&ax25->write_queue);

			if ((skbn = skb_clone(skb, GFP_ATOMIC)) == NULL) {
				skb_queue_head(&ax25->write_queue, skb);
				return;
			}

			next = (ax25->vs + 1) % MODULUS;
#ifdef notdef
			last = (next == end) || skb_peek(&ax25->write_queue) == NULL;
#else
			last = (next == end);
#endif
			/*
			 * Transmit the frame copy.
			 */
			ax25_send_iframe(ax25, skbn, (last) ? PF : 0);

			ax25->vs = next;

			/*
			 * Requeue the original data frame.
			 */
			skb_queue_tail(&ax25->ack_queue, skb);
#ifdef notdef
		} while (!last);
#else
		} while (!last && skb_peek(&ax25->write_queue) != NULL);
#endif
		ax25->condition &= ~ACK_PENDING_CONDITION;

		if (ax25->t1timer == 0) {
			ax25->t3timer = 0;
			ax25->t1timer = ax25->t1 = ax25_calculate_t1(ax25);
		}
	}

	ax25_set_timer(ax25);
}

void ax25_transmit_buffer(ax25_cb *ax25, struct sk_buff *skb, int type)
{
	unsigned char *ptr = skb->data;

	if (ax25->device == NULL) {
		if (ax25->sk != NULL) {
			ax25->sk->state = TCP_CLOSE;
			ax25->sk->err   = ENETUNREACH;
			if (!ax25->sk->dead)
				ax25->sk->state_change(ax25->sk);
			ax25->sk->dead  = 1;
		}
		return;
	}

	*ptr++ = 0;	/* KISS data */
	ptr   += build_ax25_addr(ptr, &ax25->source_addr, &ax25->dest_addr, ax25->digipeat, type);

	skb->arp = 1;

	dev_queue_xmit(skb, ax25->device, SOPRI_NORMAL);
}

/*
 * The following routines are taken from page 170 of the 7th ARRL Computer
 * Networking Conference paper, as is the whole state machine.
 */

void ax25_nr_error_recovery(ax25_cb *ax25)
{
	ax25_establish_data_link(ax25);
}

void ax25_establish_data_link(ax25_cb *ax25)
{
	ax25->condition = 0x00;
	ax25->n2count   = 0;

	ax25_send_control(ax25, SABM | PF, C_COMMAND);

	ax25->t3timer = 0;
	ax25->t2timer = 0;
	ax25->t1timer = ax25->t1 = ax25_calculate_t1(ax25);
}

void ax25_transmit_enquiry(ax25_cb *ax25)
{
	if (ax25->condition & OWN_RX_BUSY_CONDITION)
		ax25_send_control(ax25, RNR | PF, C_COMMAND);
	else
		ax25_send_control(ax25, RR | PF, C_COMMAND);

	ax25->condition &= ~ACK_PENDING_CONDITION;

	ax25->t1timer = ax25->t1 = ax25_calculate_t1(ax25);
}
 	
void ax25_enquiry_response(ax25_cb *ax25)
{
	if (ax25->condition & OWN_RX_BUSY_CONDITION)
		ax25_send_control(ax25, RNR | PF, C_RESPONSE);
	else
		ax25_send_control(ax25, RR | PF, C_RESPONSE);

	ax25->condition &= ~ACK_PENDING_CONDITION;
}

void ax25_check_iframes_acked(ax25_cb *ax25, unsigned short nr)
{
	if (ax25->vs == nr) {
		ax25_frames_acked(ax25, nr);
		ax25_calculate_rtt(ax25);
		ax25->t1timer = 0;
		ax25->t3timer = ax25->t3;
	} else {
		if (ax25->va != nr) {
			ax25_frames_acked(ax25, nr);
			ax25->t1timer = ax25->t1 = ax25_calculate_t1(ax25);
		}
	}
}

void ax25_check_need_response(ax25_cb *ax25, int type, int pf)
{
	if (type == C_COMMAND && pf)
		ax25_enquiry_response(ax25);
}

#endif

/*
 *	AX.25 release 031
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
 *	AX.25 030	Jonathan(G4KLX)	Added fragmentation to ax25_output.
 *					Added support for extended AX.25.
 *	AX.25 031	Joerg(DL1BKE)	Added DAMA support
 *
 *			Joerg(DL1BKE)	Modified fragmenter to fragment vanilla 
 *					AX.25 I-Frames. Added PACLEN parameter.
 *			Joerg(DL1BKE)	Fixed a problem with buffer allocation
 *					for fragments.
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

/*
 * All outgoing AX.25 I frames pass via this routine. Therefore this is
 * where the fragmentation of frames takes place.
 */
void ax25_output(ax25_cb *ax25, struct sk_buff *skb)
{
	struct sk_buff *skbn;
	unsigned char *p;
	int frontlen, mtu, len, fragno, ka9qfrag, first = 1;
	long flags;
	
	/*
	 * dl1bke 960301: We use the new PACLEN parameter as MTU of the AX.25 layer.
	 *                This will (hopefully) allow user programs to write() data
	 *                w/o having to think of the maximal amount of data we can
	 *		  send with one call. It's called PACLEN to (1) avoid confusion
	 *		  with (IP) MTU and (2) TAPR calls this PACLEN, too ;-)
	 */

	mtu = ax25->paclen;
	
	if ((skb->len - 1) > mtu) {
		if (*skb->data == AX25_P_TEXT) {
			skb_pull(skb, 1); /* skip PID */
			ka9qfrag = 0;
		} else {
			mtu -= 2;	/* Allow for fragment control info */
			ka9qfrag = 1;
		}
		
		fragno = skb->len / mtu;
		if (skb->len % mtu == 0) fragno--;

		frontlen = skb_headroom(skb);	/* Address space + CTRL */

		while (skb->len > 0) {
			save_flags(flags); 
			cli();
			/* 
			 * do _not_ use sock_alloc_send_skb, our socket may have
			 * sk->shutdown set...
			 */
			if ((skbn = alloc_skb(mtu + 2 + frontlen, GFP_ATOMIC)) == NULL) {
				restore_flags(flags);
				printk(KERN_DEBUG "ax25_output: alloc_skb returned NULL\n");
				if (skb_device_locked(skb))
					skb_device_unlock(skb);
				return;
			}

			skbn->sk   = skb->sk;
			
			if (skbn->sk)
				atomic_add(skbn->truesize, &skbn->sk->wmem_alloc);
			
			restore_flags(flags);
			
			skbn->free = 1;
			skbn->arp  = 1;

			len = (mtu > skb->len) ? skb->len : mtu;
			
			if (ka9qfrag == 1) {
				skb_reserve(skbn, frontlen + 2);

				memcpy(skb_put(skbn, len), skb->data, len);
				p = skb_push(skbn, 2);

				*p++ = AX25_P_SEGMENT;

				*p = fragno--;
				if (first) {
					*p |= SEG_FIRST;
					first = 0;
				}
			} else {
				skb_reserve(skbn, frontlen + 1);
				memcpy(skb_put(skbn, len), skb->data, len);
				p = skb_push(skbn, 1);
				*p = AX25_P_TEXT;
			}

			skb_pull(skb, len);
			skb_queue_tail(&ax25->write_queue, skbn); /* Throw it on the queue */
		}
		
		skb->free = 1;
		kfree_skb(skb, FREE_WRITE);
	} else {
		skb_queue_tail(&ax25->write_queue, skb);	  /* Throw it on the queue */
	}

	if (ax25->state == AX25_STATE_3 || ax25->state == AX25_STATE_4) {
		if (!ax25->dama_slave)		/* bke 960114: we aren't allowed to transmit */
			ax25_kick(ax25);	/* in DAMA mode unless we received a Poll */
	}
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

	if (ax25->modulus == MODULUS) {
		frame = skb_push(skb, 1);

		*frame = I;
		*frame |= (poll_bit) ? PF : 0;
		*frame |= (ax25->vr << 5);
		*frame |= (ax25->vs << 1);
	} else {
		frame = skb_push(skb, 2);

		frame[0] = I;
		frame[0] |= (ax25->vs << 1);
		frame[1] = (poll_bit) ? EPF : 0;
		frame[1] |= (ax25->vr << 1);
	}

	ax25_transmit_buffer(ax25, skb, C_COMMAND);	
}

void ax25_kick(ax25_cb *ax25)
{
	struct sk_buff *skb, *skbn;
	int last = 1;
	unsigned short start, end, next;

	del_timer(&ax25->timer);

	start = (skb_peek(&ax25->ack_queue) == NULL) ? ax25->va : ax25->vs;
	end   = (ax25->va + ax25->window) % ax25->modulus;

	if (!(ax25->condition & PEER_RX_BUSY_CONDITION) &&
	    start != end                                   &&
	    skb_peek(&ax25->write_queue) != NULL) {

		ax25->vs = start;

		/*
		 * Transmit data until either we're out of data to send or
		 * the window is full. Send a poll on the final I frame if
		 * the window is filled.
		 */

		/*
		 * Dequeue the frame and copy it.
		 */
		skb  = skb_dequeue(&ax25->write_queue);

		do {
			if ((skbn = skb_clone(skb, GFP_ATOMIC)) == NULL) {
				skb_queue_head(&ax25->write_queue, skb);
				return;
			}

			next = (ax25->vs + 1) % ax25->modulus;
#ifdef notdef
			last = (next == end) || skb_peek(&ax25->write_queue) == NULL;
#else
			last = (next == end);
#endif
			/*
			 * Transmit the frame copy.
			 * bke 960114: do not set the Poll bit on the last frame
			 * in DAMA mode.
			 */
			ax25_send_iframe(ax25, skbn, (last && !ax25->dama_slave) ? POLLON : POLLOFF);

			ax25->vs = next;

			/*
			 * Requeue the original data frame.
			 */
			skb_queue_tail(&ax25->ack_queue, skb);
#ifdef notdef
		} while (!last);
#else
		} while (!last && (skb = skb_dequeue(&ax25->write_queue)) != NULL);
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
	unsigned char *ptr;

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

	if (skb_headroom(skb) < size_ax25_addr(ax25->digipeat)) {
		printk(KERN_CRIT "ax25_transmit_buffer: not enough room for digi-peaters\n");
		skb->free = 1;
		kfree_skb(skb, FREE_WRITE);
		return;
	}

	ptr = skb_push(skb, size_ax25_addr(ax25->digipeat));
	build_ax25_addr(ptr, &ax25->source_addr, &ax25->dest_addr, ax25->digipeat, type, ax25->modulus);

	skb->arp = 1;

	ax25_queue_xmit(skb, ax25->device, SOPRI_NORMAL);
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

	if (ax25->modulus == MODULUS) {
		ax25_send_control(ax25, SABM, POLLON, C_COMMAND);
	} else {
		ax25_send_control(ax25, SABME, POLLON, C_COMMAND);
	}
	
	ax25->t3timer = 0;
	ax25->t2timer = 0;
	ax25->t1timer = ax25->t1 = ax25_calculate_t1(ax25);
}

void ax25_transmit_enquiry(ax25_cb *ax25)
{
	if (ax25->condition & OWN_RX_BUSY_CONDITION)
		ax25_send_control(ax25, RNR, POLLON, C_COMMAND);
	else
		ax25_send_control(ax25, RR, POLLON, C_COMMAND);

	ax25->condition &= ~ACK_PENDING_CONDITION;

	ax25->t1timer = ax25->t1 = ax25_calculate_t1(ax25);
}
 	
void ax25_enquiry_response(ax25_cb *ax25)
{
	if (ax25->condition & OWN_RX_BUSY_CONDITION)
		ax25_send_control(ax25, RNR, POLLON, C_RESPONSE);
	else
		ax25_send_control(ax25, RR, POLLON, C_RESPONSE);

	ax25->condition &= ~ACK_PENDING_CONDITION;
}

void ax25_timeout_response(ax25_cb *ax25)
{
	if (ax25->condition & OWN_RX_BUSY_CONDITION)
		ax25_send_control(ax25, RNR, POLLOFF, C_RESPONSE);
	else
		ax25_send_control(ax25, RR, POLLOFF, C_RESPONSE);

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

/*
 *	dl1bke 960114: shouldn't ax25/dama_check_need_response reside as
 *                static inline void ...() in ax25.h, should it? ;-)
 */
void ax25_check_need_response(ax25_cb *ax25, int type, int pf)
{
	if (!ax25->dama_slave && type == C_COMMAND && pf)
		ax25_enquiry_response(ax25);
}

/*
 *	dl1bke 960114: transmit I frames on DAMA poll
 */
void dama_enquiry_response(ax25_cb *ax25)
{
	ax25_cb *ax25o;
	
	if (!(ax25->condition & PEER_RX_BUSY_CONDITION)) {
		ax25_requeue_frames(ax25);
		ax25_kick(ax25);
	}

	if (ax25->state == AX25_STATE_1 || ax25->state == AX25_STATE_2 ||
	    skb_peek(&ax25->ack_queue) != NULL) {
		ax25_t1_timeout(ax25);
	} else {
		ax25->n2count = 0;
	}
	
	ax25->t3timer = ax25->t3;
	

	/* The FLEXNET DAMA master implementation refuses to send us ANY */
	/* I frame for this connection if we send a REJ here, probably   */
	/* due to its frame collector scheme? A simple RR or  RNR will   */
	/* invoke the retransmission, and in fact REJs are superfluous   */
	/* in DAMA mode anyway...					 */
	
#if 0
	if (ax25->condition & REJECT_CONDITION)
		ax25_send_control(ax25, REJ, POLLOFF, C_RESPONSE);
	else
#endif	
		ax25_enquiry_response(ax25);
		
	/* Note that above response to the poll could be sent behind the  */
	/* transmissions of the other channels as well... This version    */	
	/* gives better performance on FLEXNET nodes. (Why, Gunter?)	  */

	for (ax25o = ax25_list; ax25o != NULL; ax25o = ax25o->next) {
		if (ax25o == ax25)
			continue;

		if (ax25o->device != ax25->device)
			continue;

		if (ax25o->state == AX25_STATE_1 || ax25o->state == AX25_STATE_2) {
			ax25_t1_timeout(ax25o);
			continue;
		}

		if (!ax25o->dama_slave)
			continue;
			
		if ( !(ax25o->condition & PEER_RX_BUSY_CONDITION) && 
		     (ax25o->state == AX25_STATE_3 || 
		     (ax25o->state == AX25_STATE_4 && ax25o->t1timer == 0))) {
			ax25_requeue_frames(ax25o);
			ax25_kick(ax25o);
		}
		
		if (ax25o->state == AX25_STATE_1 || ax25o->state == AX25_STATE_2 ||
		    skb_peek(&ax25o->ack_queue) != NULL) {
			ax25_t1_timeout(ax25o);
		}

		ax25o->t3timer = ax25o->t3;
	}
}

void dama_check_need_response(ax25_cb *ax25, int type, int pf)
{
	if (ax25->dama_slave && type == C_COMMAND && pf)
		dama_enquiry_response(ax25);
}

void dama_establish_data_link(ax25_cb *ax25)
{
	ax25->condition = 0x00;
	ax25->n2count   = 0;

	ax25->t3timer = ax25->t3;
	ax25->t2timer = 0;
	ax25->t1timer = ax25->t1 = ax25_calculate_t1(ax25);
}

#endif

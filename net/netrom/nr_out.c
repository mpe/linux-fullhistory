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
 *	NET/ROM 001	Jonathan(G4KLX)	Cloned from ax25_out.c
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

int nr_output(struct sock *sk, struct sk_buff *skb)
{
	skb_queue_tail(&sk->write_queue, skb);	/* Throw it on the queue */

	if (sk->nr->state == NR_STATE_3)
		nr_kick(sk);

	return 0;
}

/* 
 *  This procedure is passed a buffer descriptor for an iframe. It builds
 *  the rest of the control part of the frame and then writes it out.
 */
static void nr_send_iframe(struct sock *sk, struct sk_buff *skb)
{
	unsigned char *dptr;

	if (skb == NULL)
		return;

	dptr = skb->data + 17;
	
	*dptr++ = sk->nr->vs;
	*dptr++ = sk->nr->vr;

	nr_transmit_buffer(sk, skb);	
}

void nr_send_nak_frame(struct sock *sk)
{
	struct sk_buff *skb, *skbn;
	
	if ((skb = skb_peek(&sk->nr->ack_queue)) == NULL)
		return;
		
	if ((skbn = skb_clone(skb, GFP_ATOMIC)) == NULL)
		return;

	nr_send_iframe(sk, skbn);

	sk->nr->condition &= ~ACK_PENDING_CONDITION;
	sk->nr->vl      = sk->nr->vr;
	sk->nr->t1timer = 0;
}

void nr_kick(struct sock *sk)
{
	struct sk_buff *skb, *skbn;
	int last = 1;
	unsigned short start, end, next;

	del_timer(&sk->timer);

	start = (skb_peek(&sk->nr->ack_queue) == NULL) ? sk->nr->va : sk->nr->vs;
	end   = (sk->nr->va + sk->window) % NR_MODULUS;

	if (!(sk->nr->condition & PEER_RX_BUSY_CONDITION) &&
	    start != end                                   &&
	    skb_peek(&sk->write_queue) != NULL) {

		sk->nr->vs = start;

		/*
		 * Transmit data until either we're out of data to send or
		 * the window is full.
		 */
		do {
			/*
			 * Dequeue the frame and copy it.
			 */
			skb  = skb_dequeue(&sk->write_queue);

			if ((skbn = skb_clone(skb, GFP_ATOMIC)) == NULL) {
				skb_queue_head(&sk->write_queue, skb);
				return;
			}

			next = (sk->nr->vs + 1) % NR_MODULUS;
			last = (next == end);

			/*
			 * Transmit the frame copy.
			 */
			nr_send_iframe(sk, skbn);

			sk->nr->vs = next;

			/*
			 * Requeue the original data frame.
			 */
			skb_queue_tail(&sk->nr->ack_queue, skb);

		} while (!last && skb_peek(&sk->write_queue) != NULL);

		sk->nr->vl = sk->nr->vr;
		sk->nr->condition &= ~ACK_PENDING_CONDITION;

		if (sk->nr->t1timer == 0) {
			sk->nr->t1timer = sk->nr->t1 = nr_calculate_t1(sk);
		}
	}

	nr_set_timer(sk);
}

void nr_transmit_buffer(struct sock *sk, struct sk_buff *skb)
{
	unsigned char *dptr;

	/*
	 *	Add the protocol byte
	 */
	 
	dptr = skb_push(skb,1);

	*dptr++ = AX25_P_NETROM;
	
	memcpy(dptr, &sk->nr->source_addr, sizeof(ax25_address));
	dptr[6] &= ~LAPB_C;
	dptr[6] &= ~LAPB_E;
	dptr[6] |= SSID_SPARE;
	dptr += 7;

	memcpy(dptr, &sk->nr->dest_addr,   sizeof(ax25_address));
	dptr[6] &= ~LAPB_C;
	dptr[6] |= LAPB_E;
	dptr[6] |= SSID_SPARE;
	dptr += 7;

	*dptr++ = nr_default.ttl;

	skb->arp = 1;

	if (!nr_route_frame(skb, NULL)) {
		kfree_skb(skb, FREE_WRITE);

		sk->state = TCP_CLOSE;
		sk->err   = ENETUNREACH;
		if (!sk->dead)
			sk->state_change(sk);
		sk->dead  = 1;
	}
}

/*
 * The following routines are taken from page 170 of the 7th ARRL Computer
 * Networking Conference paper, as is the whole state machine.
 */

void nr_nr_error_recovery(struct sock *sk)
{
	nr_establish_data_link(sk);
}

void nr_establish_data_link(struct sock *sk)
{
	sk->nr->condition = 0x00;
	sk->nr->n2count   = 0;

	nr_write_internal(sk, NR_CONNREQ);

	sk->nr->t2timer = 0;
	sk->nr->t1timer = sk->nr->t1 = nr_calculate_t1(sk);
}

/*
 * Never send a NAK when we are CHOKEd.
 */
void nr_enquiry_response(struct sock *sk)
{
	int frametype = NR_INFOACK;
	
	if (sk->nr->condition & OWN_RX_BUSY_CONDITION) {
		frametype += NR_CHOKE_FLAG;
	} else {
		if (skb_peek(&sk->nr->reseq_queue) != NULL) {
			frametype += NR_NAK_FLAG;
		}
	}
	
	nr_write_internal(sk, frametype);

	sk->nr->vl = sk->nr->vr;
	sk->nr->condition &= ~ACK_PENDING_CONDITION;
}

void nr_check_iframes_acked(struct sock *sk, unsigned short nr)
{
	if (sk->nr->vs == nr) {
		nr_frames_acked(sk, nr);
		nr_requeue_frames(sk);
		nr_calculate_rtt(sk);
		sk->nr->t1timer = 0;
		sk->nr->n2count = 0;
	} else {
		if (sk->nr->va != nr) {
			nr_frames_acked(sk, nr);
			nr_requeue_frames(sk);
			sk->nr->t1timer = sk->nr->t1 = nr_calculate_t1(sk);
		}
	}
}

#endif

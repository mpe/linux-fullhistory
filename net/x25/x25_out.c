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

/*
 *	This is where all X.25 information frames pass;
 */
void x25_output(struct sock *sk, struct sk_buff *skb)
{
	struct sk_buff *skbn;
	unsigned char header[X25_EXT_MIN_LEN];
	int err, frontlen, len, min_len;
	
	min_len  = (sk->protinfo.x25->neighbour->extended) ? X25_EXT_MIN_LEN : X25_STD_MIN_LEN;

	if (skb->len - min_len > 128) {		/* XXX */
		/* Save a copy of the Header */
		memcpy(header, skb->data, min_len);
		skb_pull(skb, min_len);

		frontlen = skb_headroom(skb);

		while (skb->len > 0) {
			if ((skbn = sock_alloc_send_skb(sk, frontlen + 128, 0, 0, &err)) == NULL)	/* XXX */
				return;

			skbn->sk   = sk;
			skbn->arp  = 1;

			skb_reserve(skbn, frontlen);

			len = (128 > skb->len) ? skb->len : 128;	/* XXX */

			/* Copy the user data */
			memcpy(skb_put(skbn, len), skb->data, len);
			skb_pull(skb, len);

			/* Duplicate the Header */
			skb_push(skbn, min_len);
			memcpy(skbn->data, header, min_len);

			if (skb->len > 0) {
				if (sk->protinfo.x25->neighbour->extended)
					skbn->data[3] |= X25_EXT_M_BIT;
				else
					skbn->data[2] |= X25_STD_M_BIT;
			}
		
			skb_queue_tail(&sk->write_queue, skbn); /* Throw it on the queue */
		}
		
		kfree_skb(skb, FREE_WRITE);
	} else {
		skb_queue_tail(&sk->write_queue, skb);		/* Throw it on the queue */
	}

	if (sk->protinfo.x25->state == X25_STATE_3)
		x25_kick(sk);
}

/* 
 *	This procedure is passed a buffer descriptor for an iframe. It builds
 *	the rest of the control part of the frame and then writes it out.
 */
static void x25_send_iframe(struct sock *sk, struct sk_buff *skb)
{
	if (skb == NULL)
		return;

	if (sk->protinfo.x25->neighbour->extended) {
		skb->data[2] |= (sk->protinfo.x25->vs << 1) & 0xFE;
		skb->data[3] |= (sk->protinfo.x25->vr << 1) & 0xFE;
	} else {
		skb->data[2] |= (sk->protinfo.x25->vs << 1) & 0x0E;
		skb->data[2] |= (sk->protinfo.x25->vr << 5) & 0xE0;
	}

	x25_transmit_link(skb, sk->protinfo.x25->neighbour);	
}

void x25_kick(struct sock *sk)
{
	struct sk_buff *skb, *skbn;
	int last = 1;
	unsigned short start, end, next;
	int modulus;
	
	modulus = (sk->protinfo.x25->neighbour->extended) ? X25_EMODULUS : X25_SMODULUS;

	del_timer(&sk->timer);

	start = (skb_peek(&sk->protinfo.x25->ack_queue) == NULL) ? sk->protinfo.x25->va : sk->protinfo.x25->vs;
	end   = (sk->protinfo.x25->va + sk->protinfo.x25->facilities.window_size) % modulus;

	if (!(sk->protinfo.x25->condition & X25_COND_PEER_RX_BUSY) &&
	    start != end                                           &&
	    skb_peek(&sk->write_queue) != NULL) {

		sk->protinfo.x25->vs = start;

		/*
		 * Transmit data until either we're out of data to send or
		 * the window is full.
		 */

		/*
		 * Dequeue the frame and copy it.
		 */
		skb  = skb_dequeue(&sk->write_queue);

		do {
			if ((skbn = skb_clone(skb, GFP_ATOMIC)) == NULL) {
				skb_queue_head(&sk->write_queue, skb);
				break;
			}

			next = (sk->protinfo.x25->vs + 1) % modulus;
			last = (next == end);

			/*
			 * Transmit the frame copy.
			 */
			x25_send_iframe(sk, skbn);

			sk->protinfo.x25->vs = next;

			/*
			 * Requeue the original data frame.
			 */
			skb_queue_tail(&sk->protinfo.x25->ack_queue, skb);

		} while (!last && (skb = skb_dequeue(&sk->write_queue)) != NULL);

		sk->protinfo.x25->vl         = sk->protinfo.x25->vr;
		sk->protinfo.x25->condition &= ~X25_COND_ACK_PENDING;
		sk->protinfo.x25->timer      = 0;
	}

	x25_set_timer(sk);
}

/*
 * The following routines are taken from page 170 of the 7th ARRL Computer
 * Networking Conference paper, as is the whole state machine.
 */

void x25_enquiry_response(struct sock *sk)
{
	if (sk->protinfo.x25->condition & X25_COND_OWN_RX_BUSY)
		x25_write_internal(sk, X25_RNR);
	else
		x25_write_internal(sk, X25_RR);

	sk->protinfo.x25->vl         = sk->protinfo.x25->vr;
	sk->protinfo.x25->condition &= ~X25_COND_ACK_PENDING;
	sk->protinfo.x25->timer      = 0;
}

void x25_check_iframes_acked(struct sock *sk, unsigned short nr)
{
	if (sk->protinfo.x25->vs == nr) {
		x25_frames_acked(sk, nr);
	} else {
		if (sk->protinfo.x25->va != nr) {
			x25_frames_acked(sk, nr);
		}
	}
}

#endif

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
 *	NET/ROM 001	Jonathan(G4KLX)	Cloned from ax25_subr.c
 *	NET/ROM	003	Jonathan(G4KLX)	Added G8BPQ NET/ROM extensions.
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

/*
 *	This routine purges all of the queues of frames.
 */
void nr_clear_queues(struct sock *sk)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&sk->write_queue)) != NULL) {
		skb->sk   = sk;
		skb->free = 1;
		kfree_skb(skb, FREE_WRITE);
	}

	while ((skb = skb_dequeue(&sk->nr->ack_queue)) != NULL) {
		skb->sk   = sk;
		skb->free = 1;
		kfree_skb(skb, FREE_WRITE);
	}

	while ((skb = skb_dequeue(&sk->nr->reseq_queue)) != NULL) {
		kfree_skb(skb, FREE_READ);
	}

	while ((skb = skb_dequeue(&sk->nr->frag_queue)) != NULL) {
		kfree_skb(skb, FREE_READ);
	}
}

/*
 * This routine purges the input queue of those frames that have been
 * acknowledged. This replaces the boxes labelled "V(a) <- N(r)" on the
 * SDL diagram.
 */
void nr_frames_acked(struct sock *sk, unsigned short nr)
{
	struct sk_buff *skb;

	/*
	 * Remove all the ack-ed frames from the ack queue.
	 */
	if (sk->nr->va != nr) {
		while (skb_peek(&sk->nr->ack_queue) != NULL && sk->nr->va != nr) {
		        skb = skb_dequeue(&sk->nr->ack_queue);
		        skb->sk   = sk;
			skb->free = 1;
			kfree_skb(skb, FREE_WRITE);
			sk->nr->va = (sk->nr->va + 1) % NR_MODULUS;
		}
	}
}

/*
 * Requeue all the un-ack-ed frames on the output queue to be picked
 * up by nr_kick called from the timer. This arrangement handles the
 * possibility of an empty output queue.
 */
void nr_requeue_frames(struct sock *sk)
{
	struct sk_buff *skb, *skb_prev = NULL;

	while ((skb = skb_dequeue(&sk->nr->ack_queue)) != NULL) {
		if (skb_prev == NULL)
			skb_queue_head(&sk->write_queue, skb);
		else
			skb_append(skb_prev, skb);
		skb_prev = skb;
	}
}

/*
 *	Validate that the value of nr is between va and vs. Return true or
 *	false for testing.
 */
int nr_validate_nr(struct sock *sk, unsigned short nr)
{
	unsigned short vc = sk->nr->va;

	while (vc != sk->nr->vs) {
		if (nr == vc) return 1;
		vc = (vc + 1) % NR_MODULUS;
	}
	
	if (nr == sk->nr->vs) return 1;

	return 0;
}

/*
 *	Check that ns is within the receive window.
 */
int nr_in_rx_window(struct sock *sk, unsigned short ns)
{
	unsigned short vc = sk->nr->vr;
	unsigned short vt = (sk->nr->vl + sk->window) % NR_MODULUS;

	while (vc != vt) {
		if (ns == vc) return 1;
		vc = (vc + 1) % NR_MODULUS;
	}

	return 0;
}

/* 
 *  This routine is called when the HDLC layer internally generates a
 *  control frame.
 */
void nr_write_internal(struct sock *sk, int frametype)
{
	struct sk_buff *skb;
	unsigned char  *dptr;
	int len, timeout;

	len = AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + NR_NETWORK_LEN + NR_TRANSPORT_LEN;
	
	switch (frametype & 0x0F) {
		case NR_CONNREQ:
			len += 17;
			break;
		case NR_CONNACK:
			len += (sk->nr->bpqext) ? 2 : 1;
			break;
		case NR_DISCREQ:
		case NR_DISCACK:
		case NR_INFOACK:
			break;
		default:
			printk(KERN_ERR "nr_write_internal: invalid frame type %d\n", frametype);
			return;
	}
	
	if ((skb = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	/*
	 *	Space for AX.25 and NET/ROM network header
	 */
	skb_reserve(skb, AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + NR_NETWORK_LEN);
	
	dptr = skb_put(skb, skb_tailroom(skb));

	switch (frametype & 0x0F) {

		case NR_CONNREQ:
			timeout  = (sk->nr->rtt / PR_SLOWHZ) * 2;
			*dptr++  = sk->nr->my_index;
			*dptr++  = sk->nr->my_id;
			*dptr++  = 0;
			*dptr++  = 0;
			*dptr++  = frametype;
			*dptr++  = sk->window;
			memcpy(dptr, &sk->nr->user_addr, AX25_ADDR_LEN);
			dptr[6] &= ~LAPB_C;
			dptr[6] &= ~LAPB_E;
			dptr[6] |= SSSID_SPARE;
			dptr    += AX25_ADDR_LEN;
			memcpy(dptr, &sk->nr->source_addr, AX25_ADDR_LEN);
			dptr[6] &= ~LAPB_C;
			dptr[6] &= ~LAPB_E;
			dptr[6] |= SSSID_SPARE;
			dptr    += AX25_ADDR_LEN;
			*dptr++  = timeout % 256;
			*dptr++  = timeout / 256;
			break;

		case NR_CONNACK:
			*dptr++ = sk->nr->your_index;
			*dptr++ = sk->nr->your_id;
			*dptr++ = sk->nr->my_index;
			*dptr++ = sk->nr->my_id;
			*dptr++ = frametype;
			*dptr++ = sk->window;
			if (sk->nr->bpqext) *dptr++ = nr_default.ttl;
			break;

		case NR_DISCREQ:
		case NR_DISCACK:
			*dptr++ = sk->nr->your_index;
			*dptr++ = sk->nr->your_id;
			*dptr++ = 0;
			*dptr++ = 0;
			*dptr++ = frametype;
			break;

		case NR_INFOACK:
			*dptr++ = sk->nr->your_index;
			*dptr++ = sk->nr->your_id;
			*dptr++ = 0;
			*dptr++ = sk->nr->vr;
			*dptr++ = frametype;
			break;
	}

	skb->free = 1;

	nr_transmit_buffer(sk, skb);
}

/*
 * This routine is called when a Connect Acknowledge with the Choke Flag
 * set is needed to refuse a connection.
 */
void nr_transmit_dm(struct sk_buff *skb)
{
	struct sk_buff *skbn;
	unsigned char *dptr;
	int len;

	len = AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + NR_NETWORK_LEN + NR_TRANSPORT_LEN + 1;

	if ((skbn = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skbn, AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN);

	dptr = skb_put(skbn, NR_NETWORK_LEN + NR_TRANSPORT_LEN);

	memcpy(dptr, skb->data + 7, AX25_ADDR_LEN);
	dptr[6] &= ~LAPB_C;
	dptr[6] &= ~LAPB_E;
	dptr[6] |= SSSID_SPARE;
	dptr += AX25_ADDR_LEN;
	
	memcpy(dptr, skb->data + 0, AX25_ADDR_LEN);
	dptr[6] &= ~LAPB_C;
	dptr[6] |= LAPB_E;
	dptr[6] |= SSSID_SPARE;
	dptr += AX25_ADDR_LEN;

	*dptr++ = nr_default.ttl;

	*dptr++ = skb->data[15];
	*dptr++ = skb->data[16];
	*dptr++ = 0;
	*dptr++ = 0;
	*dptr++ = NR_CONNACK | NR_CHOKE_FLAG;
	*dptr++ = 0;

	skbn->free = 1;
	skbn->sk   = NULL;

	if (!nr_route_frame(skbn, NULL))
		kfree_skb(skbn, FREE_WRITE);
}

/*
 *	Exponential backoff for NET/ROM
 */
unsigned short nr_calculate_t1(struct sock *sk)
{
	int n, t;
	
	for (t = 2, n = 0; n < sk->nr->n2count; n++)
		t *= 2;

	if (t > 8) t = 8;

	return t * sk->nr->rtt;
}

/*
 *	Calculate the Round Trip Time
 */
void nr_calculate_rtt(struct sock *sk)
{
	if (sk->nr->t1timer > 0 && sk->nr->n2count == 0)
		sk->nr->rtt = (9 * sk->nr->rtt + sk->nr->t1 - sk->nr->t1timer) / 10;

#ifdef	NR_T1CLAMPLO
	/* Don't go below one tenth of a second */
	if (sk->nr->rtt < (NR_T1CLAMPLO))
		sk->nr->rtt = (NR_T1CLAMPLO);
#else   /* Failsafe - some people might have sub 1/10th RTTs :-) **/
        if (sk->nr->rtt == 0)
                sk->nr->rtt = PR_SLOWHZ;
#endif
#ifdef  NR_T1CLAMPHI
        /* OR above clamped seconds **/
        if (sk->nr->rtt > (NR_T1CLAMPHI))
                sk->nr->rtt = (NR_T1CLAMPHI);
#endif
}

#endif

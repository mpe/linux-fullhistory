/*
 *	NET/ROM release 004
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
 *	NET/ROM 003	Jonathan(G4KLX)	Added NET/ROM fragmentation.
 *			Darryl(G7LED)	Fixed NAK, to give out correct reponse.
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

/*
 *	This is where all NET/ROM frames pass, except for IP-over-NET/ROM which
 *	cannot be fragmented in this manner.
 */
void nr_output(struct sock *sk, struct sk_buff *skb)
{
	struct sk_buff *skbn;
	unsigned char transport[NR_TRANSPORT_LEN];
	int err, frontlen, len, mtu;

	mtu = sk->protinfo.nr->paclen;
	
	if (skb->len - NR_TRANSPORT_LEN > mtu) {
		/* Save a copy of the Transport Header */
		memcpy(transport, skb->data, NR_TRANSPORT_LEN);
		skb_pull(skb, NR_TRANSPORT_LEN);

		frontlen = skb_headroom(skb);

		while (skb->len > 0) {
			if ((skbn = sock_alloc_send_skb(sk, frontlen + mtu, 0, 0, &err)) == NULL)
				return;

			skbn->sk   = sk;
			skbn->free = 1;
			skbn->arp  = 1;

			skb_reserve(skbn, frontlen);

			len = (mtu > skb->len) ? skb->len : mtu;

			/* Copy the user data */
			memcpy(skb_put(skbn, len), skb->data, len);
			skb_pull(skb, len);

			/* Duplicate the Transport Header */
			skb_push(skbn, NR_TRANSPORT_LEN);
			memcpy(skbn->data, transport, NR_TRANSPORT_LEN);

			if (skb->len > 0)
				skbn->data[4] |= NR_MORE_FLAG;
		
			skb_queue_tail(&sk->write_queue, skbn); /* Throw it on the queue */
		}
		
		skb->free = 1;
		kfree_skb(skb, FREE_WRITE);
	} else {
		skb_queue_tail(&sk->write_queue, skb);		/* Throw it on the queue */
	}

	if (sk->protinfo.nr->state == NR_STATE_3)
		nr_kick(sk);
}

/* 
 *	This procedure is passed a buffer descriptor for an iframe. It builds
 *	the rest of the control part of the frame and then writes it out.
 */
static void nr_send_iframe(struct sock *sk, struct sk_buff *skb)
{
	if (skb == NULL)
		return;

	skb->data[2] = sk->protinfo.nr->vs;
	skb->data[3] = sk->protinfo.nr->vr;

	if (sk->protinfo.nr->condition & OWN_RX_BUSY_CONDITION)
		skb->data[4] |= NR_CHOKE_FLAG;

	nr_transmit_buffer(sk, skb);	
}

void nr_send_nak_frame(struct sock *sk)
{
	struct sk_buff *skb, *skbn;
	
	if ((skb = skb_peek(&sk->protinfo.nr->ack_queue)) == NULL)
		return;
		
	if ((skbn = skb_clone(skb, GFP_ATOMIC)) == NULL)
		return;

	skbn->data[2] = sk->protinfo.nr->va;
	skbn->data[3] = sk->protinfo.nr->vr;

	if (sk->protinfo.nr->condition & OWN_RX_BUSY_CONDITION)
		skbn->data[4] |= NR_CHOKE_FLAG;

	nr_transmit_buffer(sk, skbn);

	sk->protinfo.nr->condition &= ~ACK_PENDING_CONDITION;
	sk->protinfo.nr->vl         = sk->protinfo.nr->vr;
	sk->protinfo.nr->t1timer    = 0;
}

void nr_kick(struct sock *sk)
{
	struct sk_buff *skb, *skbn;
	int last = 1;
	unsigned short start, end, next;

	del_timer(&sk->timer);

	start = (skb_peek(&sk->protinfo.nr->ack_queue) == NULL) ? sk->protinfo.nr->va : sk->protinfo.nr->vs;
	end   = (sk->protinfo.nr->va + sk->window) % NR_MODULUS;

	if (!(sk->protinfo.nr->condition & PEER_RX_BUSY_CONDITION) &&
	    start != end                                  &&
	    skb_peek(&sk->write_queue) != NULL) {

		sk->protinfo.nr->vs = start;

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

			next = (sk->protinfo.nr->vs + 1) % NR_MODULUS;
			last = (next == end);

			/*
			 * Transmit the frame copy.
			 */
			nr_send_iframe(sk, skbn);

			sk->protinfo.nr->vs = next;

			/*
			 * Requeue the original data frame.
			 */
			skb_queue_tail(&sk->protinfo.nr->ack_queue, skb);

		} while (!last && (skb = skb_dequeue(&sk->write_queue)) != NULL);

		sk->protinfo.nr->vl         = sk->protinfo.nr->vr;
		sk->protinfo.nr->condition &= ~ACK_PENDING_CONDITION;

		if (sk->protinfo.nr->t1timer == 0) {
			sk->protinfo.nr->t1timer = sk->protinfo.nr->t1 = nr_calculate_t1(sk);
		}
	}

	nr_set_timer(sk);
}

void nr_transmit_buffer(struct sock *sk, struct sk_buff *skb)
{
	unsigned char *dptr;

	/*
	 *	Add the protocol byte and network header.
	 */
	dptr = skb_push(skb, NR_NETWORK_LEN);

	memcpy(dptr, &sk->protinfo.nr->source_addr, AX25_ADDR_LEN);
	dptr[6] &= ~LAPB_C;
	dptr[6] &= ~LAPB_E;
	dptr[6] |= SSSID_SPARE;
	dptr += AX25_ADDR_LEN;

	memcpy(dptr, &sk->protinfo.nr->dest_addr, AX25_ADDR_LEN);
	dptr[6] &= ~LAPB_C;
	dptr[6] |= LAPB_E;
	dptr[6] |= SSSID_SPARE;
	dptr += AX25_ADDR_LEN;

	*dptr++ = sysctl_netrom_network_ttl_initialiser;

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

void nr_establish_data_link(struct sock *sk)
{
	sk->protinfo.nr->condition = 0x00;
	sk->protinfo.nr->n2count   = 0;

	nr_write_internal(sk, NR_CONNREQ);

	sk->protinfo.nr->t2timer = 0;
	sk->protinfo.nr->t1timer = sk->protinfo.nr->t1 = nr_calculate_t1(sk);
}

/*
 * Never send a NAK when we are CHOKEd.
 */
void nr_enquiry_response(struct sock *sk)
{
	int frametype = NR_INFOACK;
	
	if (sk->protinfo.nr->condition & OWN_RX_BUSY_CONDITION) {
		frametype |= NR_CHOKE_FLAG;
	} else {
		if (skb_peek(&sk->protinfo.nr->reseq_queue) != NULL) {
			frametype |= NR_NAK_FLAG;
		}
	}
	
	nr_write_internal(sk, frametype);

	sk->protinfo.nr->vl         = sk->protinfo.nr->vr;
	sk->protinfo.nr->condition &= ~ACK_PENDING_CONDITION;
}

void nr_check_iframes_acked(struct sock *sk, unsigned short nr)
{
	if (sk->protinfo.nr->vs == nr) {
		nr_frames_acked(sk, nr);
		nr_calculate_rtt(sk);
		sk->protinfo.nr->t1timer = 0;
		sk->protinfo.nr->n2count = 0;
	} else {
		if (sk->protinfo.nr->va != nr) {
			nr_frames_acked(sk, nr);
			sk->protinfo.nr->t1timer = sk->protinfo.nr->t1 = nr_calculate_t1(sk);
		}
	}
}

#endif

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
 *	Most of this code is based on the SDL diagrams published in the 7th
 *	ARRL Computer Networking Conference papers. The diagrams have mistakes
 *	in them, but are mostly correct. Before you modify the code could you
 *	read the SDL diagrams as the code is not obvious and probably very
 *	easy to break;
 *
 *	History
 *	NET/ROM 001	Jonathan(G4KLX)	Cloned from ax25_in.c
 *	NET/ROM 003	Jonathan(G4KLX)	Added NET/ROM fragment reception.
 *			Darryl(G7LED)	Added missing INFO with NAK case, optimized
 *					INFOACK handling, removed reconnect on error.
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
#include <net/ip.h>			/* For ip_rcv */
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/netrom.h>

static int nr_queue_rx_frame(struct sock *sk, struct sk_buff *skb, int more)
{
	struct sk_buff *skbo, *skbn = skb;

	if (more) {
		sk->nr->fraglen += skb->len;
		skb_queue_tail(&sk->nr->frag_queue, skb);
		return 0;
	}
	
	if (!more && sk->nr->fraglen > 0) {	/* End of fragment */
		sk->nr->fraglen += skb->len;
		skb_queue_tail(&sk->nr->frag_queue, skb);

		if ((skbn = alloc_skb(sk->nr->fraglen, GFP_ATOMIC)) == NULL)
			return 1;

		skbn->free = 1;
		skbn->arp  = 1;
		skbn->sk   = sk;
		sk->rmem_alloc += skbn->truesize;
		skbn->h.raw = skbn->data;

		skbo = skb_dequeue(&sk->nr->frag_queue);
		memcpy(skb_put(skbn, skbo->len), skbo->data, skbo->len);
		kfree_skb(skbo, FREE_READ);

		while ((skbo = skb_dequeue(&sk->nr->frag_queue)) != NULL) {
			skb_pull(skbo, NR_NETWORK_LEN + NR_TRANSPORT_LEN);
			memcpy(skb_put(skbn, skbo->len), skbo->data, skbo->len);
			kfree_skb(skbo, FREE_READ);
		}

		sk->nr->fraglen = 0;		
	}

	return sock_queue_rcv_skb(sk, skbn);
}

/*
 * State machine for state 1, Awaiting Connection State.
 * The handling of the timer(s) is in file nr_timer.c.
 * Handling of state 0 and connection release is in netrom.c.
 */
static int nr_state1_machine(struct sock *sk, struct sk_buff *skb, int frametype)
{
	switch (frametype) {

		case NR_CONNACK:
			nr_calculate_rtt(sk);
			sk->window         = skb->data[20];
			sk->nr->your_index = skb->data[17];
			sk->nr->your_id    = skb->data[18];
			sk->nr->t1timer    = 0;
			sk->nr->t2timer    = 0;
			sk->nr->t4timer    = 0;
			sk->nr->vs         = 0;
			sk->nr->va         = 0;
			sk->nr->vr         = 0;
			sk->nr->vl	   = 0;
			sk->nr->state      = NR_STATE_3;
			sk->state          = TCP_ESTABLISHED;
			sk->nr->n2count    = 0;
			/* For WAIT_SABM connections we will produce an accept ready socket here */
			if (!sk->dead)
				sk->state_change(sk);
			break;

		case NR_CONNACK | NR_CHOKE_FLAG:
			nr_clear_queues(sk);
			sk->nr->state = NR_STATE_0;
			sk->state     = TCP_CLOSE;
			sk->err       = ECONNREFUSED;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead      = 1;
			break;

		default:
			break;
	}

	return 0;
}

/*
 * State machine for state 2, Awaiting Release State.
 * The handling of the timer(s) is in file nr_timer.c
 * Handling of state 0 and connection release is in netrom.c.
 */
static int nr_state2_machine(struct sock *sk, struct sk_buff *skb, int frametype)
{
	switch (frametype) {

		case NR_DISCREQ:
			nr_write_internal(sk, NR_DISCACK);

		case NR_DISCACK:
			sk->nr->state = NR_STATE_0;
			sk->state     = TCP_CLOSE;
			sk->err       = 0;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead      = 1;
			break;

		default:
			break;
	}

	return 0;
}

/*
 * State machine for state 3, Connected State.
 * The handling of the timer(s) is in file nr_timer.c
 * Handling of state 0 and connection release is in netrom.c.
 */
static int nr_state3_machine(struct sock *sk, struct sk_buff *skb, int frametype)
{
	struct sk_buff_head temp_queue;
	struct sk_buff *skbn;
	unsigned short save_vr;
	unsigned short nr, ns;
	int queued = 0;

	nr = skb->data[18];
	ns = skb->data[17];

	switch (frametype) {

		case NR_CONNREQ:
			nr_write_internal(sk, NR_CONNACK);
			break;

		case NR_DISCREQ:
			nr_clear_queues(sk);
			nr_write_internal(sk, NR_DISCACK);
			sk->nr->state = NR_STATE_0;
			sk->state     = TCP_CLOSE;
			sk->err       = 0;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead      = 1;
			break;

		case NR_DISCACK:
			nr_clear_queues(sk);
			sk->nr->state = NR_STATE_0;
			sk->state     = TCP_CLOSE;
			sk->err       = ECONNRESET;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead      = 1;
			break;

		case NR_INFOACK:
		case NR_INFOACK | NR_CHOKE_FLAG:
		case NR_INFOACK | NR_NAK_FLAG:
		case NR_INFOACK | NR_NAK_FLAG | NR_CHOKE_FLAG:
			if (frametype & NR_CHOKE_FLAG) {
				sk->nr->condition |= PEER_RX_BUSY_CONDITION;
				sk->nr->t4timer = nr_default.busy_delay;
			} else {
				sk->nr->condition &= ~PEER_RX_BUSY_CONDITION;
				sk->nr->t4timer = 0;
			}
			if (!nr_validate_nr(sk, nr)) {
				break;
			}
			if (frametype & NR_NAK_FLAG) {
				nr_frames_acked(sk, nr);
				nr_send_nak_frame(sk);
			} else {
				if (sk->nr->condition & PEER_RX_BUSY_CONDITION) {
					nr_frames_acked(sk, nr);
				} else {
					nr_check_iframes_acked(sk, nr);
				}
			}
			break;
			
		case NR_INFO:
		case NR_INFO | NR_NAK_FLAG:
		case NR_INFO | NR_CHOKE_FLAG:
		case NR_INFO | NR_MORE_FLAG:
		case NR_INFO | NR_NAK_FLAG | NR_CHOKE_FLAG:
		case NR_INFO | NR_CHOKE_FLAG | NR_MORE_FLAG:
		case NR_INFO | NR_NAK_FLAG | NR_MORE_FLAG:
		case NR_INFO | NR_NAK_FLAG | NR_CHOKE_FLAG | NR_MORE_FLAG:
			if (frametype & NR_CHOKE_FLAG) {
				sk->nr->condition |= PEER_RX_BUSY_CONDITION;
				sk->nr->t4timer = nr_default.busy_delay;
			} else {
				sk->nr->condition &= ~PEER_RX_BUSY_CONDITION;
				sk->nr->t4timer = 0;
			}
			if (nr_validate_nr(sk, nr)) {
				if (frametype & NR_NAK_FLAG) {
					nr_frames_acked(sk, nr);
					nr_send_nak_frame(sk);
				} else {
					if (sk->nr->condition & PEER_RX_BUSY_CONDITION) {
						nr_frames_acked(sk, nr);
					} else {
						nr_check_iframes_acked(sk, nr);
					}
				}
			}
			queued = 1;
			skb_queue_head(&sk->nr->reseq_queue, skb);
			if (sk->nr->condition & OWN_RX_BUSY_CONDITION)
				break;
			skb_queue_head_init(&temp_queue);
			do {
				save_vr = sk->nr->vr;
				while ((skbn = skb_dequeue(&sk->nr->reseq_queue)) != NULL) {
					ns = skbn->data[17];
					if (ns == sk->nr->vr) {
						if (nr_queue_rx_frame(sk, skbn, frametype & NR_MORE_FLAG) == 0) {
							sk->nr->vr = (sk->nr->vr + 1) % NR_MODULUS;
						} else {
							sk->nr->condition |= OWN_RX_BUSY_CONDITION;
							skb_queue_tail(&temp_queue, skbn);
						}
					} else if (nr_in_rx_window(sk, ns)) {
						skb_queue_tail(&temp_queue, skbn);
					} else {
						skbn->free = 1;
						kfree_skb(skbn, FREE_READ);
					}
				}
				while ((skbn = skb_dequeue(&temp_queue)) != NULL) {
					skb_queue_tail(&sk->nr->reseq_queue, skbn);
				}
			} while (save_vr != sk->nr->vr);
			/*
			 * Window is full, ack it immediately.
			 */
			if (((sk->nr->vl + sk->window) % NR_MODULUS) == sk->nr->vr) {
				nr_enquiry_response(sk);
			} else {
				if (!(sk->nr->condition & ACK_PENDING_CONDITION)) {
					sk->nr->t2timer = sk->nr->t2;
					sk->nr->condition |= ACK_PENDING_CONDITION;
				}
			}
			break;

		default:
			break;
	}

	return queued;
}

/* Higher level upcall for a LAPB frame */
int nr_process_rx_frame(struct sock *sk, struct sk_buff *skb)
{
	int queued = 0, frametype;
	
	if (sk->nr->state == NR_STATE_0 && sk->dead)
		return queued;

	if (sk->nr->state != NR_STATE_1 && sk->nr->state != NR_STATE_2 &&
	    sk->nr->state != NR_STATE_3) {
		printk(KERN_ERR "nr_process_rx_frame: frame received - state: %d\n", sk->nr->state);
		return queued;
	}

	del_timer(&sk->timer);

	frametype = skb->data[19];

	switch (sk->nr->state)
	{
		case NR_STATE_1:
			queued = nr_state1_machine(sk, skb, frametype);
			break;
		case NR_STATE_2:
			queued = nr_state2_machine(sk, skb, frametype);
			break;
		case NR_STATE_3:
			queued = nr_state3_machine(sk, skb, frametype);
			break;
	}

	nr_set_timer(sk);

	return queued;
}

#endif

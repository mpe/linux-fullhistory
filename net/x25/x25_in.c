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
#include <net/ip.h>			/* For ip_rcv */
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/x25.h>

static int x25_queue_rx_frame(struct sock *sk, struct sk_buff *skb, int more)
{
	struct sk_buff *skbo, *skbn = skb;

	if (more) {
		sk->protinfo.x25->fraglen += skb->len;
		skb_queue_tail(&sk->protinfo.x25->fragment_queue, skb);
		return 0;
	}

	if (!more && sk->protinfo.x25->fraglen > 0) {	/* End of fragment */
		sk->protinfo.x25->fraglen += skb->len;
		skb_queue_tail(&sk->protinfo.x25->fragment_queue, skb);

		if ((skbn = alloc_skb(sk->protinfo.x25->fraglen, GFP_ATOMIC)) == NULL)
			return 1;

		skb_set_owner_r(skbn, sk);
		skbn->h.raw = skbn->data;

		skbo = skb_dequeue(&sk->protinfo.x25->fragment_queue);
		memcpy(skb_put(skbn, skbo->len), skbo->data, skbo->len);
		kfree_skb(skbo, FREE_READ);

		while ((skbo = skb_dequeue(&sk->protinfo.x25->fragment_queue)) != NULL) {
			skb_pull(skbo, (sk->protinfo.x25->neighbour->extended) ? X25_EXT_MIN_LEN : X25_STD_MIN_LEN);
			memcpy(skb_put(skbn, skbo->len), skbo->data, skbo->len);
			kfree_skb(skbo, FREE_READ);
		}

		sk->protinfo.x25->fraglen = 0;		
	}

	return sock_queue_rcv_skb(sk, skbn);
}

/*
 * State machine for state 1, Awaiting Call Accepted State.
 * The handling of the timer(s) is in file x25_timer.c.
 * Handling of state 0 and connection release is in af_x25.c.
 */
static int x25_state1_machine(struct sock *sk, struct sk_buff *skb, int frametype)
{
	x25_address source_addr, dest_addr;
	struct x25_facilities facilities;

	switch (frametype) {

		case X25_CALL_ACCEPTED:
			sk->protinfo.x25->condition = 0x00;
			sk->protinfo.x25->timer     = 0;
			sk->protinfo.x25->vs        = 0;
			sk->protinfo.x25->va        = 0;
			sk->protinfo.x25->vr        = 0;
			sk->protinfo.x25->vl        = 0;
			sk->protinfo.x25->state     = X25_STATE_3;
			sk->state                   = TCP_ESTABLISHED;
			/*
			 *	Parse the data in the frame.
			 */
			skb_pull(skb, X25_STD_MIN_LEN);
			skb_pull(skb, x25_addr_ntoa(skb->data, &source_addr, &dest_addr));
			skb_pull(skb, x25_parse_facilities(skb, &facilities));
			/*
			 *	Facilities XXX
			 *	Copy any Call User Data.
			 */
			if (skb->len >= 0) {
				memcpy(sk->protinfo.x25->calluserdata.cuddata, skb->data, skb->len);
				sk->protinfo.x25->calluserdata.cudlength = skb->len;
			}
			if (!sk->dead)
				sk->state_change(sk);
			break;

		case X25_CLEAR_REQUEST:
			x25_clear_queues(sk);
			sk->protinfo.x25->state = X25_STATE_0;
			sk->state               = TCP_CLOSE;
			sk->err                 = ECONNREFUSED;
			sk->shutdown           |= SEND_SHUTDOWN;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead                = 1;
			break;

		default:
			printk(KERN_WARNING "x25: unknown %02X in state 1\n", frametype);
			break;
	}

	return 0;
}

/*
 * State machine for state 2, Awaiting Clear Confirmation State.
 * The handling of the timer(s) is in file x25_timer.c
 * Handling of state 0 and connection release is in af_x25.c.
 */
static int x25_state2_machine(struct sock *sk, struct sk_buff *skb, int frametype)
{
	switch (frametype) {

		case X25_CLEAR_REQUEST:
		case X25_CLEAR_CONFIRMATION:
			sk->protinfo.x25->state = X25_STATE_0;
			sk->state               = TCP_CLOSE;
			sk->err                 = 0;
			sk->shutdown           |= SEND_SHUTDOWN;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead                = 1;
			break;

		default:
			printk(KERN_WARNING "x25: unknown %02X in state 2\n", frametype);
			break;
	}

	return 0;
}

/*
 * State machine for state 3, Connected State.
 * The handling of the timer(s) is in file x25_timer.c
 * Handling of state 0 and connection release is in af_x25.c.
 */
static int x25_state3_machine(struct sock *sk, struct sk_buff *skb, int frametype, int ns, int nr, int q, int d, int m)
{
	int queued = 0;
	int modulus;
	
	modulus = (sk->protinfo.x25->neighbour->extended) ? X25_EMODULUS : X25_SMODULUS;

	switch (frametype) {

		case X25_RESET_REQUEST:
			x25_clear_queues(sk);
			x25_write_internal(sk, X25_RESET_CONFIRMATION);
			sk->protinfo.x25->condition = 0x00;
			sk->protinfo.x25->timer     = 0;
			sk->protinfo.x25->vs        = 0;
			sk->protinfo.x25->vr        = 0;
			sk->protinfo.x25->va        = 0;
			sk->protinfo.x25->vl        = 0;
			break;

		case X25_CLEAR_REQUEST:
			x25_clear_queues(sk);
			x25_write_internal(sk, X25_CLEAR_CONFIRMATION);
			sk->protinfo.x25->state = X25_STATE_0;
			sk->state               = TCP_CLOSE;
			sk->err                 = 0;
			sk->shutdown           |= SEND_SHUTDOWN;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead                = 1;
			break;

		case X25_RR:
		case X25_RNR:
			if (frametype == X25_RNR) {
				sk->protinfo.x25->condition |= X25_COND_PEER_RX_BUSY;
			} else {
				sk->protinfo.x25->condition &= ~X25_COND_PEER_RX_BUSY;
			}
			if (!x25_validate_nr(sk, nr)) {
				x25_clear_queues(sk);
				x25_write_internal(sk, X25_RESET_REQUEST);
				sk->protinfo.x25->condition = 0x00;
				sk->protinfo.x25->vs        = 0;
				sk->protinfo.x25->vr        = 0;
				sk->protinfo.x25->va        = 0;
				sk->protinfo.x25->vl        = 0;
				sk->protinfo.x25->state     = X25_STATE_4;
				sk->protinfo.x25->timer     = sk->protinfo.x25->t22;
			} else {
				if (sk->protinfo.x25->condition & X25_COND_PEER_RX_BUSY) {
					sk->protinfo.x25->va = nr;
				} else {
					x25_check_iframes_acked(sk, nr);
				}
			}
			break;

		case X25_DATA:	/* XXX */
			sk->protinfo.x25->condition &= ~X25_COND_PEER_RX_BUSY;
			if (!x25_validate_nr(sk, nr)) {
				x25_clear_queues(sk);
				x25_write_internal(sk, X25_RESET_REQUEST);
				sk->protinfo.x25->condition = 0x00;
				sk->protinfo.x25->vs        = 0;
				sk->protinfo.x25->vr        = 0;
				sk->protinfo.x25->va        = 0;
				sk->protinfo.x25->vl        = 0;
				sk->protinfo.x25->state     = X25_STATE_4;
				sk->protinfo.x25->timer     = sk->protinfo.x25->t22;
				break;
			}
			if (sk->protinfo.x25->condition & X25_COND_PEER_RX_BUSY) {
				sk->protinfo.x25->va = nr;
			} else {
				x25_check_iframes_acked(sk, nr);
			}
			if (sk->protinfo.x25->condition & X25_COND_OWN_RX_BUSY)
				break;
			if (ns == sk->protinfo.x25->vr) {
				if (x25_queue_rx_frame(sk, skb, m) == 0) {
					sk->protinfo.x25->vr = (sk->protinfo.x25->vr + 1) % modulus;
					queued = 1;
				} else {
					sk->protinfo.x25->condition |= X25_COND_OWN_RX_BUSY;
				}
			}
			/*
			 *	If the window is full Ack it immediately, else
			 *	start the holdback timer.
			 */
			if (((sk->protinfo.x25->vl + sk->protinfo.x25->facilities.winsize_in) % modulus) == sk->protinfo.x25->vr) {
				sk->protinfo.x25->condition &= ~X25_COND_ACK_PENDING;
				sk->protinfo.x25->timer      = 0;
				x25_enquiry_response(sk);
			} else {
				sk->protinfo.x25->condition |= X25_COND_ACK_PENDING;
				sk->protinfo.x25->timer      = sk->protinfo.x25->t2;
			}
			break;

		case X25_INTERRUPT_CONFIRMATION:
			sk->protinfo.x25->intflag = 0;
			break;

		case X25_INTERRUPT:
			if (sk->urginline) {
				queued = (sock_queue_rcv_skb(sk, skb) == 0);
			} else {
				skb_set_owner_r(skb, sk);
				skb_queue_tail(&sk->protinfo.x25->interrupt_in_queue, skb);
				queued = 1;
			}
			if (sk->proc != 0) {
				if (sk->proc > 0)
					kill_proc(sk->proc, SIGURG, 1);
				else
					kill_pg(-sk->proc, SIGURG, 1);
			}
			x25_write_internal(sk, X25_INTERRUPT_CONFIRMATION);
			break;

		default:
			printk(KERN_WARNING "x25: unknown %02X in state 3\n", frametype);
			break;
	}

	return queued;
}

/*
 * State machine for state 4, Awaiting Reset Confirmation State.
 * The handling of the timer(s) is in file x25_timer.c
 * Handling of state 0 and connection release is in af_x25.c.
 */
static int x25_state4_machine(struct sock *sk, struct sk_buff *skb, int frametype)
{
	switch (frametype) {

		case X25_RESET_CONFIRMATION:
		case X25_RESET_REQUEST:
			sk->protinfo.x25->timer     = 0;
			sk->protinfo.x25->condition = 0x00;
			sk->protinfo.x25->va        = 0;
			sk->protinfo.x25->vr        = 0;
			sk->protinfo.x25->vs        = 0;
			sk->protinfo.x25->vl        = 0;
			sk->protinfo.x25->state     = X25_STATE_3;
			break;

		case X25_CLEAR_REQUEST:
			x25_clear_queues(sk);
			x25_write_internal(sk, X25_CLEAR_CONFIRMATION);
			sk->protinfo.x25->timer = 0;
			sk->protinfo.x25->state = X25_STATE_0;
			sk->state               = TCP_CLOSE;
			sk->err                 = 0;
			sk->shutdown           |= SEND_SHUTDOWN;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead                = 1;
			break;

		default:
			printk(KERN_WARNING "x25: unknown %02X in state 4\n", frametype);
			break;
	}

	return 0;
}

/* Higher level upcall for a LAPB frame */
int x25_process_rx_frame(struct sock *sk, struct sk_buff *skb)
{
	int queued = 0, frametype, ns, nr, q, d, m;

	if (sk->protinfo.x25->state == X25_STATE_0)
		return 0;

	del_timer(&sk->timer);

	frametype = x25_decode(sk, skb, &ns, &nr, &q, &d, &m);

	switch (sk->protinfo.x25->state) {
		case X25_STATE_1:
			queued = x25_state1_machine(sk, skb, frametype);
			break;
		case X25_STATE_2:
			queued = x25_state2_machine(sk, skb, frametype);
			break;
		case X25_STATE_3:
			queued = x25_state3_machine(sk, skb, frametype, ns, nr, q, d, m);
			break;
		case X25_STATE_4:
			queued = x25_state4_machine(sk, skb, frametype);
			break;
	}

	x25_set_timer(sk);

	return queued;
}

#endif

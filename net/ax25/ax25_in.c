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
 *	AX.25 028b	Jonathan(G4KLX) Extracted AX25 control block from
 *					the sock structure.
 *	AX.25 029	Alan(GW4PTS)	Switched to KA9Q constant names.
 *			Jonathan(G4KLX)	Added IP mode registration.
 *	AX.25 030	Jonathan(G4KLX)	Added AX.25 fragment reception.
 *					Upgraded state machine for SABME.
 *					Added arbitrary protocol id support.
 *	AX.25 031	Joerg(DL1BKE)	Added DAMA support
 *			HaJo(DD8NE)	Added Idle Disc Timer T5
 *			Joerg(DL1BKE)   Renamed it to "IDLE" with a slightly
 *					different behaviour. Fixed defrag
 *					routine (I hope)
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
#include <net/ip.h>			/* For ip_rcv */
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#ifdef CONFIG_NETROM
#include <net/netrom.h>
#endif

static int ax25_rx_iframe(ax25_cb *, struct sk_buff *);

/*
 *	Given a fragment, queue it on the fragment queue and if the fragment
 *	is complete, send it back to ax25_rx_iframe.
 */
static int ax25_rx_fragment(ax25_cb *ax25, struct sk_buff *skb)
{
	struct sk_buff *skbn, *skbo;
	int hdrlen;
	
	if (ax25->fragno != 0) {
		if (!(*skb->data & SEG_FIRST)) {
			if ((ax25->fragno - 1) == (*skb->data & SEG_REM)) {
			
				/* enqueue fragment */
				
				ax25->fragno = *skb->data & SEG_REM;
				skb_pull(skb, 1);	/* skip fragno */
				ax25->fraglen += skb->len;
				skb_queue_tail(&ax25->frag_queue, skb);
				
				/* last fragment received? */

				if (ax25->fragno == 0) {
					if ((skbn = alloc_skb(AX25_MAX_HEADER_LEN + ax25->fraglen, GFP_ATOMIC)) == NULL)
						return 0;

					skbn->free = 1;
					skbn->arp  = 1;
					skbn->dev  = skb->dev;

					if (ax25->sk != NULL) {
						skbn->sk = ax25->sk;
						atomic_add(skbn->truesize, &ax25->sk->rmem_alloc);
					}

					/* get first fragment from queue */
					
					skbo = skb_dequeue(&ax25->frag_queue);
					hdrlen = skbo->data - skbo->h.raw - 2;	/* skip PID & fragno */
					
					skb_push(skbo, hdrlen + 2);		/* start of address field */
					skbn->data = skb_put(skbn, hdrlen);	/* get space for info */
					memcpy(skbn->data, skbo->data, hdrlen);	/* copy address field */
					skb_pull(skbo, hdrlen + 2);		/* start of data */
					skb_pull(skbn, hdrlen + 1);		/* ditto */

					/* copy data from first fragment */

					memcpy(skb_put(skbn, skbo->len), skbo->data, skbo->len);
					kfree_skb(skbo, FREE_READ);
					
					/* add other fragment's data */

					while ((skbo = skb_dequeue(&ax25->frag_queue)) != NULL) {
						memcpy(skb_put(skbn, skbo->len), skbo->data, skbo->len);
						kfree_skb(skbo, FREE_READ);
					}

					ax25->fraglen = 0;		/* reset counter */
					
					/* 
					 * mysteriously we need to re-adjust skb->data.
					 * Anyway, it seems to work. Do we have the address fields
					 * encoded TWICE in one sk_buff?
					 */
					
					skb_pull(skbn, hdrlen);

					if (ax25_rx_iframe(ax25, skbn) == 0)
						kfree_skb(skbn, FREE_READ);
				}
				
				return 1;
			}
		}
	} else {
		/* first fragment received? */

		if (*skb->data & SEG_FIRST) {
			ax25->fragno = *skb->data & SEG_REM;
			skb_pull(skb, 1);		/* skip fragno */
			ax25->fraglen = skb->len;
			skb_queue_tail(&ax25->frag_queue, skb);
			return 1;
		}
	}

	return 0;
}

/*
 *	This is where all valid I frames are sent to, to be dispatched to
 *	whichever protocol requires them.
 */
static int ax25_rx_iframe(ax25_cb *ax25, struct sk_buff *skb)
{
	volatile int queued = 0;
	unsigned char pid;
	
	if (skb == NULL) return 0;

	ax25->idletimer = ax25->idle;
	
	pid = *skb->data;

	switch (pid) {
#ifdef CONFIG_NETROM
		case AX25_P_NETROM:
			if (ax25_dev_get_value(ax25->device, AX25_VALUES_NETROM)) {
				skb_pull(skb, 1);	/* Remove PID */
				queued = nr_route_frame(skb, ax25);
			}
			break;
#endif
#ifdef CONFIG_INET
		case AX25_P_IP:
			skb_pull(skb, 1);	/* Remove PID */
			skb->h.raw = skb->data;
			ax25_ip_mode_set(&ax25->dest_addr, ax25->device, 'V');
			ip_rcv(skb, ax25->device, NULL);	/* Wrong ptype */
			queued = 1;
			break;
#endif
		case AX25_P_SEGMENT:
			skb_pull(skb, 1);	/* Remove PID */
			queued = ax25_rx_fragment(ax25, skb);
			break;

		default:
			if (ax25->sk != NULL && ax25_dev_get_value(ax25->device, AX25_VALUES_TEXT) && ax25->sk->protocol == pid) {
				if (sock_queue_rcv_skb(ax25->sk, skb) == 0) {
					queued = 1;
				} else {
					ax25->condition |= OWN_RX_BUSY_CONDITION;
				}
			}
			break;
	}

	return queued;
}

/*
 *	State machine for state 1, Awaiting Connection State.
 *	The handling of the timer(s) is in file ax25_timer.c.
 *	Handling of state 0 and connection release is in ax25.c.
 */
static int ax25_state1_machine(ax25_cb *ax25, struct sk_buff *skb, int frametype, int pf, int type, int dama)
{
	switch (frametype) {
		case SABM:
			ax25->modulus = MODULUS;
			ax25->window  = ax25_dev_get_value(ax25->device, AX25_VALUES_WINDOW);
			ax25_send_control(ax25, UA, pf, C_RESPONSE);
			break;

		case SABME:
			ax25->modulus = EMODULUS;
			ax25->window  = ax25_dev_get_value(ax25->device, AX25_VALUES_EWINDOW);
			ax25_send_control(ax25, UA, pf, C_RESPONSE);
			break;

		case DISC:
			ax25_send_control(ax25, DM, pf, C_RESPONSE);
			break;

		case UA:
			if (pf || dama) {
				if (dama) ax25_dama_on(ax25); /* bke */
					
				ax25_calculate_rtt(ax25);
				ax25->t1timer = 0;
				ax25->t3timer = ax25->t3;
				ax25->idletimer = ax25->idle;
				ax25->vs      = 0;
				ax25->va      = 0;
				ax25->vr      = 0;
				ax25->state   = AX25_STATE_3;
				ax25->n2count = 0;
				ax25->dama_slave = dama;	/* bke */
					
				if (ax25->sk != NULL) {
					ax25->sk->state = TCP_ESTABLISHED;
					/* For WAIT_SABM connections we will produce an accept ready socket here */
					if (!ax25->sk->dead)
						ax25->sk->state_change(ax25->sk);
				}
			}
			break;

		case DM:
			if (pf) {
				if (ax25->modulus == MODULUS) {
					ax25_clear_queues(ax25);
					ax25->state = AX25_STATE_0;
					if (ax25->sk != NULL) {
						ax25->sk->state = TCP_CLOSE;
						ax25->sk->err   = ECONNREFUSED;
						if (!ax25->sk->dead)
							ax25->sk->state_change(ax25->sk);
						ax25->sk->dead  = 1;
					}
				} else {
					ax25->modulus = MODULUS;
					ax25->window  = ax25_dev_get_value(ax25->device, AX25_VALUES_WINDOW);
				}
			}
			break;

		default:
			if (dama && pf)	/* dl1bke 960116 */
				ax25_send_control(ax25, SABM, POLLON, C_COMMAND);
			break;
	}

	return 0;
}

/*
 *	State machine for state 2, Awaiting Release State.
 *	The handling of the timer(s) is in file ax25_timer.c
 *	Handling of state 0 and connection release is in ax25.c.
 */
static int ax25_state2_machine(ax25_cb *ax25, struct sk_buff *skb, int frametype, int pf, int type)
{
	switch (frametype) {
		case SABM:
		case SABME:
			ax25_send_control(ax25, DM, pf, C_RESPONSE);
			if (ax25->dama_slave)
				ax25_send_control(ax25, DISC, POLLON, C_COMMAND);
			break;

		case DISC:
			ax25_send_control(ax25, UA, pf, C_RESPONSE);
			if (ax25->dama_slave) {
				ax25->state = AX25_STATE_0;
				ax25_dama_off(ax25);

				if (ax25->sk != NULL) {
					ax25->sk->state = TCP_CLOSE;
					ax25->sk->err   = 0;
					if (!ax25->sk->dead)
						ax25->sk->state_change(ax25->sk);
					ax25->sk->dead  = 1;
				}
			}
			break;

		case UA:
			if (pf) {
				ax25->state = AX25_STATE_0;
				ax25_dama_off(ax25);

				if (ax25->sk != NULL) {
					ax25->sk->state = TCP_CLOSE;
					ax25->sk->err   = 0;
					if (!ax25->sk->dead)
						ax25->sk->state_change(ax25->sk);
					ax25->sk->dead  = 1;
				}
			}
			break;

		case DM:
			if (pf) {
				ax25->state = AX25_STATE_0;
				ax25_dama_off(ax25);
					
				if (ax25->sk != NULL) {
					ax25->sk->state = TCP_CLOSE;
					ax25->sk->err   = 0;
					if (!ax25->sk->dead)
						ax25->sk->state_change(ax25->sk);
					ax25->sk->dead  = 1;
				}
			}
			break;

		case I:
		case REJ:
		case RNR:
		case RR:
			if (pf) {
				if (ax25->dama_slave)
					ax25_send_control(ax25, DISC, POLLON, C_COMMAND);
				else
					ax25_send_control(ax25, DM, POLLON, C_RESPONSE);
			}
			break;
				
		default:
			break;
	}

	return 0;
}

/*
 *	State machine for state 3, Connected State.
 *	The handling of the timer(s) is in file ax25_timer.c
 *	Handling of state 0 and connection release is in ax25.c.
 */
static int ax25_state3_machine(ax25_cb *ax25, struct sk_buff *skb, int frametype, int ns, int nr, int pf, int type, int dama)
{
	int queued = 0;

	switch (frametype) {
		case SABM:
			if (dama) ax25_dama_on(ax25);
				
			ax25->modulus   = MODULUS;
			ax25->window    = ax25_dev_get_value(ax25->device, AX25_VALUES_WINDOW);
			ax25_send_control(ax25, UA, pf, C_RESPONSE);
			ax25->condition = 0x00;
			ax25->t1timer   = 0;
			ax25->t3timer   = ax25->t3;
			ax25->idletimer = ax25->idle;
			ax25->vs        = 0;
			ax25->va        = 0;
			ax25->vr        = 0;
			ax25->dama_slave = dama;
			break;

		case SABME:
			if (dama) ax25_dama_on(ax25);
				
			ax25->modulus   = EMODULUS;
			ax25->window    = ax25_dev_get_value(ax25->device, AX25_VALUES_EWINDOW);
			ax25_send_control(ax25, UA, pf, C_RESPONSE);
			ax25->condition = 0x00;
			ax25->t1timer   = 0;
			ax25->t3timer   = ax25->t3;
			ax25->idletimer = ax25->idle;
			ax25->vs        = 0;
			ax25->va        = 0;
			ax25->vr        = 0;
			ax25->dama_slave = dama;
			break;

		case DISC:
			ax25_clear_queues(ax25);
			ax25_send_control(ax25, UA, pf, C_RESPONSE);
			ax25->t3timer = 0;
			ax25->state   = AX25_STATE_0;
			ax25_dama_off(ax25);
			
			if (ax25->sk != NULL) {
				ax25->sk->state = TCP_CLOSE;
				ax25->sk->err   = 0;
				if (!ax25->sk->dead)
					ax25->sk->state_change(ax25->sk);
				ax25->sk->dead  = 1;
			}
			break;

		case DM:
			ax25_clear_queues(ax25);
			ax25->t3timer = 0;
			ax25->state   = AX25_STATE_0;
			ax25_dama_off(ax25);
			if (ax25->sk) {
				ax25->sk->state = TCP_CLOSE;
				ax25->sk->err   = ECONNRESET;
				if (!ax25->sk->dead)
					ax25->sk->state_change(ax25->sk);
				ax25->sk->dead         = 1;
			}
			break;

		case RNR:
			ax25->condition |= PEER_RX_BUSY_CONDITION;
			ax25_check_need_response(ax25, type, pf);
			if (ax25_validate_nr(ax25, nr)) {
				ax25_check_iframes_acked(ax25, nr);
				dama_check_need_response(ax25, type, pf);
			} else {
				ax25_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;
			
		case RR:
			ax25->condition &= ~PEER_RX_BUSY_CONDITION;
			ax25_check_need_response(ax25, type, pf);
			if (ax25_validate_nr(ax25, nr)) {
				ax25_check_iframes_acked(ax25, nr);
				dama_check_need_response(ax25, type, pf);
			} else {
				ax25_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;
				
		case REJ:
			ax25->condition &= ~PEER_RX_BUSY_CONDITION;
			ax25_check_need_response(ax25, type, pf);
			if (ax25_validate_nr(ax25, nr)) {
				ax25_frames_acked(ax25, nr);
				ax25_calculate_rtt(ax25);
				ax25->t1timer = 0;
				ax25->t3timer = ax25->t3;
				ax25_requeue_frames(ax25);
				dama_check_need_response(ax25, type, pf);
			} else {
				ax25_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;
			
		case I:
#ifndef AX25_BROKEN_NETMAC
			if (type != C_COMMAND)
				break;
#endif
			if (!ax25_validate_nr(ax25, nr)) {
				ax25_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
				break;
			}
			if (ax25->condition & PEER_RX_BUSY_CONDITION) {
				ax25_frames_acked(ax25, nr);
			} else {
				ax25_check_iframes_acked(ax25, nr);
			}
			if (ax25->condition & OWN_RX_BUSY_CONDITION) {
				if (pf)	{
					if (ax25->dama_slave)	/* dl1bke 960114 */
						dama_enquiry_response(ax25);
					else
						ax25_enquiry_response(ax25);
				}
				break;
			}
			if (ns == ax25->vr) {
				queued = ax25_rx_iframe(ax25, skb);
				if (ax25->condition & OWN_RX_BUSY_CONDITION) {
					if (pf) {
						if (ax25->dama_slave)	/* dl1bke 960114 */
							dama_enquiry_response(ax25);
						else
							ax25_enquiry_response(ax25);
					}
					break;
				}
				ax25->vr = (ax25->vr + 1) % ax25->modulus;
				ax25->condition &= ~REJECT_CONDITION;
				if (pf) {
					if (ax25->dama_slave)	/* dl1bke 960114 */
						dama_enquiry_response(ax25);
					else
						ax25_enquiry_response(ax25);
				} else {
					if (!(ax25->condition & ACK_PENDING_CONDITION)) {
						ax25->t2timer = ax25->t2;
						ax25->condition |= ACK_PENDING_CONDITION;
					}
				}
			} else {
				if (ax25->condition & REJECT_CONDITION) {
					if (pf) {
						if (ax25->dama_slave)	/* dl1bke 960114 */
							dama_enquiry_response(ax25);
						else
							ax25_enquiry_response(ax25);
					}
				} else {
					ax25->condition |= REJECT_CONDITION;
					if (ax25->dama_slave)		/* dl1bke 960114 */
						dama_enquiry_response(ax25);
					else
						ax25_send_control(ax25, REJ, pf, C_RESPONSE);
					ax25->condition &= ~ACK_PENDING_CONDITION;
				}
			}
			break;

		case FRMR:
		case ILLEGAL:
			ax25_establish_data_link(ax25);
			ax25->state = AX25_STATE_1;
			break;

		default:
			break;
	}

	return queued;
}

/*
 *	State machine for state 4, Timer Recovery State.
 *	The handling of the timer(s) is in file ax25_timer.c
 *	Handling of state 0 and connection release is in ax25.c.
 */
static int ax25_state4_machine(ax25_cb *ax25, struct sk_buff *skb, int frametype, int ns, int nr, int pf, int type, int dama)
{
	int queued = 0;

	switch (frametype) {
		case SABM:
			if (dama) ax25_dama_on(ax25);
				
			ax25->dama_slave = dama;
			ax25->modulus   = MODULUS;
			ax25->window    = ax25_dev_get_value(ax25->device, AX25_VALUES_WINDOW);
			ax25_send_control(ax25, UA, pf, C_RESPONSE);
			ax25->condition = 0x00;
			ax25->t1timer   = 0;
			ax25->t3timer   = ax25->t3;
			ax25->idletimer = ax25->idle;
			ax25->vs        = 0;
			ax25->va        = 0;
			ax25->vr        = 0;
			ax25->state     = AX25_STATE_3;
			ax25->n2count   = 0;
			break;

		case SABME:
			if (dama) ax25_dama_on(ax25);
				
			ax25->dama_slave = dama;
			ax25->modulus   = EMODULUS;
			ax25->window    = ax25_dev_get_value(ax25->device, AX25_VALUES_EWINDOW);
			ax25_send_control(ax25, UA, pf, C_RESPONSE);
			ax25->condition = 0x00;
			ax25->t1timer   = 0;
			ax25->t3timer   = ax25->t3;
			ax25->idletimer = ax25->idle;
			ax25->vs        = 0;
			ax25->va        = 0;
			ax25->vr        = 0;
			ax25->state     = AX25_STATE_3;
			ax25->n2count   = 0;
			break;

		case DISC:
			ax25_clear_queues(ax25);
			ax25_send_control(ax25, UA, pf, C_RESPONSE);
			ax25->t3timer = 0;
			ax25->state   = AX25_STATE_0;
			ax25_dama_off(ax25);
			
			if (ax25->sk != NULL) {
				ax25->sk->state = TCP_CLOSE;
				ax25->sk->err   = 0;
				if (!ax25->sk->dead)
					ax25->sk->state_change(ax25->sk);
				ax25->sk->dead  = 1;
			}
			break;

		case DM:
			ax25_clear_queues(ax25);
			ax25->t3timer = 0;
			ax25->state   = AX25_STATE_0;
			ax25_dama_off(ax25);
			
			if (ax25->sk != NULL) {
				ax25->sk->state = TCP_CLOSE;
				ax25->sk->err   = ECONNRESET;
				if (!ax25->sk->dead)
					ax25->sk->state_change(ax25->sk);
				ax25->sk->dead  = 1;
			}
			break;

		case RNR:
			ax25->condition |= PEER_RX_BUSY_CONDITION;
			if (type == C_RESPONSE && pf) {
				ax25->t1timer = 0;
				if (ax25_validate_nr(ax25, nr)) {
					ax25_frames_acked(ax25, nr);
					if (ax25->vs == ax25->va) {
						ax25->t3timer = ax25->t3;
						ax25->n2count = 0;
						ax25->state   = AX25_STATE_3;
					}
				} else {
					ax25_nr_error_recovery(ax25);
					ax25->state = AX25_STATE_1;
				}
				break;
			}
			 
			ax25_check_need_response(ax25, type, pf);
			if (ax25_validate_nr(ax25, nr)) {
				ax25_frames_acked(ax25, nr);
				dama_check_need_response(ax25, type, pf);
			} else {
				ax25_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;
			
		case RR:
			ax25->condition &= ~PEER_RX_BUSY_CONDITION;
			if ( pf && (type == C_RESPONSE || (ax25->dama_slave && type == C_COMMAND)) ) {
				ax25->t1timer = 0;
				if (ax25_validate_nr(ax25, nr)) {
					ax25_frames_acked(ax25, nr);
					if (ax25->vs == ax25->va) {
						ax25->t3timer = ax25->t3;
						ax25->n2count = 0;
						ax25->state   = AX25_STATE_3;
					} else {
						ax25_requeue_frames(ax25);
					}
					dama_check_need_response(ax25, type, pf);
				} else {
					ax25_nr_error_recovery(ax25);
					ax25->state = AX25_STATE_1;
				}
				break;
			}

			ax25_check_need_response(ax25, type, pf);
			if (ax25_validate_nr(ax25, nr)) {
				ax25_frames_acked(ax25, nr);
				dama_check_need_response(ax25, type, pf);
			} else {
				ax25_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;

		case REJ:
			ax25->condition &= ~PEER_RX_BUSY_CONDITION;
			if ( pf && (type == C_RESPONSE || (ax25->dama_slave && type == C_COMMAND)) ) {
				ax25->t1timer = 0;
				if (ax25_validate_nr(ax25, nr)) {
					ax25_frames_acked(ax25, nr);
					if (ax25->vs == ax25->va) {
						ax25->t3timer = ax25->t3;
						ax25->n2count = 0;
						ax25->state   = AX25_STATE_3;
					} else {
						ax25_requeue_frames(ax25);
					}
					dama_check_need_response(ax25, type, pf);
				} else {
					ax25_nr_error_recovery(ax25);
					ax25->state = AX25_STATE_1;
				}
				break;
			}
			
			ax25_check_need_response(ax25, type, pf);	
			if (ax25_validate_nr(ax25, nr)) {
				ax25_frames_acked(ax25, nr);
				if(ax25->vs != ax25->va) {
					ax25_requeue_frames(ax25);
				}
				dama_check_need_response(ax25, type, pf);
			} else {
				ax25_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;

		case I:
#ifndef	AX25_BROKEN_NETMAC
			if (type != C_COMMAND)
				break;
#endif
			if (!ax25_validate_nr(ax25, nr)) {
				ax25_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
				break;
			}
			ax25_frames_acked(ax25, nr);
			if (ax25->condition & OWN_RX_BUSY_CONDITION) {
				if (pf) {	/* dl1bke 960114 */
					if (ax25->dama_slave)
						ax25_enquiry_response(ax25);
					else
						dama_enquiry_response(ax25);
				}
				break;
			}
			if (ns == ax25->vr) {
				queued = ax25_rx_iframe(ax25, skb);
				if (ax25->condition & OWN_RX_BUSY_CONDITION) {
					if (pf) {	/* dl1bke 960114 */
						if (ax25->dama_slave)
							dama_enquiry_response(ax25);
						else
							ax25_enquiry_response(ax25);
					}
					break;
				}
				ax25->vr = (ax25->vr + 1) % ax25->modulus;
				ax25->condition &= ~REJECT_CONDITION;
				if (pf) {
					if (ax25->dama_slave) 	/* dl1bke 960114 */
						dama_enquiry_response(ax25);
					else
						ax25_enquiry_response(ax25);
				} else {
					if (!(ax25->condition & ACK_PENDING_CONDITION)) {
						ax25->t2timer = ax25->t2;
						ax25->condition |= ACK_PENDING_CONDITION;
					}
				}
			} else {
				if (ax25->condition & REJECT_CONDITION) {
					if (pf) { 	/* dl1bke 960114 */
						if (ax25->dama_slave)
							dama_enquiry_response(ax25);
						else
							ax25_enquiry_response(ax25);
					}
				} else {
					ax25->condition |= REJECT_CONDITION;
					if (ax25->dama_slave)		/* dl1bke 960114 */
						dama_enquiry_response(ax25);
					else
						ax25_send_control(ax25, REJ, pf, C_RESPONSE);
					ax25->condition &= ~ACK_PENDING_CONDITION;
				}
			}
			break;
		
		case FRMR:
		case ILLEGAL:
			ax25_establish_data_link(ax25);
			ax25->state = AX25_STATE_1;
			break;

		default:
			break;
	}

	return queued;
}

/*
 *	Higher level upcall for a LAPB frame
 */
int ax25_process_rx_frame(ax25_cb *ax25, struct sk_buff *skb, int type, int dama)
{
	int queued = 0, frametype, ns, nr, pf;
	
	if (ax25->sk != NULL && ax25->state == AX25_STATE_0 && ax25->sk->dead)
		return queued;

	if (ax25->state != AX25_STATE_1 && ax25->state != AX25_STATE_2 &&
	    ax25->state != AX25_STATE_3 && ax25->state != AX25_STATE_4) {
		printk("ax25_process_rx_frame: frame received - state = %d\n", ax25->state);
		return queued;
	}

	del_timer(&ax25->timer);

	frametype = ax25_decode(ax25, skb, &ns, &nr, &pf);

	switch (ax25->state) {
		case AX25_STATE_1:
			queued = ax25_state1_machine(ax25, skb, frametype, pf, type, dama);
			break;
		case AX25_STATE_2:
			queued = ax25_state2_machine(ax25, skb, frametype, pf, type);
			break;
		case AX25_STATE_3:
			queued = ax25_state3_machine(ax25, skb, frametype, ns, nr, pf, type, dama);
			break;
		case AX25_STATE_4:
			queued = ax25_state4_machine(ax25, skb, frametype, ns, nr, pf, type, dama);
			break;
	}

	ax25_set_timer(ax25);

	return queued;
}

#endif

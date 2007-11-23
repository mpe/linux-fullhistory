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
 *	AX.25 028b	Jonathan(G4KLX) Extracted AX25 control block from
 *					the sock structure.
 *	AX.25 029	Alan(GW4PTS)	Switched to KA9Q constant names.
 *			Jonathan(G4KLX)	Added IP mode registration.
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

/*
 *	This is where all valid I frames are sent to, to be dispatched to
 *	whichever protocol requires them.
 */
static int ax25_rx_iframe(ax25_cb *ax25, struct sk_buff *skb, unsigned char *iframe)
{
	int queued = 0;

	switch (iframe[1]) {
#ifdef CONFIG_NETROM
		case AX25_P_NETROM:
			/* We can't handle digipeated NET/ROM frames */
			if (ax25->digipeat == NULL)
				queued = nr_route_frame(skb, ax25->device);
			break;
#endif
#ifdef CONFIG_INET
		case AX25_P_IP:
			ax25_ip_mode_set(&ax25->dest_addr, ax25->device, 'V');
			skb->h.raw = ((char *)(iframe)) + 2;
			skb->len  -= 2;
			ip_rcv(skb, skb->dev, NULL);	/* Wrong ptype */
			queued = 1;
			break;
#endif
		case AX25_P_TEXT:
			if (ax25->sk != NULL) {
				if (sock_queue_rcv_skb(ax25->sk, skb) == 0) {
					queued = 1;
				} else {
					ax25->condition |= OWN_RX_BUSY_CONDITION;
				}
			}
			break;
					
		default:
			break;
	}

	return queued;
}

/*
 *	State machine for state 1, Awaiting Connection State.
 *	The handling of the timer(s) is in file ax25_timer.c.
 *	Handling of state 0 and connection release is in ax25.c.
 */
static int ax25_state1_machine(ax25_cb *ax25, struct sk_buff *skb, unsigned char *frame, int frametype, int type)
{
	int pf = frame[0] & PF;

	switch (frametype) {
		case SABM:
			ax25_send_control(ax25, UA | pf, C_RESPONSE);
			break;

		case DISC:
			ax25_send_control(ax25, DM | pf, C_RESPONSE);
			break;

		case UA:
			if (pf) {
				ax25_calculate_rtt(ax25);
				ax25->t1timer = 0;
				ax25->t3timer = ax25->t3;
				ax25->vs      = 0;
				ax25->va      = 0;
				ax25->vr      = 0;
				ax25->state   = AX25_STATE_3;
				ax25->n2count = 0;
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
				ax25_clear_tx_queue(ax25);
				ax25->state = AX25_STATE_0;
				if (ax25->sk != NULL) {
					ax25->sk->state = TCP_CLOSE;
					ax25->sk->err   = ECONNREFUSED;
					if (!ax25->sk->dead)
						ax25->sk->state_change(ax25->sk);
					ax25->sk->dead  = 1;
				}
			}
			break;

		default:
			break;
	}

	return 0;
}

/*
 *	State machine for state 2, Awaiting Release State.
 *	The handling of the timer(s) is in file ax25_timer.c
 *	Handling of state 0 and connection release is in ax25.c.
 */
static int ax25_state2_machine(ax25_cb *ax25, struct sk_buff *skb, unsigned char *frame, int frametype, int type)
{
	int pf = frame[0] & PF;

	switch (frametype) {
		case SABM:
			ax25_send_control(ax25, DM | pf, C_RESPONSE);
			break;

		case DISC:
			ax25_send_control(ax25, UA | pf, C_RESPONSE);
			break;

		case UA:
			if (pf) {
				ax25->state = AX25_STATE_0;
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
			if (pf)
				ax25_send_control(ax25, DM | PF, C_RESPONSE);
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
static int ax25_state3_machine(ax25_cb *ax25, struct sk_buff *skb, unsigned char *frame, int frametype, int type)
{
	unsigned short nr = (frame[0] >> 5) & 7;
	unsigned short ns = (frame[0] >> 1) & 7;
	int pf = frame[0] & PF;
	int queued = 0;

	switch (frametype) {
		case SABM:
			ax25_send_control(ax25, UA | pf, C_RESPONSE);
			ax25->condition = 0x00;
			ax25->t1timer   = 0;
			ax25->t3timer   = ax25->t3;
			ax25->vs        = 0;
			ax25->va        = 0;
			ax25->vr        = 0;
			break;

		case DISC:
			ax25_clear_tx_queue(ax25);
			ax25_send_control(ax25, UA | pf, C_RESPONSE);
			ax25->t3timer = 0;
			ax25->state   = AX25_STATE_0;
			if (ax25->sk != NULL) {
				ax25->sk->state = TCP_CLOSE;
				ax25->sk->err   = 0;
				if (!ax25->sk->dead)
					ax25->sk->state_change(ax25->sk);
				ax25->sk->dead  = 1;
			}
			break;

		case UA:
			ax25_establish_data_link(ax25);
			ax25->state = AX25_STATE_1;
			break;

		case DM:
			ax25_clear_tx_queue(ax25);
			ax25->t3timer = 0;
			ax25->state   = AX25_STATE_0;
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
			} else {
				ax25_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;
			
		case I:
			if (type != C_COMMAND)
				break;
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
				if (pf) ax25_enquiry_response(ax25);
				break;
			}
			if (ns == ax25->vr) {
				queued = ax25_rx_iframe(ax25, skb, frame);
				if (ax25->condition & OWN_RX_BUSY_CONDITION) {
					if (pf) ax25_enquiry_response(ax25);
					break;
				}
				ax25->vr = (ax25->vr + 1) % MODULUS;
				ax25->condition &= ~REJECT_CONDITION;
				if (pf) {
					ax25_enquiry_response(ax25);
				} else {
					if (!(ax25->condition & ACK_PENDING_CONDITION)) {
						ax25->t2timer = ax25->t2;
						ax25->condition |= ACK_PENDING_CONDITION;
					}
				}
			} else {
				if (ax25->condition & REJECT_CONDITION) {
					if (pf) ax25_enquiry_response(ax25);
				} else {
					ax25->condition |= REJECT_CONDITION;
					ax25_send_control(ax25, REJ | pf, C_RESPONSE);
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
static int ax25_state4_machine(ax25_cb *ax25, struct sk_buff *skb, unsigned char *frame, int frametype, int type)
{
	unsigned short nr = (frame[0] >> 5) & 7;
	unsigned short ns = (frame[0] >> 1) & 7;
	int pf = frame[0] & PF;
	int queued = 0;

	switch (frametype) {
		case SABM:
			ax25_send_control(ax25, UA | pf, C_RESPONSE);
			ax25->condition = 0x00;
			ax25->t1timer   = 0;
			ax25->t3timer   = ax25->t3;
			ax25->vs        = 0;
			ax25->va        = 0;
			ax25->vr        = 0;
			ax25->state     = AX25_STATE_3;
			ax25->n2count   = 0;
			break;

		case DISC:
			ax25_clear_tx_queue(ax25);
			ax25_send_control(ax25, UA | pf, C_RESPONSE);
			ax25->t3timer = 0;
			ax25->state   = AX25_STATE_0;
			if (ax25->sk != NULL) {
				ax25->sk->state = TCP_CLOSE;
				ax25->sk->err   = 0;
				if (!ax25->sk->dead)
					ax25->sk->state_change(ax25->sk);
				ax25->sk->dead  = 1;
			}
			break;

		case UA:
			ax25_establish_data_link(ax25);
			ax25->state = AX25_STATE_1;
			break;

		case DM:
			ax25_clear_tx_queue(ax25);
			ax25->t3timer = 0;
			ax25->state   = AX25_STATE_0;
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
			if (type == C_COMMAND && pf)
				ax25_enquiry_response(ax25);
			if (ax25_validate_nr(ax25, nr)) {
				ax25_frames_acked(ax25, nr);
			} else {
				ax25_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;
			
		case RR:
			ax25->condition &= ~PEER_RX_BUSY_CONDITION;
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
			if (type == C_COMMAND && pf)
				ax25_enquiry_response(ax25);
			if (ax25_validate_nr(ax25, nr)) {
				ax25_frames_acked(ax25, nr);
			} else {
				ax25_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;

		case REJ:
			ax25->condition &= ~PEER_RX_BUSY_CONDITION;
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
			if (type == C_COMMAND && pf)
				ax25_enquiry_response(ax25);
			if (ax25_validate_nr(ax25, nr)) {
				ax25_frames_acked(ax25, nr);
			} else {
				ax25_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;

		case I:
			if (type != C_COMMAND)
				break;
			if (!ax25_validate_nr(ax25, nr)) {
				ax25_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
				break;
			}
			ax25_frames_acked(ax25, nr);
			if (ax25->condition & OWN_RX_BUSY_CONDITION) {
				if (pf) ax25_enquiry_response(ax25);
				break;
			}
			if (ns == ax25->vr) {
				queued = ax25_rx_iframe(ax25, skb, frame);
				if (ax25->condition & OWN_RX_BUSY_CONDITION) {
					if (pf) ax25_enquiry_response(ax25);
					break;
				}
				ax25->vr = (ax25->vr + 1) % MODULUS;
				ax25->condition &= ~REJECT_CONDITION;
				if (pf) {
					ax25_enquiry_response(ax25);
				} else {
					if (!(ax25->condition & ACK_PENDING_CONDITION)) {
						ax25->t2timer = ax25->t2;
						ax25->condition |= ACK_PENDING_CONDITION;
					}
				}
			} else {
				if (ax25->condition & REJECT_CONDITION) {
					if (pf) ax25_enquiry_response(ax25);
				} else {
					ax25->condition |= REJECT_CONDITION;
					ax25_send_control(ax25, REJ | pf, C_RESPONSE);
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
int ax25_process_rx_frame(ax25_cb *ax25, struct sk_buff *skb, int type)
{
	int queued = 0, frametype;
	unsigned char *frame;

	del_timer(&ax25->timer);

	frame = skb->h.raw;

	frametype = ax25_decode(frame);

	switch (ax25->state) {
		case AX25_STATE_1:
			queued = ax25_state1_machine(ax25, skb, frame, frametype, type);
			break;
		case AX25_STATE_2:
			queued = ax25_state2_machine(ax25, skb, frame, frametype, type);
			break;
		case AX25_STATE_3:
			queued = ax25_state3_machine(ax25, skb, frame, frametype, type);
			break;
		case AX25_STATE_4:
			queued = ax25_state4_machine(ax25, skb, frame, frametype, type);
			break;
		default:
			printk("ax25_process_rx_frame: frame received - state = %d\n", ax25->state);
			break;
	}

	ax25_set_timer(ax25);

	return(queued);
}

#endif

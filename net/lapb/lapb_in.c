/*
 *	LAPB release 001
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	LAPB 001	Jonathan Naulor	Started Coding
 */

#include <linux/config.h>
#if defined(CONFIG_LAPB) || defined(CONFIG_LAPB_MODULE)
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
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/lapb.h>

/*
 *	State machine for state 0, Disconnected State.
 *	The handling of the timer(s) is in file lapb_timer.c.
 */
static void lapb_state0_machine(lapb_cb *lapb, struct sk_buff *skb, int frametype, int pf)
{
	switch (frametype) {
		case LAPB_SABM:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S0 RX SABM(%d)\n", lapb->token, pf);
#endif
			if (lapb->mode & LAPB_EXTENDED) {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S0 TX DM(%d)\n", lapb->token, pf);
#endif
				lapb_send_control(lapb, LAPB_DM, pf, LAPB_RESPONSE);
			} else {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S0 TX UA(%d)\n", lapb->token, pf);
#endif
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S0 -> S3\n", lapb->token);
#endif
				lapb_send_control(lapb, LAPB_UA, pf, LAPB_RESPONSE);
				lapb->state     = LAPB_STATE_3;
				lapb->condition = 0x00;
				lapb->t1timer   = 0;
				lapb->t2timer   = 0;
				lapb->n2count   = 0;
				lapb->vs        = 0;
				lapb->vr        = 0;
				lapb->va        = 0;
				lapb_connect_indication(lapb, LAPB_OK);
			}
			break;

		case LAPB_SABME:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S0 RX SABME(%d)\n", lapb->token, pf);
#endif
			if (lapb->mode & LAPB_EXTENDED) {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S0 TX UA(%d)\n", lapb->token, pf);
#endif
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S0 -> S3\n", lapb->token);
#endif
				lapb_send_control(lapb, LAPB_UA, pf, LAPB_RESPONSE);
				lapb->state     = LAPB_STATE_3;
				lapb->condition = 0x00;
				lapb->t1timer   = 0;
				lapb->t2timer   = 0;
				lapb->n2count   = 0;
				lapb->vs        = 0;
				lapb->vr        = 0;
				lapb->va        = 0;
				lapb_connect_indication(lapb, LAPB_OK);
			} else {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S0 TX DM(%d)\n", lapb->token, pf);
#endif
				lapb_send_control(lapb, LAPB_DM, pf, LAPB_RESPONSE);
			}
			break;

		case LAPB_DISC:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S0 RX DISC(%d)\n", lapb->token, pf);
			printk(KERN_DEBUG "lapb: (%p) S0 TX UA(%d)\n", lapb->token, pf);
#endif
			lapb_send_control(lapb, LAPB_UA, pf, LAPB_RESPONSE);
			break;

		default:
			break;
	}

	kfree_skb(skb, FREE_READ);
}

/*
 *	State machine for state 1, Awaiting Connection State.
 *	The handling of the timer(s) is in file lapb_timer.c.
 */
static void lapb_state1_machine(lapb_cb *lapb, struct sk_buff *skb, int frametype, int pf)
{
	switch (frametype) {
		case LAPB_SABM:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S1 RX SABM(%d)\n", lapb->token, pf);
#endif
			if (lapb->mode & LAPB_EXTENDED) {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S1 TX DM(%d)\n", lapb->token, pf);
#endif
				lapb_send_control(lapb, LAPB_DM, pf, LAPB_RESPONSE);
			} else {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S1 TX UA(%d)\n", lapb->token, pf);
#endif
				lapb_send_control(lapb, LAPB_UA, pf, LAPB_RESPONSE);
			}
			break;

		case LAPB_SABME:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S1 RX SABME(%d)\n", lapb->token, pf);
#endif
			if (lapb->mode & LAPB_EXTENDED) {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S1 TX UA(%d)\n", lapb->token, pf);
#endif
				lapb_send_control(lapb, LAPB_UA, pf, LAPB_RESPONSE);
			} else {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S1 TX DM(%d)\n", lapb->token, pf);
#endif
				lapb_send_control(lapb, LAPB_DM, pf, LAPB_RESPONSE);
			}
			break;

		case LAPB_DISC:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S1 RX DISC(%d)\n", lapb->token, pf);
			printk(KERN_DEBUG "lapb: (%p) S1 TX DM(%d)\n", lapb->token, pf);
#endif
			lapb_send_control(lapb, LAPB_DM, pf, LAPB_RESPONSE);
			break;

		case LAPB_UA:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S1 RX UA(%d)\n", lapb->token, pf);
#endif
			if (pf) {
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S1 -> S3\n", lapb->token);
#endif
				lapb->state     = LAPB_STATE_3;
				lapb->condition = 0x00;
				lapb->t1timer   = 0;
				lapb->t2timer   = 0;
				lapb->n2count   = 0;
				lapb->vs        = 0;
				lapb->vr        = 0;
				lapb->va        = 0;
				lapb_connect_confirmation(lapb, LAPB_OK);
			}
			break;

		case LAPB_DM:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S1 RX DM(%d)\n", lapb->token, pf);
#endif
			if (pf) {
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S1 -> S0\n", lapb->token);
#endif
				lapb_clear_queues(lapb);
				lapb->state   = LAPB_STATE_0;
				lapb->t1timer = (lapb->mode & LAPB_DCE) ? lapb->t1 : 0;
				lapb->t2timer = 0;
				lapb_disconnect_indication(lapb, LAPB_REFUSED);
			}
			break;

		default:
			break;
	}

	kfree_skb(skb, FREE_READ);
}

/*
 *	State machine for state 2, Awaiting Release State.
 *	The handling of the timer(s) is in file lapb_timer.c
 */
static void lapb_state2_machine(lapb_cb *lapb, struct sk_buff *skb, int frametype, int pf)
{
	switch (frametype) {
		case LAPB_SABM:
		case LAPB_SABME:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S2 RX {SABM,SABME}(%d)\n", lapb->token, pf);
			printk(KERN_DEBUG "lapb: (%p) S2 TX DM(%d)\n", lapb->token, pf);
#endif
			lapb_send_control(lapb, LAPB_DM, pf, LAPB_RESPONSE);
			break;

		case LAPB_DISC:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S2 RX DISC(%d)\n", lapb->token, pf);
			printk(KERN_DEBUG "lapb: (%p) S2 TX UA(%d)\n", lapb->token, pf);
#endif
			lapb_send_control(lapb, LAPB_UA, pf, C_RESPONSE);
			break;

		case LAPB_UA:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S2 RX UA(%d)\n", lapb->token, pf);
#endif
			if (pf) {
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S2 -> S0\n", lapb->token);
#endif
				lapb->state   = LAPB_STATE_0;
				lapb->t1timer = (lapb->mode & LAPB_DCE) ? lapb->t1 : 0;
				lapb->t2timer = 0;
				lapb_disconnect_confirmation(lapb, LAPB_OK);
			}
			break;

		case LAPB_DM:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S2 RX DM(%d)\n", lapb->token, pf);
#endif
			if (pf) {
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S2 -> S0\n", lapb->token);
#endif
				lapb->state   = LAPB_STATE_0;
				lapb->t1timer = (lapb->mode & LAPB_DCE) ? lapb->t1 : 0;
				lapb->t2timer = 0;
				lapb_disconnect_confirmation(lapb, LAPB_NOTCONNECTED);
			}
			break;

		case LAPB_I:
		case LAPB_REJ:
		case LAPB_RNR:
		case LAPB_RR:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S2 RX {I,REJ,RNR,RR}(%d)\n", lapb->token, pf);
			printk(KERN_DEBUG "lapb: (%p) S2 RX DM(%d)\n", lapb->token, pf);
#endif
			if (pf) lapb_send_control(lapb, LAPB_DM, pf, LAPB_RESPONSE);
			break;
				
		default:
			break;
	}

	kfree_skb(skb, FREE_READ);
}

/*
 *	State machine for state 3, Connected State.
 *	The handling of the timer(s) is in file lapb_timer.c
 */
static void lapb_state3_machine(lapb_cb *lapb, struct sk_buff *skb, int frametype, int ns, int nr, int pf, int type)
{
	int queued = 0;
	int modulus;
	
	modulus = (lapb->mode & LAPB_EXTENDED) ? LAPB_EMODULUS : LAPB_SMODULUS;

	switch (frametype) {
		case LAPB_SABM:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S3 RX SABM(%d)\n", lapb->token, pf);
#endif
			if (lapb->mode & LAPB_EXTENDED) {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S3 TX DM(%d)\n", lapb->token, pf);
#endif
				lapb_send_control(lapb, LAPB_DM, pf, LAPB_RESPONSE);
			} else {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S3 TX UA(%d)\n", lapb->token, pf);
#endif
				lapb_send_control(lapb, LAPB_UA, pf, LAPB_RESPONSE);
				lapb->condition = 0x00;
				lapb->t1timer   = 0;
				lapb->t2timer   = 0;
				lapb->vs        = 0;
				lapb->vr        = 0;
				lapb->va        = 0;
			}
			break;

		case LAPB_SABME:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S3 RX SABME(%d)\n", lapb->token, pf);
#endif
			if (lapb->mode & LAPB_EXTENDED) {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S3 TX UA(%d)\n", lapb->token, pf);
#endif
				lapb_send_control(lapb, LAPB_UA, pf, LAPB_RESPONSE);
				lapb->condition = 0x00;
				lapb->t1timer   = 0;
				lapb->t2timer   = 0;
				lapb->vs        = 0;
				lapb->vr        = 0;
				lapb->va        = 0;
			} else {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S3 TX DM(%d)\n", lapb->token, pf);
#endif
				lapb_send_control(lapb, LAPB_DM, pf, LAPB_RESPONSE);
			}
			break;

		case LAPB_DISC:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S3 RX DISC(%d)\n", lapb->token, pf);
#endif
#if LAPB_DEBUG > 0
			printk(KERN_DEBUG "lapb: (%p) S3 -> S0\n", lapb->token);
#endif
			lapb_clear_queues(lapb);
			lapb_send_control(lapb, LAPB_UA, pf, LAPB_RESPONSE);
			lapb->state   = LAPB_STATE_0;
			lapb->t1timer = (lapb->mode & LAPB_DCE) ? lapb->t1 : 0;
			lapb->t2timer = 0;
			lapb_disconnect_indication(lapb, LAPB_OK);
			break;

		case LAPB_DM:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S3 RX DM(%d)\n", lapb->token, pf);
#endif
#if LAPB_DEBUG > 0
			printk(KERN_DEBUG "lapb: (%p) S3 -> S0\n", lapb->token);
#endif
			lapb_clear_queues(lapb);
			lapb->state   = LAPB_STATE_0;
			lapb->t1timer = (lapb->mode & LAPB_DCE) ? lapb->t1 : 0;
			lapb->t2timer = 0;
			lapb_disconnect_indication(lapb, LAPB_NOTCONNECTED);
			break;

		case LAPB_RNR:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S3 RX RNR(%d) R%d\n", lapb->token, pf, nr);
#endif
			lapb->condition |= LAPB_PEER_RX_BUSY_CONDITION;
			lapb_check_need_response(lapb, type, pf);
			if (lapb_validate_nr(lapb, nr)) {
				lapb_check_iframes_acked(lapb, nr);
			} else {
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S3 -> S1\n", lapb->token);
#endif
				lapb_nr_error_recovery(lapb);
				lapb->state = LAPB_STATE_1;
			}
			break;
			
		case LAPB_RR:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S3 RX RR(%d) R%d\n", lapb->token, pf, nr);
#endif
			lapb->condition &= ~LAPB_PEER_RX_BUSY_CONDITION;
			lapb_check_need_response(lapb, type, pf);
			if (lapb_validate_nr(lapb, nr)) {
				lapb_check_iframes_acked(lapb, nr);
			} else {
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S3 -> S1\n", lapb->token);
#endif
				lapb_nr_error_recovery(lapb);
				lapb->state = LAPB_STATE_1;
			}
			break;
				
		case LAPB_REJ:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S3 RX REJ(%d) R%d\n", lapb->token, pf, nr);
#endif
			lapb->condition &= ~LAPB_PEER_RX_BUSY_CONDITION;
			lapb_check_need_response(lapb, type, pf);
			if (lapb_validate_nr(lapb, nr)) {
				lapb_frames_acked(lapb, nr);
				lapb->t1timer = 0;
				lapb_requeue_frames(lapb);
			} else {
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S3 -> S1\n", lapb->token);
#endif
				lapb_nr_error_recovery(lapb);
				lapb->state = LAPB_STATE_1;
			}
			break;
			
		case LAPB_I:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S3 RX I(%d) S%d R%d\n", lapb->token, pf, ns, nr);
#endif
			if (type != LAPB_COMMAND)
				break;
			if (!lapb_validate_nr(lapb, nr)) {
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S3 -> S1\n", lapb->token);
#endif
				lapb_nr_error_recovery(lapb);
				lapb->state = LAPB_STATE_1;
				break;
			}
			if (lapb->condition & LAPB_PEER_RX_BUSY_CONDITION) {
				lapb_frames_acked(lapb, nr);
			} else {
				lapb_check_iframes_acked(lapb, nr);
			}
			if (ns == lapb->vr) {
				lapb->vr = (lapb->vr + 1) % modulus;
				queued = lapb_data_indication(lapb, skb);
				lapb->condition &= ~LAPB_REJECT_CONDITION;
				if (pf) {
					lapb_enquiry_response(lapb);
				} else {
					if (!(lapb->condition & LAPB_ACK_PENDING_CONDITION)) {
						lapb->t2timer = lapb->t2;
						lapb->condition |= LAPB_ACK_PENDING_CONDITION;
					}
				}
			} else {
				if (lapb->condition & LAPB_REJECT_CONDITION) {
					if (pf)
						lapb_enquiry_response(lapb);
				} else {
#if LAPB_DEBUG > 1
					printk(KERN_DEBUG "lapb: (%p) S3 TX REJ(%d) R%d\n", lapb->token, pf, lapb->vr);
#endif
					lapb->condition |= LAPB_REJECT_CONDITION;
					lapb_send_control(lapb, LAPB_REJ, pf, LAPB_RESPONSE);
					lapb->condition &= ~LAPB_ACK_PENDING_CONDITION;
				}
			}
			break;

		case LAPB_FRMR:
		case LAPB_ILLEGAL:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S3 RX {FRMR,ILLEGAL}(%d)\n", lapb->token, pf);
#endif
#if LAPB_DEBUG > 0
			printk(KERN_DEBUG "lapb: (%p) S3 -> S1\n", lapb->token);
#endif
			lapb_establish_data_link(lapb);
			lapb->state = LAPB_STATE_1;
			break;

		default:
			break;
	}

	if (!queued)
		kfree_skb(skb, FREE_READ);
}

/*
 *	State machine for state 4, Timer Recovery State.
 *	The handling of the timer(s) is in file lapb_timer.c
 */
static void lapb_state4_machine(lapb_cb *lapb, struct sk_buff *skb, int frametype, int ns, int nr, int pf, int type)
{
	int queued = 0;
	int modulus;

	modulus = (lapb->mode & LAPB_EXTENDED) ? LAPB_EMODULUS : LAPB_SMODULUS;

	switch (frametype) {
		case LAPB_SABM:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S4 RX SABM(%d)\n", lapb->token, pf);
#endif
			if (lapb->mode & LAPB_EXTENDED) {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S4 TX DM(%d)\n", lapb->token, pf);
#endif
				lapb_send_control(lapb, LAPB_DM, pf, LAPB_RESPONSE);
			} else {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S4 TX UA(%d)\n", lapb->token, pf);
#endif
				lapb_send_control(lapb, LAPB_UA, pf, LAPB_RESPONSE);
				lapb->condition = 0x00;
				lapb->t1timer   = 0;
				lapb->t2timer   = 0;
				lapb->vs        = 0;
				lapb->vr        = 0;
				lapb->va        = 0;
			}
			break;

		case LAPB_SABME:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S4 RX SABME(%d)\n", lapb->token, pf);
#endif
			if (lapb->mode & LAPB_EXTENDED) {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S4 TX UA(%d)\n", lapb->token, pf);
#endif
				lapb_send_control(lapb, LAPB_UA, pf, LAPB_RESPONSE);
				lapb->condition = 0x00;
				lapb->t1timer   = 0;
				lapb->t2timer   = 0;
				lapb->vs        = 0;
				lapb->vr        = 0;
				lapb->va        = 0;
			} else {
#if LAPB_DEBUG > 1
				printk(KERN_DEBUG "lapb: (%p) S4 TX DM(%d)\n", lapb->token, pf);
#endif
				lapb_send_control(lapb, LAPB_DM, pf, LAPB_RESPONSE);
			}
			break;

		case LAPB_DISC:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S4 RX DISC(%d)\n", lapb->token, pf);
#endif
#if LAPB_DEBUG > 0
			printk(KERN_DEBUG "lapb: (%p) S4 -> S0\n", lapb->token);
#endif
			lapb_clear_queues(lapb);
			lapb_send_control(lapb, LAPB_UA, pf, LAPB_RESPONSE);
			lapb->state   = LAPB_STATE_0;
			lapb->t1timer = (lapb->mode & LAPB_DCE) ? lapb->t1 : 0;
			lapb->t2timer = 0;
			lapb_disconnect_indication(lapb, LAPB_OK);
			break;

		case LAPB_DM:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S4 RX DM(%d)\n", lapb->token, pf);
#endif
#if LAPB_DEBUG > 0
			printk(KERN_DEBUG "lapb: (%p) S4 -> S0\n", lapb->token);
#endif
			lapb_clear_queues(lapb);
			lapb->state   = LAPB_STATE_0;
			lapb->t1timer = (lapb->mode & LAPB_DCE) ? lapb->t1 : 0;
			lapb->t2timer = 0;
			lapb_disconnect_indication(lapb, LAPB_NOTCONNECTED);
			break;

		case LAPB_RNR:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S4 RX RNR(%d) R%d\n", lapb->token, pf, nr);
#endif
			lapb->condition |= LAPB_PEER_RX_BUSY_CONDITION;
			if (type == LAPB_RESPONSE && pf) {
				lapb->t1timer = 0;
				if (lapb_validate_nr(lapb, nr)) {
					lapb_frames_acked(lapb, nr);
					if (lapb->vs == lapb->va) {
#if LAPB_DEBUG > 0
						printk(KERN_DEBUG "lapb: (%p) S4 -> S3\n", lapb->token);
#endif
						lapb->n2count = 0;
						lapb->state   = LAPB_STATE_3;
					}
				} else {
#if LAPB_DEBUG > 0
					printk(KERN_DEBUG "lapb: (%p) S4 -> S1\n", lapb->token);
#endif
					lapb_nr_error_recovery(lapb);
					lapb->state = LAPB_STATE_1;
				}
				break;
			}
			 
			lapb_check_need_response(lapb, type, pf);
			if (lapb_validate_nr(lapb, nr)) {
				lapb_frames_acked(lapb, nr);
			} else {
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S4 -> S1\n", lapb->token);
#endif
				lapb_nr_error_recovery(lapb);
				lapb->state = LAPB_STATE_1;
			}
			break;
			
		case LAPB_RR:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S4 RX RR(%d) R%d\n", lapb->token, pf, nr);
#endif
			lapb->condition &= ~LAPB_PEER_RX_BUSY_CONDITION;
			if (pf && type == LAPB_RESPONSE) {
				lapb->t1timer = 0;
				if (lapb_validate_nr(lapb, nr)) {
					lapb_frames_acked(lapb, nr);
					if (lapb->vs == lapb->va) {
#if LAPB_DEBUG > 0
						printk(KERN_DEBUG "lapb: (%p) S4 -> S3\n", lapb->token);
#endif
						lapb->n2count = 0;
						lapb->state   = LAPB_STATE_3;
					} else {
						lapb_requeue_frames(lapb);
					}
				} else {
#if LAPB_DEBUG > 0
					printk(KERN_DEBUG "lapb: (%p) S4 -> S1\n", lapb->token);
#endif
					lapb_nr_error_recovery(lapb);
					lapb->state = LAPB_STATE_1;
				}
				break;
			}

			lapb_check_need_response(lapb, type, pf);
			if (lapb_validate_nr(lapb, nr)) {
				lapb_frames_acked(lapb, nr);
			} else {
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S4 -> S1\n", lapb->token);
#endif
				lapb_nr_error_recovery(lapb);
				lapb->state = LAPB_STATE_1;
			}
			break;

		case LAPB_REJ:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S4 RX REJ(%d) R%d\n", lapb->token, pf, nr);
#endif
			lapb->condition &= ~LAPB_PEER_RX_BUSY_CONDITION;
			if (pf && type == LAPB_RESPONSE) {
				lapb->t1timer = 0;
				if (lapb_validate_nr(lapb, nr)) {
					lapb_frames_acked(lapb, nr);
					if (lapb->vs == lapb->va) {
#if LAPB_DEBUG > 0
						printk(KERN_DEBUG "lapb: (%p) S4 -> S3\n", lapb->token);
#endif
						lapb->n2count = 0;
						lapb->state   = LAPB_STATE_3;
					} else {
						lapb_requeue_frames(lapb);
					}
				} else {
#if LAPB_DEBUG > 0
					printk(KERN_DEBUG "lapb: (%p) S4 -> S1\n", lapb->token);
#endif
					lapb_nr_error_recovery(lapb);
					lapb->state = LAPB_STATE_1;
				}
				break;
			}
			
			lapb_check_need_response(lapb, type, pf);	
			if (lapb_validate_nr(lapb, nr)) {
				lapb_frames_acked(lapb, nr);
				if (lapb->vs != lapb->va)
					lapb_requeue_frames(lapb);
			} else {
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S4 -> S1\n", lapb->token);
#endif
				lapb_nr_error_recovery(lapb);
				lapb->state = LAPB_STATE_1;
			}
			break;

		case LAPB_I:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S4 RX I(%d) S%d R%d\n", lapb->token, pf, ns, nr);
#endif
			if (type != LAPB_COMMAND)
				break;
			if (!lapb_validate_nr(lapb, nr)) {
#if LAPB_DEBUG > 0
				printk(KERN_DEBUG "lapb: (%p) S4 -> S1\n", lapb->token);
#endif
				lapb_nr_error_recovery(lapb);
				lapb->state = LAPB_STATE_1;
				break;
			}
			lapb_frames_acked(lapb, nr);
			if (ns == lapb->vr) {
				lapb->vr = (lapb->vr + 1) % modulus;
				queued = lapb_data_indication(lapb, skb);
				lapb->condition &= ~LAPB_REJECT_CONDITION;
				if (pf) {
					lapb_enquiry_response(lapb);
				} else {
					if (!(lapb->condition & LAPB_ACK_PENDING_CONDITION)) {
						lapb->t2timer = lapb->t2;
						lapb->condition |= LAPB_ACK_PENDING_CONDITION;
					}
				}
			} else {
				if (lapb->condition & LAPB_REJECT_CONDITION) {
					if (pf)
						lapb_enquiry_response(lapb);
				} else {
#if LAPB_DEBUG > 1
					printk(KERN_DEBUG "lapb: (%p) S4 TX REJ(%d) R%d\n", lapb->token, pf, lapb->vr);
#endif
					lapb->condition |= LAPB_REJECT_CONDITION;
					lapb_send_control(lapb, LAPB_REJ, pf, LAPB_RESPONSE);
					lapb->condition &= ~LAPB_ACK_PENDING_CONDITION;
				}
			}
			break;
		
		case LAPB_FRMR:
		case LAPB_ILLEGAL:
#if LAPB_DEBUG > 1
			printk(KERN_DEBUG "lapb: (%p) S4 TX {FRMR,ILLEGAL}(%d)\n", lapb->token, pf);
#endif
#if LAPB_DEBUG > 0
			printk(KERN_DEBUG "lapb: (%p) S4 -> S1\n", lapb->token);
#endif
			lapb_establish_data_link(lapb);
			lapb->state = LAPB_STATE_1;
			break;

		default:
			break;
	}

	if (!queued)
		kfree_skb(skb, FREE_READ);
}

/*
 *	Process an incoming LAPB frame
 */
int lapb_data_received(void *token, struct sk_buff *skb)
{
	int frametype, ns, nr, pf, type;
	lapb_cb *lapb;
	
	if ((lapb = lapb_tokentostruct(token)) == NULL)
		return LAPB_BADTOKEN;

	del_timer(&lapb->timer);

	frametype = lapb_decode(lapb, skb, &ns, &nr, &pf, &type);

	switch (lapb->state) {
		case LAPB_STATE_0:
			lapb_state0_machine(lapb, skb, frametype, pf);
			break;
		case LAPB_STATE_1:
			lapb_state1_machine(lapb, skb, frametype, pf);
			break;
		case LAPB_STATE_2:
			lapb_state2_machine(lapb, skb, frametype, pf);
			break;
		case LAPB_STATE_3:
			lapb_state3_machine(lapb, skb, frametype, ns, nr, pf, type);
			break;
		case LAPB_STATE_4:
			lapb_state4_machine(lapb, skb, frametype, ns, nr, pf, type);
			break;
	}

	lapb_set_timer(lapb);

	return LAPB_OK;
}

#endif

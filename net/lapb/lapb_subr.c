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
 *	LAPB 001	Jonathan Naylor	Started Coding
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
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/lapb.h>

/*
 *	This routine purges all the queues of frames.
 */
void lapb_clear_queues(lapb_cb *lapb)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&lapb->input_queue)) != NULL)
		kfree_skb(skb, FREE_READ);

	while ((skb = skb_dequeue(&lapb->write_queue)) != NULL)
		kfree_skb(skb, FREE_WRITE);

	while ((skb = skb_dequeue(&lapb->ack_queue)) != NULL)
		kfree_skb(skb, FREE_WRITE);
}

/*
 * This routine purges the input queue of those frames that have been
 * acknowledged. This replaces the boxes labelled "V(a) <- N(r)" on the
 * SDL diagram.
 */
void lapb_frames_acked(lapb_cb *lapb, unsigned short nr)
{
	struct sk_buff *skb;
	int modulus;

	modulus = (lapb->mode & LAPB_EXTENDED) ? LAPB_EMODULUS : LAPB_SMODULUS;

	/*
	 * Remove all the ack-ed frames from the ack queue.
	 */
	if (lapb->va != nr) {
		while (skb_peek(&lapb->ack_queue) != NULL && lapb->va != nr) {
		        skb = skb_dequeue(&lapb->ack_queue);
			kfree_skb(skb, FREE_WRITE);
			lapb->va = (lapb->va + 1) % modulus;
		}
	}
}

void lapb_requeue_frames(lapb_cb *lapb)
{
        struct sk_buff *skb, *skb_prev = NULL;

	/*
	 * Requeue all the un-ack-ed frames on the output queue to be picked
	 * up by lapb_kick called from the timer. This arrangement handles the
	 * possibility of an empty output queue.
	 */
	while ((skb = skb_dequeue(&lapb->ack_queue)) != NULL) {
		if (skb_prev == NULL)
			skb_queue_head(&lapb->write_queue, skb);
		else
			skb_append(skb_prev, skb);
		skb_prev = skb;
	}
}

/*
 *	Validate that the value of nr is between va and vs. Return true or
 *	false for testing.
 */
int lapb_validate_nr(lapb_cb *lapb, unsigned short nr)
{
	unsigned short vc = lapb->va;
	int modulus;
	
	modulus = (lapb->mode & LAPB_EXTENDED) ? LAPB_EMODULUS : LAPB_SMODULUS;

	while (vc != lapb->vs) {
		if (nr == vc) return 1;
		vc = (vc + 1) % modulus;
	}
	
	if (nr == lapb->vs) return 1;

	return 0;
}

/*
 *	This routine is the centralised routine for parsing the control
 *	information for the different frame formats.
 */
int lapb_decode(lapb_cb *lapb, struct sk_buff *skb, int *ns, int *nr, int *pf, int *type)
{
	int frametype = LAPB_ILLEGAL;

	*ns = *nr = *pf = *type = 0;

#if LAPB_DEBUG > 2
	printk(KERN_DEBUG "lapb: (%p) S%d RX %02X %02X %02X\n", lapb->token, lapb->state, skb->data[0], skb->data[1], skb->data[2]);
#endif

	if (lapb->mode & LAPB_MLP) {
		if (lapb->mode & LAPB_DCE) {
			if (skb->data[0] == LAPB_ADDR_D)
				*type = LAPB_COMMAND;
			if (skb->data[0] == LAPB_ADDR_C)
				*type = LAPB_RESPONSE;
		} else {
			if (skb->data[0] == LAPB_ADDR_C)
				*type = LAPB_COMMAND;
			if (skb->data[0] == LAPB_ADDR_D)
				*type = LAPB_RESPONSE;
		}
	} else {
		if (lapb->mode & LAPB_DCE) {
			if (skb->data[0] == LAPB_ADDR_B)
				*type = LAPB_COMMAND;
			if (skb->data[0] == LAPB_ADDR_A)
				*type = LAPB_RESPONSE;
		} else {
			if (skb->data[0] == LAPB_ADDR_A)
				*type = LAPB_COMMAND;
			if (skb->data[0] == LAPB_ADDR_B)
				*type = LAPB_RESPONSE;
		}
	}
		
	skb_pull(skb, 1);

	if (lapb->mode & LAPB_EXTENDED) {
		if ((skb->data[0] & LAPB_S) == 0) {
			frametype = LAPB_I;			/* I frame - carries NR/NS/PF */
			*ns = (skb->data[0] >> 1) & 0x7F;
			*nr = (skb->data[1] >> 1) & 0x7F;
			*pf = skb->data[1] & LAPB_EPF;
			skb_pull(skb, 2);
		} else if ((skb->data[0] & LAPB_U) == 1) { 	/* S frame - take out PF/NR */
			frametype = skb->data[0] & 0x0F;
			*nr = (skb->data[1] >> 1) & 0x7F;
			*pf = skb->data[1] & LAPB_EPF;
			skb_pull(skb, 2);
		} else if ((skb->data[0] & LAPB_U) == 3) { 	/* U frame - take out PF */
			frametype = skb->data[0] & ~LAPB_SPF;
			*pf = skb->data[0] & LAPB_SPF;
			skb_pull(skb, 1);
		}
	} else {
		if ((skb->data[0] & LAPB_S) == 0) {
			frametype = LAPB_I;			/* I frame - carries NR/NS/PF */
			*ns = (skb->data[0] >> 1) & 0x07;
			*nr = (skb->data[0] >> 5) & 0x07;
			*pf = skb->data[0] & LAPB_SPF;
		} else if ((skb->data[0] & LAPB_U) == 1) { 	/* S frame - take out PF/NR */
			frametype = skb->data[0] & 0x0F;
			*nr = (skb->data[0] >> 5) & 0x07;
			*pf = skb->data[0] & LAPB_SPF;
		} else if ((skb->data[0] & LAPB_U) == 3) { 	/* U frame - take out PF */
			frametype = skb->data[0] & ~LAPB_SPF;
			*pf = skb->data[0] & LAPB_SPF;
		}

		skb_pull(skb, 1);
	}

	return frametype;
}

/* 
 *	This routine is called when the HDLC layer internally  generates a
 *	command or  response  for  the remote machine ( eg. RR, UA etc. ). 
 *	Only supervisory or unnumbered frames are processed.
 */
void lapb_send_control(lapb_cb *lapb, int frametype, int poll_bit, int type)
{
	struct sk_buff *skb;
	unsigned char  *dptr;

	if ((skb = alloc_skb(LAPB_HEADER_LEN + 3, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, LAPB_HEADER_LEN + 1);

	/* Assume a response - address structure for DTE */
	if (lapb->mode & LAPB_EXTENDED) {
		if ((frametype & LAPB_U) == LAPB_U) {
			dptr = skb_put(skb, 1);
			*dptr = frametype;
			*dptr |= (poll_bit) ? LAPB_SPF : 0;
		} else {
			dptr = skb_put(skb, 2);
			dptr[0] = frametype;
			dptr[1] = (lapb->vr << 1);
			dptr[1] |= (poll_bit) ? LAPB_EPF : 0;
		}
	} else {
		dptr = skb_put(skb, 1);
		*dptr = frametype;
		*dptr |= (poll_bit) ? LAPB_SPF : 0;
		if ((frametype & LAPB_U) == LAPB_S)		/* S frames carry NR */
			*dptr |= (lapb->vr << 5);
	}

	lapb_transmit_buffer(lapb, skb, type);
}

#endif

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
 *	AX.25 029	Alan(GW4PTS)	Switched to KA9Q constant names. Removed
 *					old BSD code.
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
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

/* #define	NO_BACKOFF	*/

/*
 * This routine purges the input queue of frames.
 */
void ax25_clear_tx_queue(ax25_cb *ax25)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&ax25->write_queue)) != NULL) {
		skb->free = 1;
		kfree_skb(skb, FREE_WRITE);
	}

	while ((skb = skb_dequeue(&ax25->ack_queue)) != NULL) {
		skb->free = 1;
		kfree_skb(skb, FREE_WRITE);
	}
}

/*
 * This routine purges the input queue of those frames that have been
 * acknowledged. This replaces the boxes labelled "V(a) <- N(r)" on the
 * SDL diagram.
 */
void ax25_frames_acked(ax25_cb *ax25, unsigned short nr)
{
	struct sk_buff *skb, *skb_prev = NULL;

	/*
	 * Remove all the ack-ed frames from the ack queue.
	 */
	if (ax25->va != nr) {
		while (skb_peek(&ax25->ack_queue) != NULL && ax25->va != nr) {
		        skb = skb_dequeue(&ax25->ack_queue);
			skb->free = 1;
			kfree_skb(skb, FREE_WRITE);
			ax25->va = (ax25->va + 1) % MODULUS;
		}
	}

	/*
	 * Requeue all the un-ack-ed frames on the output queue to be picked
	 * up by ax25_kick called from the timer. This arrangement handles the
	 * possibility of an empty output queue.
	 */
	while ((skb = skb_dequeue(&ax25->ack_queue)) != NULL) {
		if (skb_prev == NULL)
			skb_queue_head(&ax25->write_queue, skb);
		else
			skb_append(skb_prev, skb);
		skb_prev = skb;
	}
}

/*
 *	Validate that the value of nr is between va and vs. Return true or
 *	false for testing.
 */
int ax25_validate_nr(ax25_cb *ax25, unsigned short nr)
{
	unsigned short vc = ax25->va;

	while (vc != ax25->vs) {
		if (nr == vc) return 1;
		vc = (vc + 1) % MODULUS;
	}
	
	if (nr == ax25->vs) return 1;

	return 0;
}

int ax25_decode(unsigned char *frame)
{
	int frametype = ILLEGAL;

	if ((frame[0] & S) == 0)
		frametype = I;		/* I frame - carries NR/NS/PF */
	else if ((frame[0] & U) == 1) 	/* S frame - take out PF/NR */
		frametype = frame[0] & 0x0F;
	else if ((frame[0] & U) == 3) 	/* U frame - take out PF */
		frametype = frame[0] & ~PF;

	return frametype;
}

/* 
 *	This routine is called when the HDLC layer internally  generates a
 *	command or  response  for  the remote machine ( eg. RR, UA etc. ). 
 *	Only supervisory or unnumbered frames are processed.
 */
void ax25_send_control(ax25_cb *ax25, int frametype, int type)
{
	struct sk_buff *skb;
	unsigned char  *dptr;
	struct device *dev;
	
	if ((dev = ax25->device) == NULL)
		return;	/* Route died */

	if ((skb = alloc_skb(AX25_BPQ_HEADER_LEN + size_ax25_addr(ax25->digipeat) + 1, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, AX25_BPQ_HEADER_LEN + size_ax25_addr(ax25->digipeat));

	if (ax25->sk != NULL) {
		skb->sk = ax25->sk;
        	ax25->sk->wmem_alloc += skb->truesize;
	}

	/* Assume a response - address structure for DTE */
	dptr = skb_put(skb, 1);
	
	if ((frametype & U) == S)		/* S frames carry NR */
		frametype |= (ax25->vr << 5);

	*dptr = frametype;

	skb->free = 1;

	ax25_transmit_buffer(ax25, skb, type);
}

/*
 *	Send a 'DM' to an unknown connection attempt, or an invalid caller.
 *
 *	Note: src here is the sender, thus its the target of the DM
 */
void ax25_return_dm(struct device *dev, ax25_address *src, ax25_address *dest, ax25_digi *digi)
{
	struct sk_buff *skb;
	char *dptr;
	ax25_digi retdigi;

	if (dev == NULL)
		return;

	if ((skb = alloc_skb(AX25_BPQ_HEADER_LEN + size_ax25_addr(digi) + 1, GFP_ATOMIC)) == NULL)
		return;	/* Next SABM will get DM'd */

	skb_reserve(skb, AX25_BPQ_HEADER_LEN + size_ax25_addr(digi));

	ax25_digi_invert(digi, &retdigi);

	dptr = skb_put(skb, 1);
	skb->sk = NULL;

	*dptr = DM | PF;

	/*
	 *	Do the address ourselves
	 */

	dptr  = skb_push(skb, size_ax25_addr(digi));
	dptr += build_ax25_addr(dptr, dest, src, &retdigi, C_RESPONSE);

	skb->arp  = 1;
	skb->free = 1;

	ax25_queue_xmit(skb, dev, SOPRI_NORMAL);
}

/*
 *	Exponential backoff for AX.25
 */
unsigned short ax25_calculate_t1(ax25_cb *ax25)
{
#ifndef NO_BACKOFF
	int n, t = 2;

	if (ax25->backoff)
		for (n = 0; n < ax25->n2count; n++)
			t *= 2;

	return t * ax25->rtt;
#else
	return 2 * ax25->rtt;
#endif
}

/*
 *	Calculate the Round Trip Time
 */
void ax25_calculate_rtt(ax25_cb *ax25)
{
	if (ax25->t1timer > 0 && ax25->n2count == 0)
		ax25->rtt = (9 * ax25->rtt + ax25->t1 - ax25->t1timer) / 10;

	/* Don't go below one second */
	if (ax25->rtt < 1 * PR_SLOWHZ)
		ax25->rtt = 1 * PR_SLOWHZ;
}

/*
 *	Digipeated address processing
 */
 

/*
 *	Given an AX.25 address pull of to, from, digi list, command/response and the start of data
 *
 */

unsigned char *ax25_parse_addr(unsigned char *buf, int len, ax25_address *src, ax25_address *dest, ax25_digi *digi, int *flags)
{
	int d = 0;
	
	if (len < 14) return NULL;
		
	if (flags != NULL) {
		*flags = 0;
	
		if (buf[6] & LAPB_C) {
			*flags = C_COMMAND;
		}
		if (buf[13] & LAPB_C) {
			*flags = C_RESPONSE;
		}
	}
		
	/* Copy to, from */
	if (dest != NULL) memcpy(dest, buf + 0, AX25_ADDR_LEN);
	if (src != NULL)  memcpy(src,  buf + 7, AX25_ADDR_LEN);
	buf += 2 * AX25_ADDR_LEN;
	len -= 2 * AX25_ADDR_LEN;
	digi->lastrepeat = -1;
	digi->ndigi      = 0;
	
	while (!(buf[-1] & LAPB_E))
	{
		if (d >= 6)  return NULL;	/* Max of 6 digis */
		if (len < 7) return NULL;	/* Short packet */

		if (digi != NULL) {
			memcpy(&digi->calls[d], buf, AX25_ADDR_LEN);
			digi->ndigi = d + 1;
			if (buf[6] & AX25_REPEATED) {
				digi->repeated[d] = 1;
				digi->lastrepeat  = d;
			} else {
				digi->repeated[d] = 0;
			}
		}

		buf += AX25_ADDR_LEN;
		len -= AX25_ADDR_LEN;
		d++;
	}

	return buf;
}

/*
 *	Assemble an AX.25 header from the bits
 */
		
int build_ax25_addr(unsigned char *buf, ax25_address *src, ax25_address *dest, ax25_digi *d, int flag)
{
	int len = 0;
	int ct  = 0;

	memcpy(buf, dest, AX25_ADDR_LEN);
	
	if (flag != C_COMMAND && flag != C_RESPONSE)
		printk("build_ax25_addr: Bogus flag %d\n!", flag);
	buf[6] &= ~(LAPB_E | LAPB_C);
	buf[6] |= SSID_SPARE;

	if (flag == C_COMMAND) buf[6] |= LAPB_C;

	buf += AX25_ADDR_LEN;
	len += AX25_ADDR_LEN;
	memcpy(buf, src, AX25_ADDR_LEN);
	buf[6] &= ~(LAPB_E | LAPB_C);
	buf[6] |= SSID_SPARE;

	if (flag == C_RESPONSE) buf[6] |= LAPB_C;
	/*
	 *	Fast path the normal digiless path
	 */
	if (d == NULL || d->ndigi == 0) {
		buf[6] |= LAPB_E;
		return 2 * AX25_ADDR_LEN;
	}	
	
	buf += AX25_ADDR_LEN;
	len += AX25_ADDR_LEN;
	
	while (ct < d->ndigi) {
		memcpy(buf, &d->calls[ct], AX25_ADDR_LEN);
		if (d->repeated[ct])
			buf[6] |= AX25_REPEATED;
		else
			buf[6] &= ~AX25_REPEATED;
		buf[6] &= ~LAPB_E;
		buf[6] |= SSID_SPARE;

		buf += AX25_ADDR_LEN;
		len += AX25_ADDR_LEN;
		ct++;
	}

	buf[-1] |= LAPB_E;
	
	return len;
}

int size_ax25_addr(ax25_digi *dp)
{
	if (dp == NULL)
		return 2 * AX25_ADDR_LEN;

	return AX25_ADDR_LEN * (2 + dp->ndigi);
}
	
/* 
 *	Reverse Digipeat List. May not pass both parameters as same struct
 */	

void ax25_digi_invert(ax25_digi *in, ax25_digi *out)
{
	int ct = 0;
	
	/* Invert the digipeaters */
	
	while (ct < in->ndigi) {
		out->calls[ct]    = in->calls[in->ndigi - ct - 1];
		out->repeated[ct] = 0;
		ct++;
	}
	
	/* Copy ndigis */
	out->ndigi = in->ndigi;

	/* Finish off */
	out->lastrepeat = 0;
}

#endif

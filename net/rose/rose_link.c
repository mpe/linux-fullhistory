/*
 *	Rose release 001
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
 *	Rose 001	Jonathan(G4KLX)	Cloned from rose_timer.c
 */

#include <linux/config.h>
#if defined(CONFIG_ROSE) || defined(CONFIG_ROSE_MODULE)
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
#include <linux/firewall.h>
#include <net/rose.h>

static void rose_link_timer(unsigned long);

/*
 *	Linux set timer
 */
void rose_link_set_timer(struct rose_neigh *neigh)
{
	unsigned long flags;

	save_flags(flags); cli();
	del_timer(&neigh->timer);
	restore_flags(flags);

	neigh->timer.data     = (unsigned long)neigh;
	neigh->timer.function = &rose_link_timer;
	neigh->timer.expires  = jiffies + 10;

	add_timer(&neigh->timer);
}

/*
 *	Rose Link Timer
 *
 *	This routine is called every 100ms. Decrement timer by this
 *	amount - if expired then process the event.
 */
static void rose_link_timer(unsigned long param)
{
	struct rose_neigh *neigh = (struct rose_neigh *)param;

	if (neigh->ftimer > 0)
		neigh->ftimer--;

	if (neigh->t0timer > 0) {
		neigh->t0timer--;

		if (neigh->t0timer == 0) {
			rose_transmit_restart_request(neigh);
			neigh->t0timer = sysctl_rose_restart_request_timeout;
		}
	}

	if (neigh->ftimer > 0 || neigh->t0timer > 0)
		rose_link_set_timer(neigh);
	else
		del_timer(&neigh->timer);
}

/*
 *	Interface to ax25_send_frame. Changes my level 2 callsign depending
 *	on whether we have a global ROSE callsign or use the default port
 *	callsign.
 */
static int rose_send_frame(struct sk_buff *skb, struct rose_neigh *neigh)
{
	ax25_address *rose_call;

	if (ax25cmp(&rose_callsign, &null_ax25_address) == 0)
		rose_call = (ax25_address *)neigh->dev->dev_addr;
	else
		rose_call = &rose_callsign;

	return ax25_send_frame(skb, 256, rose_call, &neigh->callsign, neigh->digipeat, neigh->dev);
}

/*
 *	Interface to ax25_link_up. Changes my level 2 callsign depending
 *	on whether we have a global ROSE callsign or use the default port
 *	callsign.
 */
static int rose_link_up(struct rose_neigh *neigh)
{
	ax25_address *rose_call;

	if (ax25cmp(&rose_callsign, &null_ax25_address) == 0)
		rose_call = (ax25_address *)neigh->dev->dev_addr;
	else
		rose_call = &rose_callsign;

	return ax25_link_up(rose_call, &neigh->callsign, neigh->digipeat, neigh->dev);
}

/*
 *	This handles all restart and diagnostic frames.
 */
void rose_link_rx_restart(struct sk_buff *skb, struct rose_neigh *neigh, unsigned short frametype)
{
	struct sk_buff *skbn;

	switch (frametype) {
		case ROSE_RESTART_REQUEST:
			neigh->t0timer   = 0;
			neigh->restarted = 1;
			del_timer(&neigh->timer);
			rose_transmit_restart_confirmation(neigh);
			break;

		case ROSE_RESTART_CONFIRMATION:
			neigh->t0timer   = 0;
			neigh->restarted = 1;
			del_timer(&neigh->timer);
			break;

		case ROSE_DIAGNOSTIC:
			printk(KERN_WARNING "rose: diagnostic #%d\n", skb->data[3]);
			break;

		default:
			printk(KERN_WARNING "rose: received unknown %02X with LCI 000\n", frametype);
			break;
	}

	if (neigh->restarted) {
		while ((skbn = skb_dequeue(&neigh->queue)) != NULL)
			if (!rose_send_frame(skbn, neigh))
				kfree_skb(skbn, FREE_WRITE);
	}
}

/*
 *	This routine is called when a Restart Request is needed
 */
void rose_transmit_restart_request(struct rose_neigh *neigh)
{
	struct sk_buff *skb;
	unsigned char *dptr;
	int len;

	len = AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + ROSE_MIN_LEN + 3;

	if ((skb = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN);

	dptr = skb_put(skb, ROSE_MIN_LEN + 3);

	*dptr++ = AX25_P_ROSE;
	*dptr++ = ROSE_GFI;
	*dptr++ = 0x00;
	*dptr++ = ROSE_RESTART_REQUEST;
	*dptr++ = 0x00;
	*dptr++ = 0;

	if (!rose_send_frame(skb, neigh))
		kfree_skb(skb, FREE_WRITE);
}

/*
 * This routine is called when a Restart Confirmation is needed
 */
void rose_transmit_restart_confirmation(struct rose_neigh *neigh)
{
	struct sk_buff *skb;
	unsigned char *dptr;
	int len;

	len = AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + ROSE_MIN_LEN + 1;

	if ((skb = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN);

	dptr = skb_put(skb, ROSE_MIN_LEN + 1);

	*dptr++ = AX25_P_ROSE;
	*dptr++ = ROSE_GFI;
	*dptr++ = 0x00;
	*dptr++ = ROSE_RESTART_CONFIRMATION;

	if (!rose_send_frame(skb, neigh))
		kfree_skb(skb, FREE_WRITE);
}

/*
 * This routine is called when a Diagnostic is required.
 */
void rose_transmit_diagnostic(struct rose_neigh *neigh, unsigned char diag)
{
	struct sk_buff *skb;
	unsigned char *dptr;
	int len;

	len = AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + ROSE_MIN_LEN + 2;

	if ((skb = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN);

	dptr = skb_put(skb, ROSE_MIN_LEN + 2);

	*dptr++ = AX25_P_ROSE;
	*dptr++ = ROSE_GFI;
	*dptr++ = 0x00;
	*dptr++ = ROSE_DIAGNOSTIC;
	*dptr++ = diag;

	if (!rose_send_frame(skb, neigh))
		kfree_skb(skb, FREE_WRITE);
}

/*
 * This routine is called when a Clear Request is needed outside of the context
 * of a connected socket.
 */
void rose_transmit_clear_request(struct rose_neigh *neigh, unsigned int lci, unsigned char cause)
{
	struct sk_buff *skb;
	unsigned char *dptr;
	int len;

	len = AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + ROSE_MIN_LEN + 3;

	if ((skb = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN);

	dptr = skb_put(skb, ROSE_MIN_LEN + 3);

	*dptr++ = AX25_P_ROSE;
	*dptr++ = ((lci >> 8) & 0x0F) | ROSE_GFI;
	*dptr++ = ((lci >> 0) & 0xFF);
	*dptr++ = ROSE_CLEAR_REQUEST;
	*dptr++ = cause;
	*dptr++ = 0x00;

	if (!rose_send_frame(skb, neigh))
		kfree_skb(skb, FREE_WRITE);
}

void rose_transmit_link(struct sk_buff *skb, struct rose_neigh *neigh)
{
	unsigned char *dptr;

#ifdef CONFIG_FIREWALL
	if (call_fw_firewall(PF_ROSE, skb->dev, skb->data, NULL) != FW_ACCEPT)
		return;
#endif

	if (!rose_link_up(neigh))
		neigh->restarted = 0;

	dptr = skb_push(skb, 1);
	*dptr++ = AX25_P_ROSE;

	if (neigh->restarted) {
		if (!rose_send_frame(skb, neigh))
			kfree_skb(skb, FREE_WRITE);
	} else {
		skb_queue_tail(&neigh->queue, skb);

		if (neigh->t0timer == 0) {
			rose_transmit_restart_request(neigh);
			neigh->t0timer = sysctl_rose_restart_request_timeout;
			rose_link_set_timer(neigh);
		}
	}
}

#endif

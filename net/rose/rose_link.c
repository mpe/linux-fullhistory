/*
 *	Rose release 001
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 2.1.0 or higher/ NET3.029
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
 *	Linux set/reset timer routines
 */
static void rose_link_set_timer(struct rose_neigh *neigh)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();
	del_timer(&neigh->timer);
	restore_flags(flags);

	neigh->timer.next     = neigh->timer.prev = NULL;	
	neigh->timer.data     = (unsigned long)neigh;
	neigh->timer.function = &rose_link_timer;

	neigh->timer.expires  = jiffies + 10;
	add_timer(&neigh->timer);
}

static void rose_link_reset_timer(struct rose_neigh *neigh)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();
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

	if (neigh->t0timer == 0 || --neigh->t0timer > 0) {
		rose_link_reset_timer(neigh);
		return;
	}

	/*
	 * T0 for a link has expired.
	 */
	rose_transmit_restart_request(neigh);

	neigh->t0timer = neigh->t0;

	rose_link_set_timer(neigh);
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
			if (!ax25_send_frame(skbn, (ax25_address *)neigh->dev->dev_addr, &neigh->callsign, neigh->digipeat, neigh->dev))
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
	*dptr++ = GFI;
	*dptr++ = 0x00;
	*dptr++ = ROSE_RESTART_REQUEST;
	*dptr++ = 0x00;
	*dptr++ = 0;

	skb->free = 1;
	skb->sk   = NULL;

	if (!ax25_send_frame(skb, (ax25_address *)neigh->dev->dev_addr, &neigh->callsign, neigh->digipeat, neigh->dev))
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
	*dptr++ = GFI;
	*dptr++ = 0x00;
	*dptr++ = ROSE_RESTART_CONFIRMATION;

	skb->free = 1;
	skb->sk   = NULL;

	if (!ax25_send_frame(skb, (ax25_address *)neigh->dev->dev_addr, &neigh->callsign, neigh->digipeat, neigh->dev))
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
	*dptr++ = GFI;
	*dptr++ = 0x00;
	*dptr++ = ROSE_DIAGNOSTIC;
	*dptr++ = diag;

	skb->free = 1;
	skb->sk   = NULL;

	if (!ax25_send_frame(skb, (ax25_address *)neigh->dev->dev_addr, &neigh->callsign, neigh->digipeat, neigh->dev))
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
	*dptr++ = ((lci >> 8) & 0x0F) | GFI;
	*dptr++ = ((lci >> 0) & 0xFF);
	*dptr++ = ROSE_CLEAR_REQUEST;
	*dptr++ = cause;
	*dptr++ = 0x00;

	skb->free = 1;
	skb->sk   = NULL;

	if (!ax25_send_frame(skb, (ax25_address *)neigh->dev->dev_addr, &neigh->callsign, neigh->digipeat, neigh->dev))
		kfree_skb(skb, FREE_WRITE);
}

void rose_transmit_link(struct sk_buff *skb, struct rose_neigh *neigh)
{
	unsigned char *dptr;

#ifdef CONFIG_FIREWALL
	if (call_fw_firewall(PF_ROSE, skb->dev, skb->data, NULL) != FW_ACCEPT)
		return;
#endif

	if (!ax25_link_up((ax25_address *)neigh->dev->dev_addr, &neigh->callsign, neigh->dev))
		neigh->restarted = 0;

	dptr = skb_push(skb, 1);
	*dptr++ = AX25_P_ROSE;

	skb->arp  = 1;
	skb->free = 1;

	if (neigh->restarted) {
		if (!ax25_send_frame(skb, (ax25_address *)neigh->dev->dev_addr, &neigh->callsign, neigh->digipeat, neigh->dev))
			kfree_skb(skb, FREE_WRITE);
	} else {
		skb_queue_tail(&neigh->queue, skb);
		
		if (neigh->t0timer == 0) {
			rose_transmit_restart_request(neigh);
			neigh->t0timer = neigh->t0;
			rose_link_set_timer(neigh);
		}
	}
}

#endif

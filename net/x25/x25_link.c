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
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/firewall.h>
#include <net/x25.h>

static struct x25_neigh *x25_neigh_list = NULL;

static void x25_link_timer(unsigned long);

/*
 *	Linux set/reset timer routines
 */
static void x25_link_set_timer(struct x25_neigh *neigh)
{
	unsigned long flags;

	save_flags(flags); cli();
	del_timer(&neigh->timer);
	restore_flags(flags);

	neigh->timer.data     = (unsigned long)neigh;
	neigh->timer.function = &x25_link_timer;
	neigh->timer.expires  = jiffies + (HZ / 1);

	add_timer(&neigh->timer);
}

/*
 *	X.25 Link TIMER 
 *
 *	This routine is called every second. Decrement timer by this
 *	amount - if expired then process the event.
 */
static void x25_link_timer(unsigned long param)
{
	struct x25_neigh *neigh = (struct x25_neigh *)param;

	if (neigh->t20timer == 0 || --neigh->t20timer > 0) {
		x25_link_set_timer(neigh);
		return;
	}

	/*
	 * T20 for a link has expired.
	 */
	x25_transmit_restart_request(neigh);

	neigh->t20timer = neigh->t20;

	x25_link_set_timer(neigh);
}

/*
 *	This handles all restart and diagnostic frames.
 */
void x25_link_control(struct sk_buff *skb, struct x25_neigh *neigh, unsigned short frametype)
{
	struct sk_buff *skbn;

	switch (frametype) {
		case X25_RESTART_REQUEST:
			neigh->t20timer = 0;
			neigh->state    = X25_LINK_STATE_3;
			del_timer(&neigh->timer);
			x25_transmit_restart_confirmation(neigh);
			break;

		case X25_RESTART_CONFIRMATION:
			neigh->t20timer = 0;
			neigh->state    = X25_LINK_STATE_3;
			del_timer(&neigh->timer);
			break;

		case X25_DIAGNOSTIC:
			printk(KERN_WARNING "x25: diagnostic #%d - %02X %02X %02X\n", skb->data[3], skb->data[4], skb->data[5], skb->data[6]);
			break;
			
		default:
			printk(KERN_WARNING "x25: received unknown %02X with LCI 000\n", frametype);
			break;
	}

	if (neigh->state == X25_LINK_STATE_3) {
		while ((skbn = skb_dequeue(&neigh->queue)) != NULL)
			x25_send_frame(skbn, neigh);
	}
}

/*
 *	This routine is called when a Restart Request is needed
 */
void x25_transmit_restart_request(struct x25_neigh *neigh)
{
	struct sk_buff *skb;
	unsigned char *dptr;
	int len;

	len = X25_MAX_L2_LEN + X25_STD_MIN_LEN + 2;

	if ((skb = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, X25_MAX_L2_LEN);

	dptr = skb_put(skb, X25_STD_MIN_LEN + 2);

	*dptr++ = (neigh->extended) ? X25_GFI_EXTSEQ : X25_GFI_STDSEQ;
	*dptr++ = 0x00;
	*dptr++ = X25_RESTART_REQUEST;
	*dptr++ = 0x00;
	*dptr++ = 0;

	skb->sk = NULL;

	x25_send_frame(skb, neigh);
}

/*
 * This routine is called when a Restart Confirmation is needed
 */
void x25_transmit_restart_confirmation(struct x25_neigh *neigh)
{
	struct sk_buff *skb;
	unsigned char *dptr;
	int len;

	len = X25_MAX_L2_LEN + X25_STD_MIN_LEN;

	if ((skb = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, X25_MAX_L2_LEN);

	dptr = skb_put(skb, X25_STD_MIN_LEN);

	*dptr++ = (neigh->extended) ? X25_GFI_EXTSEQ : X25_GFI_STDSEQ;
	*dptr++ = 0x00;
	*dptr++ = X25_RESTART_CONFIRMATION;

	skb->sk = NULL;

	x25_send_frame(skb, neigh);
}

/*
 * This routine is called when a Diagnostic is required.
 */
void x25_transmit_diagnostic(struct x25_neigh *neigh, unsigned char diag)
{
	struct sk_buff *skb;
	unsigned char *dptr;
	int len;

	len = X25_MAX_L2_LEN + X25_STD_MIN_LEN + 1;

	if ((skb = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, X25_MAX_L2_LEN);

	dptr = skb_put(skb, X25_STD_MIN_LEN + 1);

	*dptr++ = (neigh->extended) ? X25_GFI_EXTSEQ : X25_GFI_STDSEQ;
	*dptr++ = 0x00;
	*dptr++ = X25_DIAGNOSTIC;
	*dptr++ = diag;

	skb->sk = NULL;

	x25_send_frame(skb, neigh);
}

/*
 *	This routine is called when a Clear Request is needed outside of the context
 *	of a connected socket.
 */
void x25_transmit_clear_request(struct x25_neigh *neigh, unsigned int lci, unsigned char cause)
{
	struct sk_buff *skb;
	unsigned char *dptr;
	int len;

	len = X25_MAX_L2_LEN + X25_STD_MIN_LEN + 2;

	if ((skb = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, X25_MAX_L2_LEN);

	dptr = skb_put(skb, X25_STD_MIN_LEN + 2);

	*dptr++ = ((lci >> 8) & 0x0F) | (neigh->extended) ? X25_GFI_EXTSEQ : X25_GFI_STDSEQ;
	*dptr++ = ((lci >> 0) & 0xFF);
	*dptr++ = X25_CLEAR_REQUEST;
	*dptr++ = cause;
	*dptr++ = 0x00;

	skb->sk = NULL;

	x25_send_frame(skb, neigh);
}

void x25_transmit_link(struct sk_buff *skb, struct x25_neigh *neigh)
{
	if (call_fw_firewall(PF_X25, skb->dev, skb->data, NULL, &skb) != FW_ACCEPT) {
		kfree_skb(skb, FREE_WRITE);
		return;
	}

	switch (neigh->state) {
		case X25_LINK_STATE_0:
			skb_queue_tail(&neigh->queue, skb);
			neigh->state = X25_LINK_STATE_1;
			x25_establish_link(neigh);
			break;
		case X25_LINK_STATE_1:
		case X25_LINK_STATE_2:
			skb_queue_tail(&neigh->queue, skb);
			break;
		case X25_LINK_STATE_3:
			x25_send_frame(skb, neigh);
			break;
	}
}

/*
 *	Called when the link layer has become established.
 */
void x25_link_established(struct x25_neigh *neigh)
{
	switch (neigh->state) {
		case X25_LINK_STATE_0:
			neigh->state = X25_LINK_STATE_2;
			break;
		case X25_LINK_STATE_1:
			x25_transmit_restart_request(neigh);
			neigh->state    = X25_LINK_STATE_2;
			neigh->t20timer = neigh->t20;
			x25_link_set_timer(neigh);
			break;
	}
}

/*
 *	Called when the link layer has terminated, or an establishment
 *	request has failed. XXX should tell sockets.
 */
void x25_link_terminated(struct x25_neigh *neigh)
{
	neigh->state = X25_LINK_STATE_0;
}

/*
 *	Add a new device.
 */
void x25_link_device_up(struct device *dev)
{
	struct x25_neigh *x25_neigh;
	unsigned long flags;

	if ((x25_neigh = kmalloc(sizeof(*x25_neigh), GFP_ATOMIC)) == NULL)
		return;

	skb_queue_head_init(&x25_neigh->queue);
	init_timer(&x25_neigh->timer);

	x25_neigh->dev      = dev;
	x25_neigh->state    = X25_LINK_STATE_0;
	x25_neigh->extended = 0;
	x25_neigh->t20timer = 0;
	x25_neigh->t20      = sysctl_x25_restart_request_timeout;

	save_flags(flags); cli();
	x25_neigh->next = x25_neigh_list;
	x25_neigh_list  = x25_neigh;
	restore_flags(flags);
}

static void x25_remove_neigh(struct x25_neigh *x25_neigh)
{
	struct x25_neigh *s;
	unsigned long flags;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&x25_neigh->queue)) != NULL)
		kfree_skb(skb, FREE_WRITE);

	del_timer(&x25_neigh->timer);

	save_flags(flags);
	cli();

	if ((s = x25_neigh_list) == x25_neigh) {
		x25_neigh_list = x25_neigh->next;
		restore_flags(flags);
		kfree(x25_neigh);
		return;
	}

	while (s != NULL && s->next != NULL) {
		if (s->next == x25_neigh) {
			s->next = x25_neigh->next;
			restore_flags(flags);
			kfree(x25_neigh);
			return;
		}

		s = s->next;
	}

	restore_flags(flags);
}

/*
 *	A device has been removed, remove its links.
 */
void x25_link_device_down(struct device *dev)
{
	struct x25_neigh *neigh, *x25_neigh = x25_neigh_list;

	while (x25_neigh != NULL) {
		neigh     = x25_neigh;
		x25_neigh = x25_neigh->next;

		if (neigh->dev == dev)
			x25_remove_neigh(neigh);
	}
}

/*
 *	Given a device, return the neighbour address.
 */
struct x25_neigh *x25_get_neigh(struct device *dev)
{
	struct x25_neigh *x25_neigh;

	for (x25_neigh = x25_neigh_list; x25_neigh != NULL; x25_neigh = x25_neigh->next)
		if (x25_neigh->dev == dev)
			return x25_neigh;

	return NULL;
}

/*
 *	Handle the ioctls that control the subscription functions.
 */
int x25_subscr_ioctl(unsigned int cmd, void *arg)
{
	struct x25_subscrip_struct x25_subscr;
	struct x25_neigh *x25_neigh;
	struct device *dev;
	int err;

	switch (cmd) {

		case SIOCX25GSUBSCRIP:
			if ((err = verify_area(VERIFY_WRITE, arg, sizeof(struct x25_subscrip_struct))) != 0)
				return err;
			if ((dev = x25_dev_get(x25_subscr.device)) == NULL)
				return -EINVAL;
			if ((x25_neigh = x25_get_neigh(dev)) == NULL)
				return -EINVAL;
			x25_subscr.extended = x25_neigh->extended;
			copy_to_user(arg, &x25_subscr, sizeof(struct x25_subscrip_struct));
			break;

		case SIOCX25SSUBSCRIP:
			if ((err = verify_area(VERIFY_READ, arg, sizeof(struct x25_subscrip_struct))) != 0)
				return err;
			copy_from_user(&x25_subscr, arg, sizeof(struct x25_subscrip_struct));
			if ((dev = x25_dev_get(x25_subscr.device)) == NULL)
				return -EINVAL;
			if ((x25_neigh = x25_get_neigh(dev)) == NULL)
				return -EINVAL;
			if (x25_subscr.extended != 0 && x25_subscr.extended != 1)
				return -EINVAL;
			x25_neigh->extended = x25_subscr.extended;
			break;

		default:
			return -EINVAL;
	}

	return 0;
}

#ifdef MODULE

/*
 *	Release all memory associated with X.25 neighbour structures.
 */
void x25_link_free(void)
{
	struct x25_neigh *neigh, *x25_neigh = x25_neigh_list;

	while (x25_neigh != NULL) {
		neigh     = x25_neigh;
		x25_neigh = x25_neigh->next;

		x25_remove_neigh(neigh);
	}
}

#endif

#endif

/*
 *	Handle incoming frames
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_input.c,v 1.1 2000/02/18 16:47:12 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include "br_private.h"

unsigned char bridge_ula[5] = { 0x01, 0x80, 0xc2, 0x00, 0x00 };

static void br_pass_frame_up(struct net_bridge *br, struct sk_buff *skb)
{
	if (br->dev.flags & IFF_UP) {
		br->statistics.rx_packets++;
		br->statistics.rx_bytes += skb->len;

		skb->dev = &br->dev;
		skb->pkt_type = PACKET_HOST;
		skb->mac.raw = skb->data;
		skb_pull(skb, skb->nh.raw - skb->data);
		skb->protocol = eth_type_trans(skb, &br->dev);
		netif_rx(skb);

		return;
	}

	kfree_skb(skb);
}

static void __br_handle_frame(struct sk_buff *skb)
{
	struct net_bridge *br;
	unsigned char *dest;
	struct net_bridge_fdb_entry *dst;
	struct net_bridge_port *p;

	skb->nh.raw = skb->mac.raw;
	dest = skb->mac.ethernet->h_dest;

	p = skb->dev->br_port;
	br = p->br;

	if (p->state == BR_STATE_DISABLED ||
	    skb->mac.ethernet->h_source[0] & 1)
		goto freeandout;

	if (!memcmp(dest, bridge_ula, 5) && !(dest[5] & 0xF0))
		goto handle_special_frame;

	skb_push(skb, skb->data - skb->mac.raw);

	if (p->state == BR_STATE_LEARNING ||
	    p->state == BR_STATE_FORWARDING)
		br_fdb_insert(br, p, skb->mac.ethernet->h_source, 0);

	if (p->state != BR_STATE_FORWARDING)
		goto freeandout;

	if (dest[0] & 1) {
		br_flood(br, skb, 1);
		br_pass_frame_up(br, skb);
		return;
	}

	dst = br_fdb_get(br, dest);

	if (dst != NULL && dst->is_local) {
		br_pass_frame_up(br, skb);
		br_fdb_put(dst);
		return;
	}

	if (dst != NULL) {
		br_forward(dst->dst, skb);
		br_fdb_put(dst);
		return;
	}

	br_flood(br, skb, 0);
	return;

 handle_special_frame:
	if (!dest[5]) {
		br_stp_handle_bpdu(skb);
		return;
	}

 freeandout:
	kfree_skb(skb);
}

void br_handle_frame(struct sk_buff *skb)
{
	struct net_bridge *br;

	br = skb->dev->br_port->br;
	read_lock(&br->lock);
	__br_handle_frame(skb);
	read_unlock(&br->lock);
}

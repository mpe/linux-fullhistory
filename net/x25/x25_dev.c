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
#include <linux/stat.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/fcntl.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/if_arp.h>
#include <linux/firewall.h>
#include <net/x25.h>

static int x25_receive_data(struct sk_buff *skb, struct device *dev)
{
	struct x25_neigh *neigh;
	struct sock *sk;
	unsigned short frametype;
	unsigned int lci;

#ifdef CONFIG_FIREWALL
	if (call_in_firewall(PF_X25, skb->dev, skb->data, NULL) != FW_ACCEPT) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}
#endif

	/*
	 *	Packet received from unrecognised device, throw it away.
	 */
	if ((neigh = x25_get_neigh(dev)) == NULL) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}

	frametype = skb->data[2];
        lci = ((skb->data[0] << 8) & 0xF00) + ((skb->data[1] << 0) & 0x0FF);

	/*
	 *	LCI of zero is always for us, and its always a link control
	 *	frame.
	 */
	if (lci == 0) {
		x25_link_control(skb, neigh, frametype);
		return 0;
	}

	/*
	 *	Find an existing socket.
	 */
	if ((sk = x25_find_socket(lci)) != NULL) {
		skb->h.raw = skb->data;
		return x25_process_rx_frame(sk, skb);
	}

	/*
	 *	Is is a Call Request ? if so process it.
	 */
	if (frametype == X25_CALL_REQUEST)
		return x25_rx_call_request(skb, neigh, lci);

	/*
	 *	Its not a Call Request, nor is it a control frame, throw it awa
	 */
	x25_transmit_clear_request(neigh, lci, 0x0D);

	kfree_skb(skb, FREE_READ);

	return 0;
}

int x25_lapb_receive_frame(struct sk_buff *skb, struct device *dev, struct packet_type *ptype)
{
	skb->sk = NULL;
	
	switch (*skb->data) {
		case 0x00:
			skb_pull(skb, 1);
			return x25_receive_data(skb, dev);

		default:
			kfree_skb(skb, FREE_READ);
			return 0;
	}
}

int x25_llc_receive_frame(struct sk_buff *skb, struct device *dev, struct packet_type *ptype)
{
	unsigned int len;

	skb->sk = NULL;

	memcpy(&len, skb->data, sizeof(int));

	skb_pull(skb, sizeof(int));

	skb_trim(skb, len);
	
	return x25_receive_data(skb, dev);
}

int x25_link_up(struct device *dev)
{
	switch (dev->type) {
		case ARPHRD_ETHER:
			return 1;
		case ARPHRD_X25:
			return 0;
		default:
			return 0;
	}
}

void x25_send_frame(struct sk_buff *skb, struct device *dev)
{
	unsigned char *dptr;
	unsigned int  len;
	static char bcast_addr[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

	skb->protocol = htons(ETH_P_X25);
	skb->priority = SOPRI_NORMAL;
	skb->dev      = dev;
	skb->arp      = 1;

	switch (dev->type) {
		case ARPHRD_ETHER:
			len  = skb->len;
			dptr = skb_push(skb, sizeof(int));
			memcpy(dptr, &len, sizeof(int));
			dev->hard_header(skb, dev, ETH_P_X25, bcast_addr, NULL, 0);
			break;

		case ARPHRD_X25:
			dptr  = skb_push(skb, 1);
			*dptr = 0x00;
			break;

		default:
			kfree_skb(skb, FREE_WRITE);
			return;
	}

	dev_queue_xmit(skb);
}

#endif

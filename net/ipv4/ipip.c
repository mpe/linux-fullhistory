/*
 *	Linux NET3:	IP/IP protocol decoder. 
 *
 *	Authors:
 *		Sam Lantinga (slouken@cs.ucdavis.edu)  02/01/95
 *
 *	Fixes:
 *		Alan Cox	:	Merged and made usable non modular (its so tiny its silly as
 *					a module taking up 2 pages).
 *		Alan Cox	: 	Fixed bug with 1.3.18 and IPIP not working (now needs to set skb->h.iph)
 *					to keep ip_forward happy.
 *		Alan Cox	:	More fixes for 1.3.21, and firewall fix. Maybe this will work soon 8).
 *		Kai Schulte	:	Fixed #defines for IP_FIREWALL->FIREWALL
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */
 
#include <linux/module.h>

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/firewall.h>

#include <net/datalink.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/protocol.h>
#include <net/ipip.h>

/*
 *	The IPIP protocol driver.
 *
 *	On entry here
 *		skb->data is the original IP header
 *		skb->ip_hdr points to the initial IP header.
 *		skb->h.raw points at the new header.
 */

int ipip_rcv(struct sk_buff *skb, struct device *dev, struct options *opt, 
		__u32 daddr, unsigned short len, __u32 saddr,
                                   int redo, struct inet_protocol *protocol)
{
	/* Don't unlink in the middle of a turnaround */
	MOD_INC_USE_COUNT;
#ifdef TUNNEL_DEBUG
	printk("ipip_rcv: got a packet!\n");
#endif
	/*
	 *	Discard the original IP header
	 */
	 
	skb_pull(skb, ((struct iphdr *)skb->data)->ihl<<2);
	
	/*
	 *	Adjust pointers
	 */
	 
	skb->h.iph=(struct iphdr *)skb->data;
	skb->ip_hdr=(struct iphdr *)skb->data;
	memset(skb->proto_priv, 0, sizeof(struct options));

	/*
	 *	If you want to add LZ compressed IP or things like that here,
	 *	and in drivers/net/tunnel.c are the places to add.
	 */
	
	skb->protocol = htons(ETH_P_IP);
	skb->ip_summed = 0;
	netif_rx(skb);
	MOD_DEC_USE_COUNT;
	return(0);
}

#ifdef MODULE

static struct inet_protocol ipip_protocol = {
  ipip_rcv,             /* IPIP handler          */
#if 0
  NULL,                 /* Will be UDP fraglist handler */
#endif
  NULL,                 /* TUNNEL error control    */
  0,                    /* next                 */
  IPPROTO_IPIP,         /* protocol ID          */
  0,                    /* copy                 */
  NULL,                 /* data                 */
  "IPIP"                /* name                 */
};


/*
 *	And now the modules code and kernel interface.
 */

int init_module( void) 
{
	inet_add_protocol(&ipip_protocol);
	return 0;
}

void cleanup_module( void) 
{
	if ( inet_del_protocol(&ipip_protocol) < 0 )
		printk(KERN_INFO "ipip close: can't remove protocol\n");
}

#endif

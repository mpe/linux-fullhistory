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
#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/if_arp.h>
#include <linux/mroute.h>

#include <net/datalink.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/protocol.h>
#include <net/ipip.h>

void ipip_err(struct sk_buff *skb, unsigned char *dp)
{
	/* NI */
	return;
}

/*
 *	The IPIP protocol driver.
 *
 *	On entry here
 *		skb->data is the original IP header
 *		skb->nh points to the initial IP header.
 *		skb->h points at the new header.
 */

int ipip_rcv(struct sk_buff *skb, unsigned short len)
{
	struct device *dev;
	struct iphdr *iph;

#ifdef TUNNEL_DEBUG
	printk("ipip_rcv: got a packet!\n");
#endif
	/*
	 *	Discard the original IP header
	 */

	skb->mac.raw = skb->data;	 
	skb_pull(skb, skb->h.raw - skb->nh.raw);
	
	/*
	 *	Adjust pointers
	 */
	 
	iph = skb->nh.iph;
	skb->nh.iph = skb->h.ipiph;
	memset(&(IPCB(skb)->opt), 0, sizeof(struct ip_options));

	/*
	 *	If you want to add LZ compressed IP or things like that here,
	 *	and in drivers/net/tunnel.c are the places to add.
	 */
	
	skb->protocol = htons(ETH_P_IP);
	skb->ip_summed = 0;
	skb->pkt_type = PACKET_HOST;

	/*
	 * Is it draconic? I do not think so. --ANK
	 */
	dev = ip_dev_find_tunnel(iph->daddr, iph->saddr);
	if (dev == NULL) {
#ifdef CONFIG_IP_MROUTE
		int vif;

		if (!MULTICAST(skb->nh.iph->daddr) ||
		    !ipv4_config.multicast_route ||
		    LOCAL_MCAST(skb->nh.iph->daddr) ||
		    (vif=ip_mr_find_tunnel(iph->daddr, iph->saddr)) < 0)
		{
#endif
			kfree_skb(skb, FREE_READ);
			return -EINVAL;
#ifdef CONFIG_IP_MROUTE
		}
		IPCB(skb)->flags |= IPSKB_TUNNELED;
		IPCB(skb)->vif = vif;
		dev = skb->dev;
#endif
	}
	skb->dev = dev;
	dst_release(skb->dst);
	skb->dst = NULL;
	netif_rx(skb);
	return(0);
}

#ifdef MODULE

static struct inet_protocol ipip_protocol = {
  ipip_rcv,             /* IPIP handler          */
  ipip_err,             /* TUNNEL error control */
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

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
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */
 
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <net/datalink.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/ipip.h>

/*
 * NB. we must include the kernel idenfication string in to install the module.
 */
 
#if ( defined(CONFIG_NET_IPIP) && defined(CONFIG_IP_FORWARD)) || defined(MODULE)
#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>

static char kernel_version[] = UTS_RELEASE;

#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif 


/*
 *	The driver.
 */

int ipip_rcv(struct sk_buff *skb, struct device *dev, struct options *opt, 
		unsigned long daddr, unsigned short len, unsigned long saddr,
                                   int redo, struct inet_protocol *protocol)
{
	/* Don't unlink in the middle of a turnaround */
	MOD_INC_USE_COUNT;
#ifdef TUNNEL_DEBUG
	printk("ipip_rcv: got a packet!\n");
#endif
	skb->h.iph=skb->data;	/* Correct IP header pointer on to new header */
	if(ip_forward(skb, dev, 0, daddr, 0))
		kfree_skb(skb, FREE_READ);
	MOD_DEC_USE_COUNT;
	return(0);
}

#ifdef MODULE
static struct inet_protocol ipip_protocol = {
  ipip_rcv,             /* IPIP handler          */
  NULL,                 /* Will be UDP fraglist handler */
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
		printk("ipip close: can't remove protocol\n");
}

#endif
#endif

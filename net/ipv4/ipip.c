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
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */
 
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <net/datalink.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/protocol.h>
#include <net/ipip.h>
#include <linux/ip_fw.h>

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
#ifdef CONFIG_IP_FIREWALL
	int err;
#endif
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
	
#ifdef CONFIG_IP_FIREWALL
	/*
	 *	Check the firewall [well spotted Olaf]
	 */
	 
	if((err=ip_fw_chk(skb->ip_hdr,dev,ip_fw_blk_chain, ip_fw_blk_policy,0))<FW_ACCEPT)
	{
		if(err==FW_REJECT)
			icmp_send(skb,ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0 , dev);
		kfree_skb(skb, FREE_READ);
		return 0;
	}	
#endif

	/*
	 *	If you want to add LZ compressed IP or things like that here,
	 *	and in drivers/net/tunnel.c are the places to add.
	 */
	
	/* skb=lzw_uncompress(skb); */
	
	/*
	 *	Feed to IP forward.
	 */
	 
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

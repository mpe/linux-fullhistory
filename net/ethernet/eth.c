/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Ethernet-type device handling.
 *
 * Version:	@(#)eth.c	1.0.7	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 *		Florian  La Roche, <rzsfl@rz.uni-sb.de>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 * 
 * Fixes:
 *		Mr Linux	: Arp problems
 *		Alan Cox	: Generic queue tidyup (very tiny here)
 *		Alan Cox	: eth_header ntohs should be htons
 *		Alan Cox	: eth_rebuild_header missing an htons and
 *				  minor other things.
 *		Tegge		: Arp bug fixes. 
 *		Florian		: Removed many unnecessary functions, code cleanup
 *				  and changes for new arp and skbuff.
 *		Alan Cox	: Redid header building to reflect new format.
 *		Alan Cox	: ARP only when compiled with CONFIG_INET
 *		Greg Page	: 802.2 and SNAP stuff.
 *		Alan Cox	: MAC layer pointers/new format.
 *		Paul Gortmaker	: eth_copy_and_sum shouldn't csum padding.
 *		Alan Cox	: Protect against forwarding explosions with
 *				  older network drivers and IFF_ALLMULTI
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/config.h>
#include <net/arp.h>
#include <net/sock.h>
#include <asm/checksum.h>

void eth_setup(char *str, int *ints)
{
	struct device *d = dev_base;

	if (!str || !*str)
		return;
	while (d) 
	{
		if (!strcmp(str,d->name)) 
		{
			if (ints[0] > 0)
				d->irq=ints[1];
			if (ints[0] > 1)
				d->base_addr=ints[2];
			if (ints[0] > 2)
				d->mem_start=ints[3];
			if (ints[0] > 3)
				d->mem_end=ints[4];
			break;
		}
		d=d->next;
	}
}


/*
 *	 Create the Ethernet MAC header for an arbitrary protocol layer 
 *
 *	saddr=NULL	means use device source address
 *	daddr=NULL	means leave destination address (eg unresolved arp)
 */

int eth_header(struct sk_buff *skb, struct device *dev, unsigned short type,
	   void *daddr, void *saddr, unsigned len)
{
	struct ethhdr *eth = (struct ethhdr *)skb_push(skb,14);

	/* 
	 *	Set the protocol type. For a packet of type ETH_P_802_3 we put the length
	 *	in here instead. It is up to the 802.2 layer to carry protocol information.
	 */
	
	if(type!=ETH_P_802_3) 
		eth->h_proto = htons(type);
	else
		eth->h_proto = htons(len);

	/*
	 *	Set the source hardware address. 
	 */
	 
	if(saddr)
		memcpy(eth->h_source,saddr,dev->addr_len);
	else
		memcpy(eth->h_source,dev->dev_addr,dev->addr_len);

	/*
	 *	Anyway, the loopback-device should never use this function... 
	 */

	if (dev->flags & IFF_LOOPBACK) 
	{
		memset(eth->h_dest, 0, dev->addr_len);
		return(dev->hard_header_len);
	}
	
	if(daddr)
	{
		memcpy(eth->h_dest,daddr,dev->addr_len);
		return dev->hard_header_len;
	}
	
	return -dev->hard_header_len;
}


/*
 *	Rebuild the Ethernet MAC header. This is called after an ARP
 *	(or in future other address resolution) has completed on this
 *	sk_buff. We now let ARP fill in the other fields.
 */
 
int eth_rebuild_header(void *buff, struct device *dev, unsigned long dst,
			struct sk_buff *skb)
{
	struct ethhdr *eth = (struct ethhdr *)buff;

	/*
	 *	Only ARP/IP is currently supported
	 */
	 
	if(eth->h_proto != htons(ETH_P_IP)) 
	{
		printk("eth_rebuild_header: Don't know how to resolve type %d addresses?\n",(int)eth->h_proto);
		memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
		return 0;
	}

	/*
	 *	Try and get ARP to resolve the header.
	 */
#ifdef CONFIG_INET	 
	return arp_find(eth->h_dest, dst, dev, dev->pa_addr, skb)? 1 : 0;
#else
	return 0;	
#endif	
}


/*
 *	Determine the packet's protocol ID. The rule here is that we 
 *	assume 802.3 if the type field is short enough to be a length.
 *	This is normal practice and works for any 'now in use' protocol.
 */
 
unsigned short eth_type_trans(struct sk_buff *skb, struct device *dev)
{
	struct ethhdr *eth;
	unsigned char *rawp;
	
	skb->mac.raw=skb->data;
	skb_pull(skb,14);	
	eth= skb->mac.ethernet;
	
	if(*eth->h_dest&1)
	{
		if(memcmp(eth->h_dest,dev->broadcast, ETH_ALEN)==0)
			skb->pkt_type=PACKET_BROADCAST;
		else
			skb->pkt_type=PACKET_MULTICAST;
	}
	
	/*
	 *	This ALLMULTI check should be redundant by 1.4
	 *	so don't forget to remove it.
	 */
	 
	else if(dev->flags&(IFF_PROMISC|IFF_ALLMULTI))
	{
		if(memcmp(eth->h_dest,dev->dev_addr, ETH_ALEN))
			skb->pkt_type=PACKET_OTHERHOST;
	}
	
	if (ntohs(eth->h_proto) >= 1536)
		return eth->h_proto;
		
	rawp = skb->data;
	
	/*
	 *	This is a magic hack to spot IPX packets. Older Novell breaks
	 *	the protocol design and runs IPX over 802.3 without an 802.2 LLC
	 *	layer. We look for FFFF which isnt a used 802.2 SSAP/DSAP. This
	 *	won't work for fault tolerant netware but does for the rest.
	 */
	if (*(unsigned short *)rawp == 0xFFFF)
		return htons(ETH_P_802_3);
		
	/*
	 *	Real 802.2 LLC
	 */
	return htons(ETH_P_802_2);
}

/*
 *	Header caching for ethernet. Try to find and cache a header to avoid arp overhead.
 */
 
void eth_header_cache(struct device *dev, struct sock *sk, unsigned long saddr, unsigned long daddr)
{
	int v=arp_find_cache(sk->ip_hcache_data, daddr, dev);
	if(v!=1)
		sk->ip_hcache_state=0;	/* Try when arp resolves */
	else
	{
		memcpy(sk->ip_hcache_data+6, dev->dev_addr, ETH_ALEN);
		sk->ip_hcache_data[12]=ETH_P_IP>>8;
		sk->ip_hcache_data[13]=ETH_P_IP&0xFF;
		sk->ip_hcache_state=1;
		sk->ip_hcache_stamp=arp_cache_stamp;
		sk->ip_hcache_ver=&arp_cache_stamp;
	}
}

/*
 *	Copy from an ethernet device memory space to an sk_buff while checksumming if IP
 *	The magic "34" is Rx_addr+Tx_addr+type_field+sizeof(struct iphdr) == 6+6+2+20.
 */
 
void eth_copy_and_sum(struct sk_buff *dest, unsigned char *src, int length, int base)
{
	struct ethhdr *eth;
	struct iphdr *iph;
	int ip_length;

	IS_SKB(dest);
	eth=(struct ethhdr *)dest->data;
	if(eth->h_proto!=htons(ETH_P_IP))
	{
		memcpy(dest->data,src,length);
		return;
	}
	/*
	 * We have to watch for padded packets. The csum doesn't include the
	 * padding, and there is no point in copying the padding anyway.
	 * We have to use the smaller of length and ip_length because it
	 * can happen that ip_length > length.
	 */
	memcpy(dest->data,src,34);	/* ethernet is always >= 34 */
	length -= 34;
	iph=(struct iphdr*)(src+14);	/* 14 = Rx_addr+Tx_addr+type_field */
	ip_length = ntohs(iph->tot_len) - sizeof(struct iphdr);
	if (ip_length <= length)
		length=ip_length;

	dest->csum=csum_partial_copy(src+34,dest->data+34,length,base);
	dest->ip_summed=1;
}

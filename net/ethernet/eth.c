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
 *				  older network drivers and IFF_ALLMULTI.
 *	Christer Weinigel	: Better rebuild header message.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/ip.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/config.h>
#include <net/dst.h>
#include <net/arp.h>
#include <net/sock.h>
#include <net/ipv6.h>

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
#include <linux/in6.h>
#include <net/ndisc.h>
#endif

#include <asm/checksum.h>


#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
int (*ndisc_eth_hook) (unsigned char *, struct device *, 
		       struct sk_buff *) = NULL;
#endif

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
	struct ethhdr *eth = (struct ethhdr *)skb_push(skb,ETH_HLEN);

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
 
int eth_rebuild_header(struct sk_buff *skb)
{
	struct ethhdr *eth = (struct ethhdr *)skb->data;
	struct device *dev = skb->dev;

	/*
	 *	Only ARP/IP and NDISC/IPv6 are currently supported
	 */
	
	switch (eth->h_proto)
	{
#ifdef CONFIG_INET
	case __constant_htons(ETH_P_IP):

		/*
		 *	Try to get ARP to resolve the header.
		 */

		return arp_find(eth->h_dest, skb) ? 1 : 0;
		break;
#endif

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	case __constant_htons(ETH_P_IPV6):
#ifdef CONFIG_IPV6
		return (ndisc_eth_resolv(eth->h_dest, dev, skb));
#else
		if (ndisc_eth_hook)
			return (ndisc_eth_hook(eth->h_dest, dev, skb));
#endif
		break;
#endif	
	default:
		printk(KERN_DEBUG 
		       "%s: unable to resolve type %X addresses.\n", 
		       dev->name, (int)eth->h_proto);
		
		memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
		return 0;
		break;
	}

	return 0;	
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
	skb_pull(skb,dev->hard_header_len);
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
	 *	layer. We look for FFFF which isn't a used 802.2 SSAP/DSAP. This
	 *	won't work for fault tolerant netware but does for the rest.
	 */
	if (*(unsigned short *)rawp == 0xFFFF)
		return htons(ETH_P_802_3);
		
	/*
	 *	Real 802.2 LLC
	 */
	return htons(ETH_P_802_2);
}

int eth_header_cache(struct dst_entry *dst, struct dst_entry *neigh, struct hh_cache *hh)
{
	unsigned short type = hh->hh_type;
	struct ethhdr *eth = (struct ethhdr*)hh->hh_data;
	struct device *dev = dst->dev;

	if (type == ETH_P_802_3)
		return -1;
	
	eth->h_proto = htons(type);

	memcpy(eth->h_source, dev->dev_addr, dev->addr_len);

	if (dev->flags & IFF_LOOPBACK) {
		memset(eth->h_dest, 0, dev->addr_len);
		hh->hh_uptodate = 1;
		return 0;
	}

	if (type != ETH_P_IP) 
	{
		printk(KERN_DEBUG "%s: unable to resolve type %X addresses.\n",dev->name,(int)eth->h_proto);
		hh->hh_uptodate = 0;
		return 0;
	}

#ifdef CONFIG_INET
	hh->hh_uptodate = arp_find_1(eth->h_dest, dst, neigh);
#else
	hh->hh_uptodate = 0;
#endif
	return 0;
}

/*
 * Called by Address Resolution module to notify changes in address.
 */

void eth_header_cache_update(struct hh_cache *hh, struct device *dev, unsigned char * haddr)
{
	if (hh->hh_type != ETH_P_IP)
	{
		printk(KERN_DEBUG "eth_header_cache_update: %04x cache is not implemented\n", hh->hh_type);
		return;
	}
	memcpy(hh->hh_data, haddr, ETH_ALEN);
	hh->hh_uptodate = 1;
}

#ifndef CONFIG_IP_ROUTER

/*
 *	Copy from an ethernet device memory space to an sk_buff while checksumming if IP
 */
 
void eth_copy_and_sum(struct sk_buff *dest, unsigned char *src, int length, int base)
{
	struct ethhdr *eth;
	struct iphdr *iph;
	int ip_length;

	IS_SKB(dest);
	eth=(struct ethhdr *)src;
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
	memcpy(dest->data,src,sizeof(struct iphdr)+ETH_HLEN);	/* ethernet is always >= 34 */
	length -= sizeof(struct iphdr) + ETH_HLEN;
	iph=(struct iphdr*)(src+ETH_HLEN);
	ip_length = ntohs(iph->tot_len) - sizeof(struct iphdr);

	/* Also watch out for bogons - min IP size is 8 (rfc-1042) */
	if ((ip_length <= length) && (ip_length > 7))
		length=ip_length;

	dest->csum=csum_partial_copy(src+sizeof(struct iphdr)+ETH_HLEN,dest->data+sizeof(struct iphdr)+ETH_HLEN,length,base);
	dest->ip_summed=1;
}

#endif /* !(CONFIG_IP_ROUTER) */

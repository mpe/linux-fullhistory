/*
 *	Extension Header handling for IPv6
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	$Id: exthdrs.c,v 1.4 1997/03/18 18:24:29 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/icmpv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/rawv6.h>
#include <net/ndisc.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>

/*
 *	inbound
 */
#if 0
int ipv6_routing_header(struct sk_buff **skb_ptr, struct device *dev,
			__u8 *nhptr, struct ipv6_options *opt)
{
	struct sk_buff *skb = *skb_ptr;
	struct in6_addr *addr;
	struct in6_addr daddr;
	int addr_type = 0;
	int strict = 0;
	__u32 bit_map;
	int pos;
	int n, i;

	struct ipv6_rt_hdr *hdr = (struct ipv6_rt_hdr *) skb->h.raw;
	struct rt0_hdr *rthdr;

	if (hdr->segments_left == 0) {
		struct ipv6_options *opt;

		opt = (struct ipv6_options *) skb->cb;
		opt->srcrt = hdr;

		skb->h.raw += (hdr->hdrlen + 1) << 3;
		return hdr->nexthdr;		
	}

	if (hdr->type != IPV6_SRCRT_TYPE_0 || hdr->hdrlen & 0x01 ||
	    hdr->hdrlen > 46) {
                /* 
		 *	Discard 
		 */
		
		pos = (__u8 *) hdr - (__u8 *) skb->nh.ipv6h + 2;

		if (hdr->type)
			pos += 2;
		else
			pos += 1;

		icmpv6_send(skb, ICMPV6_PARAMETER_PROB, 0, pos, dev);
		kfree_skb(skb);
		return 0;	
	}

	/*
	 *	This is the routing header forwarding algorithm from
	 *	RFC 1883, page 17.
	 */

	n = hdr->hdrlen >> 1;

	if (hdr->segments_left > n) {
		pos = (__u8 *) hdr - (__u8 *) skb->nh.ipv6h + 2;

		pos += 3;

		icmpv6_send(skb, ICMPV6_PARAMETER_PROB, 0, pos, dev);
		kfree_skb(skb);
		return 0;
	}

	i = n - --hdr->segments_left;

	rthdr = (struct rt0_hdr *) hdr;
	addr = rthdr->addr;
	addr += i - 1;

	addr_type = ipv6_addr_type(addr);

	if (addr_type == IPV6_ADDR_MULTICAST) {
		kfree_skb(skb);
		return 0;
	}

	ipv6_addr_copy(&daddr, addr);
	ipv6_addr_copy(addr, &skb->nh.ipv6h->daddr);
	ipv6_addr_copy(&skb->nh.ipv6h->daddr, &daddr);

	/*
	 *	Check Strick Source Route
	 */

	bit_map = ntohl(rthdr->bitmap);

	if ((bit_map & (1 << i)) == IPV6_SRCRT_STRICT)
		strict = 1;

	ipv6_forward(skb, dev, (strict ? IP6_FW_STRICT : 0) | IP6_FW_SRCRT);

	return 0;
}


/*
 *	outbound
 */

int ipv6opt_bld_rthdr(struct sk_buff *skb, struct ipv6_options *opt,
		      struct in6_addr *addr, int proto)		      
{
	struct rt0_hdr *phdr, *ihdr;
	int hops;

	ihdr = (struct rt0_hdr *) opt->srcrt;
	
	phdr = (struct rt0_hdr *) skb_put(skb, (ihdr->rt_hdr.hdrlen + 1) << 3);
	memcpy(phdr, ihdr, sizeof(struct ipv6_rt_hdr));

	hops = ihdr->rt_hdr.hdrlen >> 1;
	
	if (hops > 1)
		memcpy(phdr->addr, ihdr->addr + 1,
		       (hops - 1) * sizeof(struct in6_addr));

	ipv6_addr_copy(phdr->addr + (hops - 1), addr);
	
	phdr->rt_hdr.nexthdr = proto;

	return NEXTHDR_ROUTING;
}
#endif

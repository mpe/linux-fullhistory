/*
 *	Extension Header handling for IPv6
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Andi Kleen		<ak@muc.de>
 *
 *	$Id: exthdrs.c,v 1.6 1998/04/30 16:24:20 freitag Exp $
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

#include <asm/uaccess.h>

#define swap(a,b) do { typeof (a) tmp; tmp = (a); (a) = (b); (b) = (tmp); } while(0)

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
		      struct in6_addr *addr)		      
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

/* 
 * find out if nexthdr is an extension header or a protocol
 */

static __inline__ int ipv6_ext_hdr(u8 nexthdr)
{
	/* 
	 * find out if nexthdr is an extension header or a protocol
	 */
	return ( (nexthdr == NEXTHDR_HOP)	||
		 (nexthdr == NEXTHDR_ROUTING)	||
		 (nexthdr == NEXTHDR_FRAGMENT)	||
		 (nexthdr == NEXTHDR_ESP)	||
		 (nexthdr == NEXTHDR_AUTH)	||
		 (nexthdr == NEXTHDR_NONE)	||
		 (nexthdr == NEXTHDR_DEST) );
		 
}

/*
 * Skip any extension headers. This is used by the ICMP module.
 *
 * Note that strictly speaking this conflicts with RFC1883 4.0:
 * ...The contents and semantics of each extension header determine whether 
 * or not to proceed to the next header.  Therefore, extension headers must
 * be processed strictly in the order they appear in the packet; a
 * receiver must not, for example, scan through a packet looking for a
 * particular kind of extension header and process that header prior to
 * processing all preceding ones.
 * 
 * We do exactly this. This is a protocol bug. We can't decide after a
 * seeing an unknown discard-with-error flavour TLV option if it's a 
 * ICMP error message or not (errors should never be send in reply to
 * ICMP error messages).
 * 
 * But I see no other way to do this. This might need to be reexamined
 * when Linux implements ESP (and maybe AUTH) headers.
 */
struct ipv6_opt_hdr *ipv6_skip_exthdr(struct ipv6_opt_hdr *hdr, 
				      u8 *nexthdrp, int len)
{
	u8 nexthdr = *nexthdrp;

	while (ipv6_ext_hdr(nexthdr)) {
		int hdrlen; 
		
		if (nexthdr == NEXTHDR_NONE)
			return NULL;
		if (len < sizeof(struct ipv6_opt_hdr)) /* be anal today */
			return NULL;

		hdrlen = ipv6_optlen(hdr); 
		if (len < hdrlen)
			return NULL; 

		nexthdr = hdr->nexthdr;
		hdr = (struct ipv6_opt_hdr *) ((u8*)hdr + hdrlen);
		len -= hdrlen;
	}

	/* Hack.. Do the same for AUTH headers? */
	if (nexthdr == NEXTHDR_ESP) 
		return NULL; 

	*nexthdrp = nexthdr;
	return hdr;
}


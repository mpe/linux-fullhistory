/*
 *	IPv6 output functions
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: ip6_output.c,v 1.3 1997/03/18 18:24:37 davem Exp $
 *
 *	Based on linux/net/ipv4/ip_output.c
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/in6.h>
#include <linux/route.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>

static u32	ipv6_fragmentation_id = 1;

static void ipv6_build_mac_hdr(struct sk_buff *skb, struct dst_entry *dst,
			       int len)
{
	struct device *dev;
	
	
	dev = dst->dev;

	skb->arp = 1;
	
	if (dev->hard_header) {
		int mac;
		
#if 0
		if (dst->hh)
			hh_copy_header(dst->hh, skb);
#endif
		mac = dev->hard_header(skb, dev, ETH_P_IPV6, NULL, NULL, len);

		if (mac < 0)
			skb->arp = 0;
	}
	
	skb->mac.raw = skb->data;
}

/*
 *	xmit an sk_buff (used by TCP)
 *	sk can be NULL (for sending RESETs)
 */

int ip6_xmit(struct sock *sk, struct sk_buff *skb, struct flowi *fl,
	     struct ipv6_options *opt)
{
	struct ipv6_pinfo *np = NULL;
	struct dst_entry *dst = NULL;
	struct ipv6hdr *hdr;
	int seg_len;

	hdr = skb->nh.ipv6h;

	if (sk)
		np = &sk->net_pinfo.af_inet6;

	if (np && np->dst) {
		/*
		 *	dst_check returns NULL if route is no longer valid
		 */
		dst = dst_check(&dst, np->dst_cookie);
	}

	if (dst == NULL) {
		dst = ip6_route_output(sk, fl);

		if (dst->error) {
			/*
			 *	NETUNREACH usually
			 */
			return dst->error;
		}
	}

	skb->dst = dst_clone(dst);
	skb->dev = dst->dev;
	seg_len = skb->tail - ((unsigned char *) hdr);
	
	/*
	 *	Link Layer headers
	 */

	skb->protocol = __constant_htons(ETH_P_IPV6);
	hdr = skb->nh.ipv6h;

	ipv6_build_mac_hdr(skb, dst, seg_len);

	
	/*
	 *	Fill in the IPv6 header
	 */

	hdr->version = 6;
	hdr->priority = np ? np->priority : 0;

	if (np)
		memcpy(hdr->flow_lbl, (void *) &np->flow_lbl, 3);
	else
		memset(hdr->flow_lbl, 0, 3);

	hdr->payload_len = htons(seg_len - sizeof(struct ipv6hdr));
	hdr->nexthdr = fl->proto;
	hdr->hop_limit = np ? np->hop_limit : ipv6_config.hop_limit;
	
	ipv6_addr_copy(&hdr->saddr, fl->nl_u.ip6_u.saddr);
	ipv6_addr_copy(&hdr->daddr, fl->nl_u.ip6_u.daddr);

	ipv6_statistics.Ip6OutRequests++;
	dst->output(skb);

	if (sk)
		ip6_dst_store(sk, dst);
	else
		dst_release(dst);

	return 0;
}

/*
 *	To avoid extra problems ND packets are send through this
 *	routine. It's code duplication but i really want to avoid
 *	extra checks since ipv6_build_header is used by TCP (which
 *	is for us performace critical)
 */

int ip6_nd_hdr(struct sock *sk, struct sk_buff *skb, struct device *dev,
	       struct in6_addr *saddr, struct in6_addr *daddr,
	       int proto, int len)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct ipv6hdr *hdr;
	int totlen;

	skb->protocol = __constant_htons(ETH_P_IPV6);
	skb->dev = dev;

	totlen = len + sizeof(struct ipv6hdr);

	skb->mac.raw = skb->data;

	hdr = (struct ipv6hdr *) skb_put(skb, sizeof(struct ipv6hdr));
	skb->nh.ipv6h = hdr;

	hdr->version  = 6;
	hdr->priority = np->priority & 0x0f;
	memset(hdr->flow_lbl, 0, 3);

	hdr->payload_len = htons(len);
	hdr->nexthdr = proto;
	hdr->hop_limit = np->hop_limit;

	ipv6_addr_copy(&hdr->saddr, saddr);
	ipv6_addr_copy(&hdr->daddr, daddr);

	return 0;
}

static void ip6_bld_1(struct sock *sk, struct sk_buff *skb, struct flowi *fl,
		      int hlimit, unsigned short pktlength)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct ipv6hdr *hdr;
	
	skb->nh.raw = skb_put(skb, sizeof(struct ipv6hdr));
	hdr = skb->nh.ipv6h;
	
	hdr->version = 6;
	hdr->priority = np->priority;
	
	memcpy(hdr->flow_lbl, &np->flow_lbl, 3);
	
	hdr->payload_len = htons(pktlength - sizeof(struct ipv6hdr));

	/*
	 *	FIXME: hop limit has default UNI/MCAST and
	 *	msgctl settings
	 */
	hdr->hop_limit = hlimit;

	ipv6_addr_copy(&hdr->saddr, fl->nl_u.ip6_u.saddr);
	ipv6_addr_copy(&hdr->daddr, fl->nl_u.ip6_u.daddr);	
}

static int ip6_frag_xmit(struct sock *sk, inet_getfrag_t getfrag,
			 const void *data, struct dst_entry *dst,
			 struct flowi *fl, struct ipv6_options *opt,
			 int hlimit, int flags, unsigned short length)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct ipv6hdr *hdr;
	struct sk_buff *last_skb;
	struct frag_hdr *fhdr;
	int unfrag_len;
	int payl_len;
	int frag_len;
	int last_len;
	int nfrags;
	int fhdr_dist;
	int err;

	/*
	 *	Fragmentation
	 *
	 *	Extension header order:
	 *	Hop-by-hop -> Routing -> Fragment -> rest (...)
	 *	
	 *	We must build the non-fragmented part that
	 *	will be in every packet... this also means
	 *	that other extension headers (Dest, Auth, etc)
	 *	must be considered in the data to be fragmented
	 */

	unfrag_len = sizeof(struct ipv6hdr) + sizeof(struct frag_hdr);
	payl_len = length;

	if (opt) {
		unfrag_len += opt->opt_nflen;
		payl_len += opt->opt_flen;
	}

	nfrags = payl_len / ((dst->pmtu - unfrag_len) & ~0x7);

	/*
	 *	Length of fragmented part on every packet but 
	 *	the last must be an:
	 *	"integer multiple of 8 octects".
	 */

	frag_len = (dst->pmtu - unfrag_len) & ~0x7;

	/*
	 *	We must send from end to start because of 
	 *	UDP/ICMP checksums. We do a funny trick:
	 *	fill the last skb first with the fixed
	 *	header (and its data) and then use it
	 *	to create the following segments and send it
	 *	in the end. If the peer is checking the M_flag
	 *	to trigger the reassembly code then this 
	 *	might be a good idea.
	 */

	last_len = payl_len - (nfrags * frag_len);

	if (last_len == 0) {
		last_len = frag_len;
		nfrags--;
	}
		
	last_skb = sock_alloc_send_skb(sk, unfrag_len + frag_len +
				       dst->dev->hard_header_len + 15,
				       0, flags & MSG_DONTWAIT, &err);

	if (last_skb == NULL)
		return err;

	last_skb->dst = dst_clone(dst);
	last_skb->dev = dst->dev;
	last_skb->protocol = htons(ETH_P_IPV6);
	last_skb->when = jiffies;
	last_skb->arp = 0;

	/* 
	 * build the mac header... 
	 */
	if (dst->dev->hard_header_len) {
		skb_reserve(last_skb, (dst->dev->hard_header_len + 15) & ~15);
		ipv6_build_mac_hdr(last_skb, dst, unfrag_len + frag_len);
	}
	
	hdr = (struct ipv6hdr *) skb_put(last_skb, sizeof(struct ipv6hdr));
	last_skb->nh.ipv6h = hdr;

	hdr->version = 6;
	hdr->priority = np->priority;
	
	memcpy(hdr->flow_lbl, &np->flow_lbl, 3);
	hdr->payload_len = htons(unfrag_len + frag_len - sizeof(struct ipv6hdr));

	hdr->hop_limit = hlimit;

	hdr->nexthdr = NEXTHDR_FRAGMENT;

	ipv6_addr_copy(&hdr->saddr, fl->nl_u.ip6_u.saddr);
	ipv6_addr_copy(&hdr->daddr, fl->nl_u.ip6_u.daddr);

#if 0
	if (opt && opt->srcrt) {
		hdr->nexthdr = ipv6opt_bld_rthdr(last_skb, opt, daddr,
						 NEXTHDR_FRAGMENT);
	}
#endif

	fhdr = (struct frag_hdr *) skb_put(last_skb, sizeof(struct frag_hdr));
	memset(fhdr, 0, sizeof(struct frag_hdr));

	fhdr->nexthdr  = fl->proto;		
	fhdr->frag_off = ntohs(nfrags * frag_len);
	fhdr->identification = ipv6_fragmentation_id++;

	fhdr_dist = (unsigned char *) fhdr - last_skb->data;

	err = getfrag(data, &hdr->saddr, last_skb->tail, nfrags * frag_len,
		      last_len);

	if (!err) {
		while (nfrags--) {
			struct sk_buff *skb;
			
			struct frag_hdr *fhdr2;
				
			printk(KERN_DEBUG "sending frag %d\n", nfrags);
			skb = skb_copy(last_skb, sk->allocation);

			if (skb == NULL)
				return -ENOMEM;
			
			fhdr2 = (struct frag_hdr *) (skb->data + fhdr_dist);

			/* more flag on */
			fhdr2->frag_off = ntohs(nfrags * frag_len + 1);

			/*
			 *	FIXME:
			 *	if (nfrags == 0)
			 *	put rest of headers
			 */

			err = getfrag(data, &hdr->saddr,skb_put(skb, frag_len),
				      nfrags * frag_len, frag_len);

			if (err) {
				kfree_skb(skb, FREE_WRITE);
				break;
			}

			ipv6_statistics.Ip6OutRequests++;
			dst->output(skb);
		}
	}

	if (err) {
		kfree_skb(last_skb, FREE_WRITE);
		return -EFAULT;
	}

	printk(KERN_DEBUG "sending last frag \n");

	hdr->payload_len = htons(unfrag_len + last_len - 
				 sizeof(struct ipv6hdr));

	/*
	 *	update last_skb to reflect the getfrag we did
	 *	on start.
	 */
	
	last_skb->tail += last_len;
	last_skb->len += last_len;

	/* 
	 *	toss the mac header out and rebuild it.
	 *	needed because of the different frame length.
	 *	ie: not needed for an ethernet.
	 */

	if (dst->dev->type != ARPHRD_ETHER && last_len != frag_len) {
		skb_pull(last_skb, (unsigned char *)last_skb->nh.ipv6h - 
			 last_skb->data);
		ipv6_build_mac_hdr(last_skb, dst, unfrag_len + last_len);
	}

	ipv6_statistics.Ip6OutRequests++;
	dst->output(last_skb);

	return 0;
}

int ip6_build_xmit(struct sock *sk, inet_getfrag_t getfrag, const void *data,
		   struct flowi *fl, unsigned short length,
		   struct ipv6_options *opt, int hlimit, int flags)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct in6_addr *final_dst = NULL;
	struct dst_entry *dst;
	int pktlength;
	int err = 0;
	
	if (opt && opt->srcrt) {
		struct rt0_hdr *rt0 = (struct rt0_hdr *) opt->srcrt;
		final_dst = fl->nl_u.ip6_u.daddr;
		fl->nl_u.ip6_u.daddr = rt0->addr;
	}

	dst = NULL;
	
	if (np->dst)
		dst = dst_check(&np->dst, np->dst_cookie);

	if (dst == NULL)
		dst = ip6_route_output(sk, fl);

	if (dst->error) {
		ipv6_statistics.Ip6OutNoRoutes++;
		err = -ENETUNREACH;
		goto out;
	}

	if (fl->nl_u.ip6_u.saddr == NULL) {
		struct inet6_ifaddr *ifa;
		
		ifa = ipv6_get_saddr(dst, fl->nl_u.ip6_u.daddr);
		
		if (ifa == NULL) {
#if IP6_DEBUG >= 2
			printk(KERN_DEBUG "ip6_build_xmit: "
			       "no availiable source address\n");
#endif
			err = -ENETUNREACH;
			goto out;
		}
		fl->nl_u.ip6_u.saddr = &ifa->addr;
	}
	
	pktlength = length;

	if (hlimit < 0)
		hlimit = np->hop_limit;

	if (!sk->ip_hdrincl) {
		pktlength += sizeof(struct ipv6hdr);
		if (opt)
			pktlength += opt->opt_flen + opt->opt_nflen;
	}

	if (pktlength <= dst->pmtu) {
		struct sk_buff *skb;
		struct ipv6hdr *hdr;
		struct device *dev;

		skb = sock_alloc_send_skb(sk, pktlength + 15 +
					  dst->dev->hard_header_len, 0,
					  flags & MSG_DONTWAIT, &err);

		if (skb == NULL) {
			ipv6_statistics.Ip6OutDiscards++;
			goto out;
		}

		dev = dst->dev;
		skb->dst = dst_clone(dst);

		skb->dev = dev;
		skb->protocol = htons(ETH_P_IPV6);
		skb->when = jiffies;
		skb->arp = 0;

		if (dev && dev->hard_header_len) {
			skb_reserve(skb, (dev->hard_header_len + 15) & ~15);
			ipv6_build_mac_hdr(skb, dst, pktlength);
		}

		hdr = (struct ipv6hdr *) skb->tail;
		skb->nh.ipv6h = hdr;
		
		if (!sk->ip_hdrincl) {
			ip6_bld_1(sk, skb, fl, hlimit, pktlength);
#if 0
			if (opt && opt->srcrt) {
				hdr->nexthdr = ipv6opt_bld_rthdr(skb, opt,
								 final_dst,
								 fl->proto);
			}
			else
#endif
				hdr->nexthdr = fl->proto;
		}

		skb_put(skb, length);
		err = getfrag(data, &hdr->saddr,
			      ((char *) hdr) + (pktlength - length),
			      0, length);
		
		if (!err) {
			ipv6_statistics.Ip6OutRequests++;
			dst->output(skb);
		} else {
			err = -EFAULT;
			kfree_skb(skb, FREE_WRITE);
		}
	} else {
		if (sk->ip_hdrincl)
			return -EMSGSIZE;
		
		err = ip6_frag_xmit(sk, getfrag, data, dst, fl, opt, hlimit,
				    flags, pktlength);
	}
	
	/*
	 *	cleanup
	 */
  out:
	
	if (np->dst)
		ip6_dst_store(sk, dst);
	else
		dst_release(dst);

	return err;
}

int ip6_forward(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct ipv6hdr *hdr = skb->nh.ipv6h;
	int size;
	
	/*
	 *	check hop-by-hop options present
	 */
#if 0
	if (hdr->nexthdr == NEXTHDR_HOP)
	{
	}
#endif
	/*
	 *	check and decrement ttl
	 */
	if (hdr->hop_limit <= 1) {
		icmpv6_send(skb, ICMPV6_TIME_EXCEED, ICMPV6_EXC_HOPLIMIT,
			    0, skb->dev);

		kfree_skb(skb, FREE_READ);
		return -ETIMEDOUT;
	}

	hdr->hop_limit--;

	if (skb->dev == dst->dev && dst->neighbour) {
		struct in6_addr *target = NULL;
		struct rt6_info *rt;
		struct nd_neigh *ndn = (struct nd_neigh *) dst->neighbour;

		/*
		 *	incoming and outgoing devices are the same
		 *	send a redirect.
		 */

		rt = (struct rt6_info *) dst;
		if ((rt->rt6i_flags & RTF_GATEWAY))
			target = &ndn->ndn_addr;
		else
			target = &hdr->daddr;

		ndisc_send_redirect(skb, dst->neighbour, target);
	}
	
	size = sizeof(struct ipv6hdr) + ntohs(hdr->payload_len);

	if (size > dst->pmtu) {
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, dst->pmtu, skb->dev);
		kfree_skb(skb, FREE_READ);
		return -EMSGSIZE;
	}

	skb->dev = dst->dev;

	/*
	 *	Rebuild the mac header
	 */
	if (skb_headroom(skb) < dst->dev->hard_header_len) {
		struct sk_buff *buff;

		buff = alloc_skb(dst->dev->hard_header_len + skb->len + 15,
				 GFP_ATOMIC);

		if (buff == NULL) {
			kfree_skb(skb, FREE_WRITE);
			return -ENOMEM;
		}
		
		skb_reserve(buff, (dst->dev->hard_header_len + 15) & ~15);

		buff->protocol = __constant_htons(ETH_P_IPV6);
		buff->h.raw = skb_put(buff, size);
		buff->dst = dst_clone(dst);
		buff->dev = dst->dev;

		memcpy(buff->h.raw, hdr, size);
		buff->nh.ipv6h = (struct ipv6hdr *) buff->h.raw;
		kfree_skb(skb, FREE_READ);
		skb = buff;
	} else {
		skb_pull(skb, skb->nh.raw - skb->data);
	}

	ipv6_build_mac_hdr(skb, dst, size);

	if (dst->neighbour)
		ndisc_event_send(dst->neighbour, skb);

	ipv6_statistics.Ip6ForwDatagrams++;
	dst->output(skb);

	return 0;
}

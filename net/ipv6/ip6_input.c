/*
 *	IPv6 input
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Ian P. Morris		<I.P.Morris@soton.ac.uk>
 *
 *	$Id: ip6_input.c,v 1.9 1998/04/30 16:24:24 freitag Exp $
 *
 *	Based in linux/net/ipv4/ip_input.c
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

static int ipv6_dest_opt(struct sk_buff **skb_ptr, struct device *dev,
			 __u8 *nhptr, struct ipv6_options *opt);

struct hdrtype_proc {
	u8	type;
	int	(*func) (struct sk_buff **, struct device *dev, __u8 *ptr,
			 struct ipv6_options *opt);
} hdrproc_lst[] = {

  /*
	TODO

	{NEXTHDR_HOP,		ipv6_hop_by_hop}
	{NEXTHDR_ROUTING,	ipv6_routing_header},
   */
	{NEXTHDR_FRAGMENT,	ipv6_reassembly},
  
	{NEXTHDR_DEST,		ipv6_dest_opt},
   /*	
	{NEXTHDR_AUTH,		ipv6_auth_hdr},
	{NEXTHDR_ESP,		ipv6_esp_hdr},
    */
	{NEXTHDR_MAX,		NULL}
};

/* New header structures */


struct ipv6_tlvtype {
	u8 type;
	u8 len;
};

struct tlvtype_proc {
	u8	type;
	int	(*func) (struct sk_buff *, struct device *dev, __u8 *ptr,
			 struct ipv6_options *opt);
	/*
	 *	these functions do NOT update skb->h.raw
	 */

} tlvprocdestopt_lst[] = {
	{255,			NULL}
};

static int ip6_dstopt_unknown(struct sk_buff *skb, struct ipv6_tlvtype *hdr)
{
	struct in6_addr *daddr;
	int pos;

	/*
	 *	unkown destination option type
	 */
	
	pos = (__u8 *) skb->h.raw - (__u8 *) skb->nh.raw;
	
	/* I think this is correct please check - IPM */

	switch ((hdr->type & 0xC0) >> 6) {
	case 0: /* ignore */
		skb->h.raw += hdr->len+2;
		return 1;
		
	case 1: /* drop packet */
		break;

	case 2: /* send ICMP PARM PROB regardless and drop packet */
		icmpv6_send(skb, ICMPV6_PARAMPROB, ICMPV6_UNK_OPTION,
			    pos, skb->dev);
		break;
		
	case 3: /* Send ICMP if not a multicast address and drop packet */
		daddr = &skb->nh.ipv6h->daddr;
		if (!(ipv6_addr_type(daddr) & IPV6_ADDR_MULTICAST))
			icmpv6_send(skb, ICMPV6_PARAMPROB,
				    ICMPV6_UNK_OPTION, pos, skb->dev);
	};
	
	kfree_skb(skb);
	return 0;
}

static int ip6_parse_tlv(struct tlvtype_proc *procs, struct sk_buff *skb,
			 struct device *dev, __u8 *nhptr,
			 struct ipv6_options *opt, void *lastopt)
{
	struct ipv6_tlvtype *hdr;
	struct tlvtype_proc *curr;

	while ((hdr=(struct ipv6_tlvtype *)skb->h.raw) != lastopt) {
		switch (hdr->type) {
		case 0: /* TLV encoded Pad1 */
			skb->h.raw++;
			break;

		case 1: /* TLV encoded PadN */
			skb->h.raw += hdr->len+2;
			break;

		default: /* Other TLV code so scan list */
			for (curr=procs; curr->type != 255; curr++) {
				if (curr->type == (hdr->type)) {
					curr->func(skb, dev, nhptr, opt);
					skb->h.raw += hdr->len+2;
					break;
				}
			}
			if (curr->type==255) {
				if (ip6_dstopt_unknown(skb, hdr) == 0)
					return 0;
			}
			break;
		}
	}
	return 1;
}

static int ipv6_dest_opt(struct sk_buff **skb_ptr, struct device *dev,
			 __u8 *nhptr, struct ipv6_options *opt)
{
	struct sk_buff *skb=*skb_ptr;
	struct ipv6_destopt_hdr *hdr = (struct ipv6_destopt_hdr *) skb->h.raw;
	int res = 0;
	void *lastopt=skb->h.raw+hdr->hdrlen+sizeof(struct ipv6_destopt_hdr);

	skb->h.raw += sizeof(struct ipv6_destopt_hdr);
	if (ip6_parse_tlv(tlvprocdestopt_lst, skb, dev, nhptr, opt, lastopt))
		res = hdr->nexthdr;
	skb->h.raw+=hdr->hdrlen;

	return res;
}


int ipv6_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	struct ipv6hdr *hdr;
	int pkt_len;

	if (skb->pkt_type == PACKET_OTHERHOST) {
		kfree_skb(skb);
		return 0;
	}

	hdr = skb->nh.ipv6h;

	if (skb->len < sizeof(struct ipv6hdr) || hdr->version != 6)
		goto err;

	pkt_len = ntohs(hdr->payload_len);

	if (pkt_len + sizeof(struct ipv6hdr) > skb->len)
		goto err;

	skb_trim(skb, pkt_len + sizeof(struct ipv6hdr));

	ip6_route_input(skb);
	
	return 0;
err:
	ipv6_statistics.Ip6InHdrErrors++;
	kfree_skb(skb);
	return 0;
}

/*
 *	0 - deliver
 *	1 - block
 */
static __inline__ int icmpv6_filter(struct sock *sk, struct sk_buff *skb)
{
	struct icmp6hdr *icmph;
	struct raw6_opt *opt;

	opt = &sk->tp_pinfo.tp_raw;
	icmph = (struct icmp6hdr *) (skb->nh.ipv6h + 1);
	return test_bit(icmph->icmp6_type, &opt->filter);
}

/*
 *	demultiplex raw sockets.
 *	(should consider queueing the skb in the sock receive_queue
 *	without calling rawv6.c)
 */
static struct sock * ipv6_raw_deliver(struct sk_buff *skb,
				      struct ipv6_options *opt,
				      int nexthdr, int len)
{
	struct in6_addr *saddr;
	struct in6_addr *daddr;
	struct sock *sk, *sk2;
	__u8 hash;

	saddr = &skb->nh.ipv6h->saddr;
	daddr = saddr + 1;

	hash = nexthdr & (MAX_INET_PROTOS - 1);

	sk = raw_v6_htable[hash];

	/*
	 *	The first socket found will be delivered after
	 *	delivery to transport protocols.
	 */

	if (sk == NULL)
		return NULL;

	sk = raw_v6_lookup(sk, nexthdr, daddr, saddr);

	if (sk) {
		sk2 = sk;

		while ((sk2 = raw_v6_lookup(sk2->next, nexthdr, daddr, saddr))) {
			struct sk_buff *buff;

			if (nexthdr == IPPROTO_ICMPV6 &&
			    icmpv6_filter(sk2, skb))
				continue;

			buff = skb_clone(skb, GFP_ATOMIC);
			buff->sk = sk2;
			rawv6_rcv(buff, skb->dev, saddr, daddr, opt, len);
		}
	}

	if (sk && nexthdr == IPPROTO_ICMPV6 && icmpv6_filter(sk, skb))
		sk = NULL;

	return sk;
}

/*
 *	Deliver the packet to the host
 */

int ip6_input(struct sk_buff *skb)
{
	struct ipv6_options *opt = (struct ipv6_options *) skb->cb;
	struct ipv6hdr *hdr = skb->nh.ipv6h;
	struct inet6_protocol *ipprot;
	struct hdrtype_proc *hdrt;
	struct sock *raw_sk;
	__u8 *nhptr;
	int nexthdr;
	int found = 0;
	u8 hash;
	int len;
	
	skb->h.raw += sizeof(struct ipv6hdr);

	/*
	 *	Parse extension headers
	 */

	nexthdr = hdr->nexthdr;
	nhptr = &hdr->nexthdr;

	/*
	 *	check for extension headers
	 */

st_loop:

	for (hdrt=hdrproc_lst; hdrt->type != NEXTHDR_MAX; hdrt++) {
		if (hdrt->type == nexthdr) {
			if ((nexthdr = hdrt->func(&skb, skb->dev, nhptr, opt))) {
				nhptr = skb->h.raw;
				hdr = skb->nh.ipv6h;
				goto st_loop;
			}
			return 0;
		}
	}

	len = skb->tail - skb->h.raw;

	raw_sk = ipv6_raw_deliver(skb, opt, nexthdr, len);

	hash = nexthdr & (MAX_INET_PROTOS - 1);
	for (ipprot = (struct inet6_protocol *) inet6_protos[hash]; 
	     ipprot != NULL; 
	     ipprot = (struct inet6_protocol *) ipprot->next) {
		struct sk_buff *buff = skb;
		
		if (ipprot->protocol != nexthdr)
			continue;
		
		if (ipprot->copy || raw_sk)
			buff = skb_clone(skb, GFP_ATOMIC);
		
		
		ipprot->handler(buff, skb->dev, &hdr->saddr, &hdr->daddr,
				opt, len, 0, ipprot);
		found = 1;
	}
	
	if (raw_sk) {
		skb->sk = raw_sk;
		rawv6_rcv(skb, skb->dev, &hdr->saddr, &hdr->daddr, opt, len);
		found = 1;
	}
	
	/*
	 *	not found: send ICMP parameter problem back
	 */
	
	if (!found) {
		unsigned long offset;
#if IP6_DEBUG >= 2
		printk(KERN_DEBUG "proto not found %d\n", nexthdr);
#endif
		offset = nhptr - (u8*) hdr;
		icmpv6_send(skb, ICMPV6_PARAMPROB, ICMPV6_UNK_NEXTHDR,
			    offset, skb->dev);
		kfree_skb(skb);
	}

	return 0;
}

int ip6_mc_input(struct sk_buff *skb)
{
	struct ipv6hdr *hdr;	
	int deliver = 0;
	int discard = 1;

	hdr = skb->nh.ipv6h;
	if (ipv6_chk_mcast_addr(skb->dev, &hdr->daddr))
		deliver = 1;

	/*
	 *	IPv6 multicast router mode isnt currently supported.
	 */
#if 0
	if (ipv6_config.multicast_route) {
		int addr_type;

		addr_type = ipv6_addr_type(&hdr->daddr);

		if (!(addr_type & (IPV6_ADDR_LOOPBACK | IPV6_ADDR_LINKLOCAL))) {
			struct sk_buff *skb2;
			struct dst_entry *dst;

			dst = skb->dst;
			
			if (deliver) {
				skb2 = skb_clone(skb, GFP_ATOMIC);
			} else {
				discard = 0;
				skb2 = skb;
			}

			dst->output(skb2);
		}
	}
#endif

	if (deliver) {
		discard = 0;
		ip6_input(skb);
	}

	if (discard)
		kfree_skb(skb);

	return 0;
}

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/netfilter.h>
#include <linux/in.h>
#include <linux/icmp.h>
#include <linux/seq_file.h>
#include <net/ip.h>
#include <net/checksum.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>

unsigned long ip_ct_icmp_timeout = 30*HZ;

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

static int icmp_pkt_to_tuple(const struct sk_buff *skb,
			     unsigned int dataoff,
			     struct ip_conntrack_tuple *tuple)
{
	struct icmphdr _hdr, *hp;

	hp = skb_header_pointer(skb, dataoff, sizeof(_hdr), &_hdr);
	if (hp == NULL)
		return 0;

	tuple->dst.u.icmp.type = hp->type;
	tuple->src.u.icmp.id = hp->un.echo.id;
	tuple->dst.u.icmp.code = hp->code;

	return 1;
}

static int icmp_invert_tuple(struct ip_conntrack_tuple *tuple,
			     const struct ip_conntrack_tuple *orig)
{
	/* Add 1; spaces filled with 0. */
	static u_int8_t invmap[]
		= { [ICMP_ECHO] = ICMP_ECHOREPLY + 1,
		    [ICMP_ECHOREPLY] = ICMP_ECHO + 1,
		    [ICMP_TIMESTAMP] = ICMP_TIMESTAMPREPLY + 1,
		    [ICMP_TIMESTAMPREPLY] = ICMP_TIMESTAMP + 1,
		    [ICMP_INFO_REQUEST] = ICMP_INFO_REPLY + 1,
		    [ICMP_INFO_REPLY] = ICMP_INFO_REQUEST + 1,
		    [ICMP_ADDRESS] = ICMP_ADDRESSREPLY + 1,
		    [ICMP_ADDRESSREPLY] = ICMP_ADDRESS + 1};

	if (orig->dst.u.icmp.type >= sizeof(invmap)
	    || !invmap[orig->dst.u.icmp.type])
		return 0;

	tuple->src.u.icmp.id = orig->src.u.icmp.id;
	tuple->dst.u.icmp.type = invmap[orig->dst.u.icmp.type] - 1;
	tuple->dst.u.icmp.code = orig->dst.u.icmp.code;
	return 1;
}

/* Print out the per-protocol part of the tuple. */
static int icmp_print_tuple(struct seq_file *s,
			    const struct ip_conntrack_tuple *tuple)
{
	return seq_printf(s, "type=%u code=%u id=%u ",
			  tuple->dst.u.icmp.type,
			  tuple->dst.u.icmp.code,
			  ntohs(tuple->src.u.icmp.id));
}

/* Print out the private part of the conntrack. */
static int icmp_print_conntrack(struct seq_file *s,
				const struct ip_conntrack *conntrack)
{
	return 0;
}

/* Returns verdict for packet, or -1 for invalid. */
static int icmp_packet(struct ip_conntrack *ct,
		       const struct sk_buff *skb,
		       enum ip_conntrack_info ctinfo)
{
	/* Try to delete connection immediately after all replies:
           won't actually vanish as we still have skb, and del_timer
           means this will only run once even if count hits zero twice
           (theoretically possible with SMP) */
	if (CTINFO2DIR(ctinfo) == IP_CT_DIR_REPLY) {
		if (atomic_dec_and_test(&ct->proto.icmp.count)
		    && del_timer(&ct->timeout))
			ct->timeout.function((unsigned long)ct);
	} else {
		atomic_inc(&ct->proto.icmp.count);
		ip_ct_refresh_acct(ct, ctinfo, skb, ip_ct_icmp_timeout);
	}

	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static int icmp_new(struct ip_conntrack *conntrack,
		    const struct sk_buff *skb)
{
	static u_int8_t valid_new[]
		= { [ICMP_ECHO] = 1,
		    [ICMP_TIMESTAMP] = 1,
		    [ICMP_INFO_REQUEST] = 1,
		    [ICMP_ADDRESS] = 1 };

	if (conntrack->tuplehash[0].tuple.dst.u.icmp.type >= sizeof(valid_new)
	    || !valid_new[conntrack->tuplehash[0].tuple.dst.u.icmp.type]) {
		/* Can't create a new ICMP `conn' with this. */
		DEBUGP("icmp: can't create new conn with type %u\n",
		       conntrack->tuplehash[0].tuple.dst.u.icmp.type);
		DUMP_TUPLE(&conntrack->tuplehash[0].tuple);
		return 0;
	}
	atomic_set(&conntrack->proto.icmp.count, 0);
	return 1;
}

static int
icmp_error_message(struct sk_buff *skb,
		   enum ip_conntrack_info *ctinfo,
		   unsigned int hooknum)
{
	struct ip_conntrack_tuple innertuple, origtuple;
	struct {
		struct icmphdr icmp;
		struct iphdr ip;
	} _in, *inside;
	struct ip_conntrack_protocol *innerproto;
	struct ip_conntrack_tuple_hash *h;
	int dataoff;

	IP_NF_ASSERT(skb->nfct == NULL);

	/* Not enough header? */
	inside = skb_header_pointer(skb, skb->nh.iph->ihl*4, sizeof(_in), &_in);
	if (inside == NULL)
		return NF_ACCEPT;

	/* Ignore ICMP's containing fragments (shouldn't happen) */
	if (inside->ip.frag_off & htons(IP_OFFSET)) {
		DEBUGP("icmp_error_track: fragment of proto %u\n",
		       inside->ip.protocol);
		return NF_ACCEPT;
	}

	innerproto = ip_ct_find_proto(inside->ip.protocol);
	dataoff = skb->nh.iph->ihl*4 + sizeof(inside->icmp) + inside->ip.ihl*4;
	/* Are they talking about one of our connections? */
	if (!ip_ct_get_tuple(&inside->ip, skb, dataoff, &origtuple, innerproto)) {
		DEBUGP("icmp_error: ! get_tuple p=%u", inside->ip.protocol);
		return NF_ACCEPT;
	}

	/* Ordinarily, we'd expect the inverted tupleproto, but it's
	   been preserved inside the ICMP. */
	if (!ip_ct_invert_tuple(&innertuple, &origtuple, innerproto)) {
		DEBUGP("icmp_error_track: Can't invert tuple\n");
		return NF_ACCEPT;
	}

	*ctinfo = IP_CT_RELATED;

	h = ip_conntrack_find_get(&innertuple, NULL);
	if (!h) {
		/* Locally generated ICMPs will match inverted if they
		   haven't been SNAT'ed yet */
		/* FIXME: NAT code has to handle half-done double NAT --RR */
		if (hooknum == NF_IP_LOCAL_OUT)
			h = ip_conntrack_find_get(&origtuple, NULL);

		if (!h) {
			DEBUGP("icmp_error_track: no match\n");
			return NF_ACCEPT;
		}
		/* Reverse direction from that found */
		if (DIRECTION(h) != IP_CT_DIR_REPLY)
			*ctinfo += IP_CT_IS_REPLY;
	} else {
		if (DIRECTION(h) == IP_CT_DIR_REPLY)
			*ctinfo += IP_CT_IS_REPLY;
	}

	/* Update skb to refer to this connection */
	skb->nfct = &h->ctrack->ct_general;
	skb->nfctinfo = *ctinfo;
	return -NF_ACCEPT;
}

/* Small and modified version of icmp_rcv */
static int
icmp_error(struct sk_buff *skb, enum ip_conntrack_info *ctinfo,
	   unsigned int hooknum)
{
	struct icmphdr _ih, *icmph;

	/* Not enough header? */
	icmph = skb_header_pointer(skb, skb->nh.iph->ihl*4, sizeof(_ih), &_ih);
	if (icmph == NULL) {
		if (LOG_INVALID(IPPROTO_ICMP))
			nf_log_packet(PF_INET, 0, skb, NULL, NULL,
				      "ip_ct_icmp: short packet ");
		return -NF_ACCEPT;
	}

	/* See ip_conntrack_proto_tcp.c */
	if (hooknum != NF_IP_PRE_ROUTING)
		goto checksum_skipped;

	switch (skb->ip_summed) {
	case CHECKSUM_HW:
		if (!(u16)csum_fold(skb->csum)) 
			break;
		if (LOG_INVALID(IPPROTO_ICMP))
			nf_log_packet(PF_INET, 0, skb, NULL, NULL, 
				      "ip_ct_icmp: bad HW ICMP checksum ");
		return -NF_ACCEPT;
	case CHECKSUM_NONE:
		if ((u16)csum_fold(skb_checksum(skb, 0, skb->len, 0))) {
			if (LOG_INVALID(IPPROTO_ICMP))
				nf_log_packet(PF_INET, 0, skb, NULL, NULL, 
					      "ip_ct_icmp: bad ICMP checksum ");
			return -NF_ACCEPT;
		}
	default:
		break;
	}

checksum_skipped:
	/*
	 *	18 is the highest 'known' ICMP type. Anything else is a mystery
	 *
	 *	RFC 1122: 3.2.2  Unknown ICMP messages types MUST be silently
	 *		  discarded.
	 */
	if (icmph->type > NR_ICMP_TYPES) {
		if (LOG_INVALID(IPPROTO_ICMP))
			nf_log_packet(PF_INET, 0, skb, NULL, NULL,
				      "ip_ct_icmp: invalid ICMP type ");
		return -NF_ACCEPT;
	}

	/* Need to track icmp error message? */
	if (icmph->type != ICMP_DEST_UNREACH
	    && icmph->type != ICMP_SOURCE_QUENCH
	    && icmph->type != ICMP_TIME_EXCEEDED
	    && icmph->type != ICMP_PARAMETERPROB
	    && icmph->type != ICMP_REDIRECT)
		return NF_ACCEPT;

	return icmp_error_message(skb, ctinfo, hooknum);
}

struct ip_conntrack_protocol ip_conntrack_protocol_icmp =
{
	.proto 			= IPPROTO_ICMP,
	.name 			= "icmp",
	.pkt_to_tuple		= icmp_pkt_to_tuple,
	.invert_tuple		= icmp_invert_tuple,
	.print_tuple		= icmp_print_tuple,
	.print_conntrack	= icmp_print_conntrack,
	.packet			= icmp_packet,
	.new			= icmp_new,
	.error			= icmp_error,
};

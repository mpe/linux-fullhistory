/*
 * This is a module which is used for rejecting packets.
 * Added support for customized reject packets (Jozsef Kadlecsik).
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/icmp.h>
#include <net/ip.h>
struct in_device;
#include <net/route.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_REJECT.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

static unsigned int reject(struct sk_buff **pskb,
			   unsigned int hooknum,
			   const struct net_device *in,
			   const struct net_device *out,
			   const void *targinfo,
			   void *userinfo)
{
	const struct ipt_reject_info *reject = targinfo;

	/* WARNING: This code causes reentry within iptables.
	   This means that the iptables jump stack is now crap.  We
	   must return an absolute verdict. --RR */
    	switch (reject->with) {
    	case IPT_ICMP_NET_UNREACHABLE:
    		icmp_send(*pskb, ICMP_DEST_UNREACH, ICMP_NET_UNREACH, 0);
    		break;
    	case IPT_ICMP_HOST_UNREACHABLE:
    		icmp_send(*pskb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0);
    		break;
    	case IPT_ICMP_PROT_UNREACHABLE:
    		icmp_send(*pskb, ICMP_DEST_UNREACH, ICMP_PROT_UNREACH, 0);
    		break;
    	case IPT_ICMP_PORT_UNREACHABLE:
    		icmp_send(*pskb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);
    		break;
    	case IPT_ICMP_ECHOREPLY: {
		struct icmphdr *icmph  = (struct icmphdr *)
			((u_int32_t *)(*pskb)->nh.iph + (*pskb)->nh.iph->ihl);
		unsigned int datalen = (*pskb)->len - (*pskb)->nh.iph->ihl * 4;

		/* Not non-head frags, or truncated */
		if (((ntohs((*pskb)->nh.iph->frag_off) & IP_OFFSET) == 0)
		    && datalen >= 4) {
			/* Usually I don't like cut & pasting code,
                           but dammit, my party is starting in 45
                           mins! --RR */
			struct icmp_bxm icmp_param;

			icmp_param.icmph=*icmph;
			icmp_param.icmph.type=ICMP_ECHOREPLY;
			icmp_param.data_ptr=(icmph+1);
			icmp_param.data_len=datalen;
			icmp_reply(&icmp_param, *pskb);
		}
	}
	break;
	}

	return NF_DROP;
}

static inline int find_ping_match(const struct ipt_entry_match *m)
{
	const struct ipt_icmp *icmpinfo = (const struct ipt_icmp *)m->data;

	if (strcmp(m->u.kernel.match->name, "icmp") == 0
	    && icmpinfo->type == ICMP_ECHO
	    && !(icmpinfo->invflags & IPT_ICMP_INV))
		return 1;

	return 0;
}

static int check(const char *tablename,
		 const struct ipt_entry *e,
		 void *targinfo,
		 unsigned int targinfosize,
		 unsigned int hook_mask)
{
 	const struct ipt_reject_info *rejinfo = targinfo;

 	if (targinfosize != IPT_ALIGN(sizeof(struct ipt_icmp))) {
  		DEBUGP("REJECT: targinfosize %u != 0\n", targinfosize);
  		return 0;
  	}

	/* Only allow these for packet filtering. */
	if (strcmp(tablename, "filter") != 0) {
		DEBUGP("REJECT: bad table `%s'.\n", table);
		return 0;
	}
	if ((hook_mask & ~((1 << NF_IP_LOCAL_IN)
			   | (1 << NF_IP_FORWARD)
			   | (1 << NF_IP_LOCAL_OUT))) != 0) {
		DEBUGP("REJECT: bad hook mask %X\n", hook_mask);
		return 0;
	}

	if (rejinfo->with == IPT_ICMP_ECHOREPLY) {
		/* Must specify that it's an ICMP ping packet. */
		if (e->ip.proto != IPPROTO_ICMP
		    || (e->ip.invflags & IPT_INV_PROTO)) {
			DEBUGP("REJECT: ECHOREPLY illegal for non-icmp\n");
			return 0;
		}
		/* Must contain ICMP match. */
		if (IPT_MATCH_ITERATE(e, find_ping_match) == 0) {
			DEBUGP("REJECT: ECHOREPLY illegal for non-ping\n");
			return 0;
		}
	}

	return 1;
}

static struct ipt_target ipt_reject_reg
= { { NULL, NULL }, "REJECT", reject, check, NULL, THIS_MODULE };

static int __init init(void)
{
	if (ipt_register_target(&ipt_reject_reg))
		return -EINVAL;
	return 0;
}

static void __exit fini(void)
{
	ipt_unregister_target(&ipt_reject_reg);
}

module_init(init);
module_exit(fini);

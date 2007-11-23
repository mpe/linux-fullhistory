/* Kernel module to match NFMARK values. */
#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter_ipv4/ipt_mark.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      const void *hdr,
      u_int16_t datalen,
      int *hotdrop)
{
	const struct ipt_mark_info *info = matchinfo;

	return ((skb->nfmark & info->mask) == info->mark) ^ info->invert;
}

static int
checkentry(const char *tablename,
           const struct ip6t_ip6 *ip,
           void *matchinfo,
           unsigned int matchsize,
           unsigned int hook_mask)
{
	if (matchsize != IP6T_ALIGN(sizeof(struct ipt_mark_info)))
		return 0;

	return 1;
}

static struct ip6t_match mark_match
= { { NULL, NULL }, "mark", &match, &checkentry, NULL, THIS_MODULE };

static int __init init(void)
{
	return ip6t_register_match(&mark_match);
}

static void __exit fini(void)
{
	ip6t_unregister_match(&mark_match);
}

module_init(init);
module_exit(fini);

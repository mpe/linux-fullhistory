/*
 *	Linux INET6 implementation
 *	FIB front-end.
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: route.c,v 1.34 1998/10/03 09:38:43 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/route.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/init.h>
#include <linux/netlink.h>
#include <linux/if_arp.h>

#ifdef 	CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#include <net/snmp.h>
#include <net/ipv6.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/tcp.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <asm/uaccess.h>

#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

#undef CONFIG_RT6_POLICY

/* Set to 3 to get tracing. */
#define RT6_DEBUG 2

#if RT6_DEBUG >= 3
#define RDBG(x) printk x
#define RT6_TRACE(x...) printk(KERN_DEBUG x)
#else
#define RDBG(x)
#define RT6_TRACE(x...) do { ; } while (0)
#endif

#if RT6_DEBUG >= 1
#define BUG_TRAP(x) ({ if (!(x)) { printk("Assertion (" #x ") failed at " __FILE__ "(%d):" __FUNCTION__ "\n", __LINE__); } })
#else
#define BUG_TRAP(x) do { ; } while (0)
#endif


int ip6_rt_max_size = 4096;
int ip6_rt_gc_min_interval = 5*HZ;
int ip6_rt_gc_timeout = 60*HZ;
int ip6_rt_gc_interval = 30*HZ;
int ip6_rt_gc_elasticity = 9;

static struct rt6_info * ip6_rt_copy(struct rt6_info *ort);
static struct dst_entry	*ip6_dst_check(struct dst_entry *dst, u32 cookie);
static struct dst_entry	*ip6_dst_reroute(struct dst_entry *dst,
					 struct sk_buff *skb);
static struct dst_entry *ip6_negative_advice(struct dst_entry *);
static int		 ip6_dst_gc(void);

static int		ip6_pkt_discard(struct sk_buff *skb);
static void		ip6_link_failure(struct sk_buff *skb);

struct dst_ops ip6_dst_ops = {
	AF_INET6,
	__constant_htons(ETH_P_IPV6),
	1024,

        ip6_dst_gc,
	ip6_dst_check,
	ip6_dst_reroute,
	NULL,
	ip6_negative_advice,
	ip6_link_failure,
};

struct rt6_info ip6_null_entry = {
	{{NULL, ATOMIC_INIT(1), ATOMIC_INIT(1), &loopback_dev,
	  -1, 0, 0, 0, 0, 0, 0, 0,
	  -ENETUNREACH, NULL, NULL,
	  ip6_pkt_discard, ip6_pkt_discard,
#ifdef CONFIG_NET_CLS_ROUTE
	  0,
#endif
	  &ip6_dst_ops}},
	NULL, {{{0}}}, RTF_REJECT|RTF_NONEXTHOP, ~0U,
	255, 0, ATOMIC_INIT(1), {NULL}, {{{{0}}}, 0}, {{{{0}}}, 0}
};

struct fib6_node ip6_routing_table = {
	NULL, NULL, NULL, NULL,
	&ip6_null_entry,
	0, RTN_ROOT|RTN_TL_ROOT|RTN_RTINFO, 0
};

#ifdef CONFIG_RT6_POLICY
int	ip6_rt_policy = 0;

struct pol_chain *rt6_pol_list = NULL;


static int rt6_flow_match_in(struct rt6_info *rt, struct sk_buff *skb);
static int rt6_flow_match_out(struct rt6_info *rt, struct sock *sk);

static struct rt6_info	*rt6_flow_lookup(struct rt6_info *rt,
					 struct in6_addr *daddr,
					 struct in6_addr *saddr,
					 struct fl_acc_args *args);

#else
#define ip6_rt_policy (0)
#endif

/*
 *	Route lookup
 */

static __inline__ struct rt6_info *rt6_device_match(struct rt6_info *rt,
						    int oif,
						    int strict)
{
	struct rt6_info *local = NULL;
	struct rt6_info *sprt;

	if (oif) {
		for (sprt = rt; sprt; sprt = sprt->u.next) {
			struct device *dev = sprt->rt6i_dev;
			if (dev->ifindex == oif)
				return sprt;
			if (dev->flags&IFF_LOOPBACK)
				local = sprt;
		}

		if (local)
			return local;

		if (strict)
			return &ip6_null_entry;
	}
	return rt;
}

/*
 *	pointer to the last default router chosen
 */
static struct rt6_info *rt6_dflt_pointer = NULL;

static struct rt6_info *rt6_best_dflt(struct rt6_info *rt, int oif)
{
	struct rt6_info *match = NULL;
	struct rt6_info *sprt;
	int mpri = 0;

	for (sprt = rt; sprt; sprt = sprt->u.next) {
		struct neighbour *neigh;

		RDBG(("sprt(%p): ", sprt));
		if ((neigh = sprt->rt6i_nexthop)) {
			int m = -1;

			RDBG(("nxthop(%p,%d) ", neigh, neigh->nud_state));
			switch (neigh->nud_state) {
			case NUD_REACHABLE:
				RDBG(("NUD_REACHABLE "));
				if (sprt != rt6_dflt_pointer) {
					rt = sprt;
					RDBG(("sprt!=dflt_ptr -> %p\n",
					      sprt));
					goto out;
				}
				RDBG(("m=2, "));
				m = 2;
				break;

			case NUD_DELAY:
				RDBG(("NUD_DELAY, m=1, "));
				m = 1;
				break;

			case NUD_STALE:
				RDBG(("NUD_STALE, m=1, "));
				m = 1;
				break;
			};

			if (oif && sprt->rt6i_dev->ifindex == oif) {
				m += 2;
			}

			if (m >= mpri) {
				RDBG(("m>=mpri setmatch, "));
				mpri = m;
				match = sprt;
			}
		}
	}

	if (match) {
		RDBG(("match, set rt, "));
		rt = match;
	} else {
		/*
		 *	No default routers are known to be reachable.
		 *	SHOULD round robin
		 */
		RDBG(("!match, trying rt6_dflt_pointer, "));
		if (rt6_dflt_pointer) {
			struct rt6_info *next;

			if ((next = rt6_dflt_pointer->u.next) &&
			    next->u.dst.error == 0)
				rt = next;
		}
	}

out:
	rt6_dflt_pointer = rt;
	RDBG(("returning %p, dflt_ptr set\n", rt));
	return rt;
}

struct rt6_info *rt6_lookup(struct in6_addr *daddr, struct in6_addr *saddr,
			    int oif, int strict)
{
	struct fib6_node *fn;
	struct rt6_info *rt;

	start_bh_atomic();
	fn = fib6_lookup(&ip6_routing_table, daddr, saddr);
	rt = rt6_device_match(fn->leaf, oif, strict);
	atomic_inc(&rt->u.dst.use);
	atomic_inc(&rt->u.dst.refcnt);
	end_bh_atomic();

	rt->u.dst.lastuse = jiffies;
	if (rt->u.dst.error == 0)
		return rt;
	dst_release(&rt->u.dst);
	return NULL;
}

static int rt6_ins(struct rt6_info *rt)
{
	int err;

	start_bh_atomic();
	err = fib6_add(&ip6_routing_table, rt);
	end_bh_atomic();

	return err;
}

static struct rt6_info *rt6_cow(struct rt6_info *ort, struct in6_addr *daddr,
				struct in6_addr *saddr)
{
	int err;
	struct rt6_info *rt;

	/*
	 *	Clone the route.
	 */

	rt = ip6_rt_copy(ort);

	if (rt) {
		ipv6_addr_copy(&rt->rt6i_dst.addr, daddr);

		if (!(rt->rt6i_flags&RTF_GATEWAY))
			ipv6_addr_copy(&rt->rt6i_gateway, daddr);

		rt->rt6i_dst.plen = 128;
		rt->rt6i_flags |= RTF_CACHE;

#ifdef CONFIG_IPV6_SUBTREES
		if (rt->rt6i_src.plen && saddr) {
			ipv6_addr_copy(&rt->rt6i_src.addr, saddr);
			rt->rt6i_src.plen = 128;
		}
#endif

		rt->rt6i_nexthop = ndisc_get_neigh(rt->rt6i_dev, &rt->rt6i_gateway);

		dst_clone(&rt->u.dst);
		err = rt6_ins(rt);
		if (err == 0)
			return rt;
		rt->u.dst.error = err;
		return rt;
	}
	dst_clone(&ip6_null_entry.u.dst);
	return &ip6_null_entry;
}

#ifdef CONFIG_RT6_POLICY
static __inline__ struct rt6_info *rt6_flow_lookup_in(struct rt6_info *rt,
						      struct sk_buff *skb)
{
	struct in6_addr *daddr, *saddr;
	struct fl_acc_args arg;

	arg.type = FL_ARG_FORWARD;
	arg.fl_u.skb = skb;

	saddr = &skb->nh.ipv6h->saddr;
	daddr = &skb->nh.ipv6h->daddr;

	return rt6_flow_lookup(rt, daddr, saddr, &arg);
}

static __inline__ struct rt6_info *rt6_flow_lookup_out(struct rt6_info *rt,
						       struct sock *sk,
						       struct flowi *fl)
{
	struct fl_acc_args arg;

	arg.type = FL_ARG_ORIGIN;
	arg.fl_u.fl_o.sk = sk;
	arg.fl_u.fl_o.flow = fl;

	return rt6_flow_lookup(rt, fl->nl_u.ip6_u.daddr, fl->nl_u.ip6_u.saddr,
			       &arg);
}

#endif

#define BACKTRACK() \
if (rt == &ip6_null_entry && strict) { \
       while ((fn = fn->parent) != NULL) { \
		if (fn->fn_flags & RTN_ROOT) { \
			dst_clone(&rt->u.dst); \
			goto out; \
		} \
		if (fn->fn_flags & RTN_RTINFO) \
			goto restart; \
	} \
}


void ip6_route_input(struct sk_buff *skb)
{
	struct fib6_node *fn;
	struct rt6_info *rt;
	int strict;

	strict = ipv6_addr_type(&skb->nh.ipv6h->daddr) & (IPV6_ADDR_MULTICAST|IPV6_ADDR_LINKLOCAL);

	fn = fib6_lookup(&ip6_routing_table, &skb->nh.ipv6h->daddr,
			 &skb->nh.ipv6h->saddr);

restart:
	rt = fn->leaf;

	if ((rt->rt6i_flags & RTF_CACHE)) {
		if (ip6_rt_policy == 0) {
			rt = rt6_device_match(rt, skb->dev->ifindex, strict);
			BACKTRACK();
			dst_clone(&rt->u.dst);
			goto out;
		}

#ifdef CONFIG_RT6_POLICY
		if ((rt->rt6i_flags & RTF_FLOW)) {
			struct rt6_info *sprt;

			for (sprt = rt; sprt; sprt = sprt->u.next) {
				if (rt6_flow_match_in(sprt, skb)) {
					rt = sprt;
					dst_clone(&rt->u.dst);
					goto out;
				}
			}
		}
#endif
	}

	rt = rt6_device_match(rt, skb->dev->ifindex, 0);
	BACKTRACK();

	if (ip6_rt_policy == 0) {
		if (!rt->rt6i_nexthop && !(rt->rt6i_flags & RTF_NONEXTHOP)) {
			rt = rt6_cow(rt, &skb->nh.ipv6h->daddr,
				     &skb->nh.ipv6h->saddr);
			goto out;
		}
		dst_clone(&rt->u.dst);
	} else {
#ifdef CONFIG_RT6_POLICY
		rt = rt6_flow_lookup_in(rt, skb);
#else
		/* NEVER REACHED */
#endif
	}

out:
	rt->u.dst.lastuse = jiffies;
	atomic_inc(&rt->u.dst.refcnt);
	skb->dst = (struct dst_entry *) rt;
}

struct dst_entry * ip6_route_output(struct sock *sk, struct flowi *fl)
{
	struct fib6_node *fn;
	struct rt6_info *rt;
	int strict;

	strict = ipv6_addr_type(fl->nl_u.ip6_u.daddr) & (IPV6_ADDR_MULTICAST|IPV6_ADDR_LINKLOCAL);

	start_bh_atomic();
	fn = fib6_lookup(&ip6_routing_table, fl->nl_u.ip6_u.daddr,
			 fl->nl_u.ip6_u.saddr);

restart:
	rt = fn->leaf;

	if ((rt->rt6i_flags & RTF_CACHE)) {
		if (ip6_rt_policy == 0) {
			rt = rt6_device_match(rt, fl->oif, strict);
			BACKTRACK();
			dst_clone(&rt->u.dst);
			goto out;
		}

#ifdef CONFIG_RT6_POLICY
		if ((rt->rt6i_flags & RTF_FLOW)) {
			struct rt6_info *sprt;

			for (sprt = rt; sprt; sprt = sprt->u.next) {
				if (rt6_flow_match_out(sprt, sk)) {
					rt = sprt;
					dst_clone(&rt->u.dst);
					goto out;
				}
			}
		}
#endif
	}
	if (rt->rt6i_flags & RTF_DEFAULT) {
		if (rt->rt6i_metric >= IP6_RT_PRIO_ADDRCONF)
			rt = rt6_best_dflt(rt, fl->oif);
	} else {
		rt = rt6_device_match(rt, fl->oif, strict);
		BACKTRACK();
	}

	if (ip6_rt_policy == 0) {
		if (!rt->rt6i_nexthop && !(rt->rt6i_flags & RTF_NONEXTHOP)) {
			rt = rt6_cow(rt, fl->nl_u.ip6_u.daddr,
				     fl->nl_u.ip6_u.saddr);
			goto out;
		}
		dst_clone(&rt->u.dst);
	} else {
#ifdef CONFIG_RT6_POLICY
		rt = rt6_flow_lookup_out(rt, sk, fl);
#else
		/* NEVER REACHED */
#endif
	}

out:
	rt->u.dst.lastuse = jiffies;
	atomic_inc(&rt->u.dst.refcnt);
	end_bh_atomic();
	return &rt->u.dst;
}


/*
 *	Destination cache support functions
 */

static struct dst_entry *ip6_dst_check(struct dst_entry *dst, u32 cookie)
{
	struct rt6_info *rt;

	rt = (struct rt6_info *) dst;

	if (rt && rt->rt6i_node && (rt->rt6i_node->fn_sernum == cookie))
		return dst;

	dst_release(dst);
	return NULL;
}

static struct dst_entry *ip6_dst_reroute(struct dst_entry *dst, struct sk_buff *skb)
{
	/*
	 *	FIXME
	 */
	RDBG(("ip6_dst_reroute(%p,%p)[%p] (AIEEE)\n", dst, skb,
	      __builtin_return_address(0)));
	return NULL;
}

static struct dst_entry *ip6_negative_advice(struct dst_entry *dst)
{
	dst_release(dst);
	return NULL;
}

static void ip6_link_failure(struct sk_buff *skb)
{
	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_ADDR_UNREACH, 0, skb->dev);
}

static int ip6_dst_gc()
{
	static unsigned expire = 30*HZ;
	static unsigned long last_gc;
	unsigned long now = jiffies;

	start_bh_atomic();
	if ((long)(now - last_gc) < ip6_rt_gc_min_interval)
		goto out;

	expire++;
	fib6_run_gc(expire);
	last_gc = now;
	if (atomic_read(&ip6_dst_ops.entries) < ip6_dst_ops.gc_thresh)
		expire = ip6_rt_gc_timeout>>1;

out:
	expire -= expire>>ip6_rt_gc_elasticity;
	end_bh_atomic();
	return (atomic_read(&ip6_dst_ops.entries) > ip6_rt_max_size);
}

/* Clean host part of a prefix. Not necessary in radix tree,
   but results in cleaner routing tables.

   Remove it only when all the things will work!
 */

static void ipv6_wash_prefix(struct in6_addr *pfx, int plen)
{
	int b = plen&0x7;
	int o = (plen + 7)>>3;

	if (o < 16)
		memset(pfx->s6_addr + o, 0, 16 - o);
	if (b != 0)
		pfx->s6_addr[plen>>3] &= (0xFF<<(8-b));
}

static int ipv6_get_mtu(struct device *dev)
{
	struct inet6_dev *idev;

	idev = ipv6_get_idev(dev);
	if (idev)
		return idev->cnf.mtu6;
	else
		return IPV6_MIN_MTU;
}

static int ipv6_get_hoplimit(struct device *dev)
{
	struct inet6_dev *idev;

	idev = ipv6_get_idev(dev);
	if (idev)
		return idev->cnf.hop_limit;
	else
		return ipv6_devconf.hop_limit;
}

/*
 *
 */

int ip6_route_add(struct in6_rtmsg *rtmsg)
{
	int err;
	struct rt6_info *rt;
	struct device *dev = NULL;
	int addr_type;

	if (rtmsg->rtmsg_dst_len > 128 || rtmsg->rtmsg_src_len > 128)
		return -EINVAL;
#ifndef CONFIG_IPV6_SUBTREES
	if (rtmsg->rtmsg_src_len)
		return -EINVAL;
#endif
	if (rtmsg->rtmsg_metric == 0)
		rtmsg->rtmsg_metric = IP6_RT_PRIO_USER;

	rt = dst_alloc(sizeof(struct rt6_info), &ip6_dst_ops);

	if (rt == NULL)
		return -ENOMEM;

	rt->u.dst.obsolete = -1;
	rt->rt6i_expires = rtmsg->rtmsg_info;

	addr_type = ipv6_addr_type(&rtmsg->rtmsg_dst);

	if (addr_type & IPV6_ADDR_MULTICAST)
		rt->u.dst.input = ip6_mc_input;
	else
		rt->u.dst.input = ip6_forward;

	rt->u.dst.output = ip6_output;

	if (rtmsg->rtmsg_ifindex) {
		dev = dev_get_by_index(rtmsg->rtmsg_ifindex);
		err = -ENODEV;
		if (dev == NULL)
			goto out;
	}

	ipv6_addr_copy(&rt->rt6i_dst.addr, &rtmsg->rtmsg_dst);
	rt->rt6i_dst.plen = rtmsg->rtmsg_dst_len;
	ipv6_wash_prefix(&rt->rt6i_dst.addr, rt->rt6i_dst.plen);

#ifdef CONFIG_IPV6_SUBTREES
	ipv6_addr_copy(&rt->rt6i_src.addr, &rtmsg->rtmsg_src);
	rt->rt6i_src.plen = rtmsg->rtmsg_src_len;
	ipv6_wash_prefix(&rt->rt6i_src.addr, rt->rt6i_src.plen);
#endif

	rt->rt6i_metric = rtmsg->rtmsg_metric;

	/* We cannot add true routes via loopback here,
	   they would result in kernel looping; promote them to reject routes
	 */
	if ((rtmsg->rtmsg_flags&RTF_REJECT) ||
	    (dev && (dev->flags&IFF_LOOPBACK) && !(addr_type&IPV6_ADDR_LOOPBACK))) {
		dev = &loopback_dev;
		rt->u.dst.output = ip6_pkt_discard;
		rt->u.dst.input = ip6_pkt_discard;
		rt->u.dst.error = -ENETUNREACH;
		rt->rt6i_flags = RTF_REJECT|RTF_NONEXTHOP;
		goto install_route;
	}

	if (rtmsg->rtmsg_flags & RTF_GATEWAY) {
		struct in6_addr *gw_addr;
		int gwa_type;

		gw_addr = &rtmsg->rtmsg_gateway;
		ipv6_addr_copy(&rt->rt6i_gateway, &rtmsg->rtmsg_gateway);
		gwa_type = ipv6_addr_type(gw_addr);

		if (gwa_type != (IPV6_ADDR_LINKLOCAL|IPV6_ADDR_UNICAST)) {
			struct rt6_info *grt;

			/* IPv6 strictly inhibits using not link-local
			   addresses as nexthop address.
			   Otherwise, router will not able to send redirects.
			   It is very good, but in some (rare!) curcumstances
			   (SIT, PtP, NBMA NOARP links) it is handy to allow
			   some exceptions. --ANK
			 */
			err = -EINVAL;
			if (!(gwa_type&IPV6_ADDR_UNICAST))
				goto out;

			grt = rt6_lookup(gw_addr, NULL, rtmsg->rtmsg_ifindex, 1);

			err = -EHOSTUNREACH;
			if (grt == NULL)
				goto out;
			if (!(grt->rt6i_flags&RTF_GATEWAY))
				err = 0;
			dev = grt->rt6i_dev;
			dst_release(&grt->u.dst);

			if (err)
				goto out;
		}
		err = -EINVAL;
		if (dev == NULL || (dev->flags&IFF_LOOPBACK))
			goto out;
	}

	err = -ENODEV;
	if (dev == NULL)
		goto out;

	if (rtmsg->rtmsg_flags & (RTF_GATEWAY|RTF_NONEXTHOP)) {
		rt->rt6i_nexthop = ndisc_get_neigh(dev, &rt->rt6i_gateway);
		err = -ENOMEM;
		if (rt->rt6i_nexthop == NULL)
			goto out;
	}

	if (ipv6_addr_is_multicast(&rt->rt6i_dst.addr))
		rt->rt6i_hoplimit = IPV6_DEFAULT_MCASTHOPS;
	else
		rt->rt6i_hoplimit = ipv6_get_hoplimit(dev);
	rt->rt6i_flags = rtmsg->rtmsg_flags;

install_route:
	rt->u.dst.pmtu = ipv6_get_mtu(dev);
	rt->u.dst.rtt = TCP_TIMEOUT_INIT;
	rt->rt6i_dev = dev;
	return rt6_ins(rt);

out:
	dst_free((struct dst_entry *) rt);
	return err;
}

int ip6_del_rt(struct rt6_info *rt)
{
	int err;

	start_bh_atomic();
	rt6_dflt_pointer = NULL;
	err = fib6_del(rt);
	end_bh_atomic();

	return err;
}

int ip6_route_del(struct in6_rtmsg *rtmsg)
{
	struct fib6_node *fn;
	struct rt6_info *rt;
	int err = -ESRCH;

	start_bh_atomic();

	fn = fib6_locate(&ip6_routing_table,
			 &rtmsg->rtmsg_dst, rtmsg->rtmsg_dst_len,
			 &rtmsg->rtmsg_src, rtmsg->rtmsg_src_len);
	
	if (fn) {
		for (rt = fn->leaf; rt; rt = rt->u.next) {
			if (rtmsg->rtmsg_ifindex &&
			    (rt->rt6i_dev == NULL ||
			     rt->rt6i_dev->ifindex != rtmsg->rtmsg_ifindex))
				continue;
			if (rtmsg->rtmsg_flags&RTF_GATEWAY &&
			    ipv6_addr_cmp(&rtmsg->rtmsg_gateway, &rt->rt6i_gateway))
				continue;
			if (rtmsg->rtmsg_metric &&
			    rtmsg->rtmsg_metric != rt->rt6i_metric)
				continue;
			err = ip6_del_rt(rt);
			break;
		}
	}
	end_bh_atomic();

	return err;
}

#ifdef CONFIG_IPV6_NETLINK
/*
 *	NETLINK interface
 *	routing socket moral equivalent
 */

static int rt6_msgrcv(int unit, struct sk_buff *skb)
{
	int count = 0;
	struct in6_rtmsg *rtmsg;
	int err;

	rtnl_lock();
	while (skb->len) {
		if (skb->len < sizeof(struct in6_rtmsg)) {
			count = -EINVAL;
			goto out;
		}
		
		rtmsg = (struct in6_rtmsg *) skb->data;
		skb_pull(skb, sizeof(struct in6_rtmsg));
		count += sizeof(struct in6_rtmsg);

		switch (rtmsg->rtmsg_type) {
		case RTMSG_NEWROUTE:
			err = ip6_route_add(rtmsg);
			break;
		case RTMSG_DELROUTE:
			err = ip6_route_del(rtmsg);
			break;
		default:
			count = -EINVAL;
			goto out;
		};
	}

out:
	rtnl_unlock();
	kfree_skb(skb);	
	return count;
}

static void rt6_sndrtmsg(struct in6_rtmsg *rtmsg)
{
	struct sk_buff *skb;
	
	skb = alloc_skb(sizeof(struct in6_rtmsg), GFP_ATOMIC);
	if (skb == NULL)
		return;

	memcpy(skb_put(skb, sizeof(struct in6_rtmsg)), &rtmsg,
	       sizeof(struct in6_rtmsg));
	
	if (netlink_post(NETLINK_ROUTE6, skb))
		kfree_skb(skb);
}

void rt6_sndmsg(int type, struct in6_addr *dst, struct in6_addr *src,
		struct in6_addr *gw, struct device *dev, 
		int dstlen, int srclen,	int metric, __u32 flags)
{
	struct sk_buff *skb;
	struct in6_rtmsg *msg;
	
	skb = alloc_skb(sizeof(struct in6_rtmsg), GFP_ATOMIC);
	if (skb == NULL)
		return;

	msg = (struct in6_rtmsg *) skb_put(skb, sizeof(struct in6_rtmsg));

	memset(msg, 0, sizeof(struct in6_rtmsg));
	
	msg->rtmsg_type = type;

	if (dst)
		ipv6_addr_copy(&msg->rtmsg_dst, dst);

	if (src) {
		ipv6_addr_copy(&msg->rtmsg_src, src);
		msg->rtmsg_src_len = srclen;
	}

	if (gw)
		ipv6_addr_copy(&msg->rtmsg_gateway, gw);

	msg->rtmsg_dst_len = dstlen;
	msg->rtmsg_metric = metric;

	if (dev)
		msg->rtmsg_ifindex = dev->ifindex;

	msg->rtmsg_flags = flags;

	if (netlink_post(NETLINK_ROUTE6, skb))
		kfree_skb(skb);
}
#endif /* CONFIG_IPV6_NETLINK */

/*
 *	Handle redirects
 */
void rt6_redirect(struct in6_addr *dest, struct in6_addr *saddr,
		  struct neighbour *neigh, int on_link)
{
	struct rt6_info *rt, *nrt;

	/* Locate old route to this destination. */
	rt = rt6_lookup(dest, NULL, neigh->dev->ifindex, 1);

	if (rt == NULL)
		return;

	if (neigh->dev != rt->rt6i_dev)
		goto out;

	/* Redirect received -> path was valid.
	   Look, redirects are sent only in response to data packets,
	   so that this nexthop apparently is reachable. --ANK
	 */
	dst_confirm(&rt->u.dst);

	/* Duplicate redirect: silently ignore. */
	if (neigh == rt->u.dst.neighbour)
		goto out;

	/* Current route is on-link; redirect is always invalid.
	   
	   Seems, previous statement is not true. It could
	   be node, which looks for us as on-link (f.e. proxy ndisc)
	   But then router serving it might decide, that we should
	   know truth 8)8) --ANK (980726).
	 */
	if (!(rt->rt6i_flags&RTF_GATEWAY))
		goto out;

#if !defined(CONFIG_IPV6_EUI64) || defined(CONFIG_IPV6_NO_PB)
	/*
	 *	During transition gateways have more than
	 *	one link local address. Certainly, it is violation
	 *	of basic principles, but it is temparary.
	 */
	/*
	 *	RFC 1970 specifies that redirects should only be
	 *	accepted if they come from the nexthop to the target.
	 *	Due to the way default routers are chosen, this notion
	 *	is a bit fuzzy and one might need to check all default
	 *	routers.
	 */

	if (ipv6_addr_cmp(saddr, &rt->rt6i_gateway)) {
		if (rt->rt6i_flags & RTF_DEFAULT) {
			struct rt6_info *rt1;

			for (rt1 = ip6_routing_table.leaf; rt1; rt1 = rt1->u.next) {
				if (!ipv6_addr_cmp(saddr, &rt1->rt6i_gateway)) {
					dst_clone(&rt1->u.dst);
					dst_release(&rt->u.dst);
					rt = rt1;
					goto source_ok;
				}
			}
		}
		if (net_ratelimit())
			printk(KERN_DEBUG "rt6_redirect: source isn't a valid nexthop "
			       "for redirect target\n");
		goto out;
	}

source_ok:
#endif

	/*
	 *	We have finally decided to accept it.
	 */

	nrt = ip6_rt_copy(rt);
	if (nrt == NULL)
		goto out;

	nrt->rt6i_flags = RTF_GATEWAY|RTF_UP|RTF_DYNAMIC|RTF_CACHE;
	if (on_link)
		nrt->rt6i_flags &= ~RTF_GATEWAY;

	ipv6_addr_copy(&nrt->rt6i_dst.addr, dest);
	nrt->rt6i_dst.plen = 128;

	ipv6_addr_copy(&nrt->rt6i_gateway, (struct in6_addr*)neigh->primary_key);
	nrt->rt6i_nexthop = neigh_clone(neigh);
	/* Reset pmtu, it may be better */
	nrt->u.dst.pmtu = ipv6_get_mtu(neigh->dev);
	nrt->rt6i_hoplimit = ipv6_get_hoplimit(neigh->dev);

	if (rt6_ins(nrt))
		goto out;

	/* Sic! rt6_redirect is called by bh, so that it is allowed */
	dst_release(&rt->u.dst);
	if (rt->rt6i_flags&RTF_CACHE)
		ip6_del_rt(rt);
	return;

out:
        dst_release(&rt->u.dst);
	return;
}

/*
 *	Handle ICMP "packet too big" messages
 *	i.e. Path MTU discovery
 */

void rt6_pmtu_discovery(struct in6_addr *daddr, struct in6_addr *saddr,
			struct device *dev, u32 pmtu)
{
	struct rt6_info *rt, *nrt;

	if (pmtu < IPV6_MIN_MTU) {
		if (net_ratelimit())
			printk(KERN_DEBUG "rt6_pmtu_discovery: invalid MTU value %d\n",
			       pmtu);
		return;
	}

	rt = rt6_lookup(daddr, saddr, dev->ifindex, 0);

	if (rt == NULL)
		return;

	if (pmtu >= rt->u.dst.pmtu)
		goto out;

	/* New mtu received -> path was valid.
	   They are sent only in response to data packets,
	   so that this nexthop apparently is reachable. --ANK
	 */
	dst_confirm(&rt->u.dst);

	/* Host route. If it is static, it would be better
	   not to override it, but add new one, so that
	   when cache entry will expire old pmtu
	   would return automatically.
	 */
	if (rt->rt6i_dst.plen == 128) {
		/*
		 *	host route
		 */
		rt->u.dst.pmtu = pmtu;
		rt->rt6i_flags |= RTF_MODIFIED;
		goto out;
	}

	/* Network route.
	   Two cases are possible:
	   1. It is connected route. Action: COW
	   2. It is gatewayed route or NONEXTHOP route. Action: clone it.
	 */
	if (!rt->rt6i_nexthop && !(rt->rt6i_flags & RTF_NONEXTHOP)) {
		nrt = rt6_cow(rt, daddr, saddr);
		nrt->u.dst.pmtu = pmtu;
		nrt->rt6i_flags |= RTF_DYNAMIC;
		dst_release(&nrt->u.dst);
	} else {
		nrt = ip6_rt_copy(rt);
		if (nrt == NULL)
			goto out;
		ipv6_addr_copy(&nrt->rt6i_dst.addr, daddr);
		nrt->rt6i_dst.plen = 128;
		nrt->rt6i_nexthop = neigh_clone(rt->rt6i_nexthop);
		nrt->rt6i_flags |= (RTF_DYNAMIC | RTF_CACHE);
		nrt->u.dst.pmtu = pmtu;
		rt6_ins(nrt);
	}

out:
	dst_release(&rt->u.dst);
}

/*
 *	Misc support functions
 */

static struct rt6_info * ip6_rt_copy(struct rt6_info *ort)
{
	struct rt6_info *rt;

	rt = dst_alloc(sizeof(struct rt6_info), &ip6_dst_ops);
	
	if (rt) {
		rt->u.dst.input = ort->u.dst.input;
		rt->u.dst.output = ort->u.dst.output;

		rt->u.dst.pmtu = ort->u.dst.pmtu;
		rt->u.dst.rtt = ort->u.dst.rtt;
		rt->u.dst.window = ort->u.dst.window;
		rt->u.dst.mxlock = ort->u.dst.mxlock;
		rt->u.dst.dev = ort->u.dst.dev;
		rt->u.dst.lastuse = jiffies;
		rt->rt6i_hoplimit = ort->rt6i_hoplimit;
		rt->rt6i_expires = 0;

		ipv6_addr_copy(&rt->rt6i_gateway, &ort->rt6i_gateway);
		rt->rt6i_flags = ort->rt6i_flags & ~RTF_EXPIRES;
		rt->rt6i_metric = ort->rt6i_metric;

		memcpy(&rt->rt6i_dst, &ort->rt6i_dst, sizeof(struct rt6key));
#ifdef CONFIG_IPV6_SUBTREES
		memcpy(&rt->rt6i_src, &ort->rt6i_src, sizeof(struct rt6key));
#endif
	}
	return rt;
}

struct rt6_info *rt6_get_dflt_router(struct in6_addr *addr, struct device *dev)
{	
	struct rt6_info *rt;
	struct fib6_node *fn;

	fn = &ip6_routing_table;

	start_bh_atomic();
	for (rt = fn->leaf; rt; rt=rt->u.next) {
		if (dev == rt->rt6i_dev &&
		    ipv6_addr_cmp(&rt->rt6i_gateway, addr) == 0)
			break;
	}
	if (rt)
		dst_clone(&rt->u.dst);
	end_bh_atomic();
	return rt;
}

struct rt6_info *rt6_add_dflt_router(struct in6_addr *gwaddr,
				     struct device *dev)
{
	struct in6_rtmsg rtmsg;

	memset(&rtmsg, 0, sizeof(struct in6_rtmsg));
	rtmsg.rtmsg_type = RTMSG_NEWROUTE;
	ipv6_addr_copy(&rtmsg.rtmsg_gateway, gwaddr);
	rtmsg.rtmsg_metric = 1024;
	rtmsg.rtmsg_flags = RTF_GATEWAY | RTF_ADDRCONF | RTF_DEFAULT | RTF_UP;

	rtmsg.rtmsg_ifindex = dev->ifindex;

	ip6_route_add(&rtmsg);
	return rt6_get_dflt_router(gwaddr, dev);
}

void rt6_purge_dflt_routers(int last_resort)
{
	struct rt6_info *rt;
	u32 flags;

	if (last_resort)
		flags = RTF_ALLONLINK;
	else
		flags = RTF_DEFAULT | RTF_ADDRCONF;	

restart:
	rt6_dflt_pointer = NULL;

	for (rt = ip6_routing_table.leaf; rt; rt = rt->u.next) {
		if (rt->rt6i_flags & flags) {
			ip6_del_rt(rt);
			goto restart;
		}
	}
}

int ipv6_route_ioctl(unsigned int cmd, void *arg)
{
	struct in6_rtmsg rtmsg;
	int err;

	RDBG(("ipv6_route_ioctl(%d,%p)\n", cmd, arg));
	switch(cmd) {
	case SIOCADDRT:		/* Add a route */
	case SIOCDELRT:		/* Delete a route */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		err = copy_from_user(&rtmsg, arg,
				     sizeof(struct in6_rtmsg));
		if (err)
			return -EFAULT;
			
		rtnl_lock();
		switch (cmd) {
		case SIOCADDRT:
			err = ip6_route_add(&rtmsg);
			break;
		case SIOCDELRT:
			err = ip6_route_del(&rtmsg);
			break;
		default:
			err = -EINVAL;
		};
		rtnl_unlock();

#ifdef CONFIG_IPV6_NETLINK
		if (err == 0)
				rt6_sndrtmsg(&rtmsg);
#endif
		return err;
	};

	return -EINVAL;
}

/*
 *	Drop the packet on the floor
 */

int ip6_pkt_discard(struct sk_buff *skb)
{
	ipv6_statistics.Ip6OutNoRoutes++;
	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_ADDR_UNREACH, 0, skb->dev);
	kfree_skb(skb);
	return 0;
}

/*
 *	Add address
 */

int ip6_rt_addr_add(struct in6_addr *addr, struct device *dev)
{
	struct rt6_info *rt;

	rt = dst_alloc(sizeof(struct rt6_info), &ip6_dst_ops);
	if (rt == NULL)
		return -ENOMEM;
	
	rt->u.dst.input = ip6_input;
	rt->u.dst.output = ip6_output;
	rt->rt6i_dev = dev_get("lo");
	rt->u.dst.rtt = TCP_TIMEOUT_INIT;
	rt->u.dst.pmtu = ipv6_get_mtu(rt->rt6i_dev);
	rt->rt6i_hoplimit = ipv6_get_hoplimit(rt->rt6i_dev);
	rt->u.dst.obsolete = -1;

	rt->rt6i_flags = RTF_UP | RTF_NONEXTHOP;
	rt->rt6i_nexthop = ndisc_get_neigh(rt->rt6i_dev, &rt->rt6i_gateway);
	if (rt->rt6i_nexthop == NULL) {
		dst_free((struct dst_entry *) rt);
		return -ENOMEM;
	}

	ipv6_addr_copy(&rt->rt6i_dst.addr, addr);
	rt->rt6i_dst.plen = 128;
	rt6_ins(rt);

	return 0;
}

/* Delete address. Warning: you should check that this address
   disappeared before calling this function.
 */

int ip6_rt_addr_del(struct in6_addr *addr, struct device *dev)
{
	struct rt6_info *rt;
	int err = -ENOENT;

	rt = rt6_lookup(addr, NULL, loopback_dev.ifindex, 1);
	if (rt) {
		if (rt->rt6i_dst.plen == 128)
			err= ip6_del_rt(rt);
		dst_release(&rt->u.dst);
	}

	return err;
}

#ifdef CONFIG_RT6_POLICY

static int rt6_flow_match_in(struct rt6_info *rt, struct sk_buff *skb)
{
	struct flow_filter *frule;
	struct pkt_filter *filter;
	int res = 1;

	if ((frule = rt->rt6i_filter) == NULL)
		goto out;

	if (frule->type != FLR_INPUT) {
		res = 0;
		goto out;
	}

	for (filter = frule->u.filter; filter; filter = filter->next) {
		__u32 *word;

		word = (__u32 *) skb->h.raw;
		word += filter->offset;

		if ((*word ^ filter->value) & filter->mask) {
			res = 0;
			break;
		}
	}

out:
	return res;
}

static int rt6_flow_match_out(struct rt6_info *rt, struct sock *sk)
{
	struct flow_filter *frule;
	int res = 1;

	if ((frule = rt->rt6i_filter) == NULL)
		goto out;

	if (frule->type != FLR_INPUT) {
		res = 0;
		goto out;
	}

	if (frule->u.sk != sk)
		res = 0;
out:
	return res;
}

static struct rt6_info *rt6_flow_lookup(struct rt6_info *rt,
					struct in6_addr *daddr,
					struct in6_addr *saddr,
					struct fl_acc_args *args)
{
	struct flow_rule *frule;
	struct rt6_info *nrt = NULL;
	struct pol_chain *pol;

	for (pol = rt6_pol_list; pol; pol = pol->next) {
		struct fib6_node *fn;
		struct rt6_info *sprt;

		fn = fib6_lookup(pol->rules, daddr, saddr);

		do {
			for (sprt = fn->leaf; sprt; sprt=sprt->u.next) {
				int res;

				frule = sprt->rt6i_flowr;
#if RT6_DEBUG >= 2
				if (frule == NULL) {
					printk(KERN_DEBUG "NULL flowr\n");
					goto error;
				}
#endif
				res = frule->ops->accept(rt, sprt, args, &nrt);

				switch (res) {
				case FLOWR_SELECT:
					goto found;
				case FLOWR_CLEAR:
					goto next_policy;
				case FLOWR_NODECISION:
					break;
				default:
					goto error;
				};
			}

			fn = fn->parent;

		} while ((fn->fn_flags & RTN_TL_ROOT) == 0);

	next_policy:
	}

error:
	dst_clone(&ip6_null_entry.u.dst);
	return &ip6_null_entry;

found:
	if (nrt == NULL)
		goto error;

	nrt->rt6i_flags |= RTF_CACHE;
	dst_clone(&nrt->u.dst);
	err = rt6_ins(nrt);
	if (err)
		nrt->u.dst.error = err;
	return nrt;
}
#endif

static int fib6_ifdown(struct rt6_info *rt, void *arg)
{
	if (((void*)rt->rt6i_dev == arg || arg == NULL) &&
	    rt != &ip6_null_entry) {
		RT6_TRACE("deleted by ifdown %p\n", rt);
		return -1;
	}
	return 0;
}

void rt6_ifdown(struct device *dev)
{
	fib6_clean_tree(&ip6_routing_table, fib6_ifdown, 0, dev);
}

struct rt6_mtu_change_arg
{
	struct device *dev;
	unsigned mtu;
};

static int rt6_mtu_change_route(struct rt6_info *rt, void *p_arg)
{
	struct rt6_mtu_change_arg *arg = (struct rt6_mtu_change_arg *) p_arg;

	/* In IPv6 pmtu discovery is not optional,
	   so that RTAX_MTU lock cannot dissable it.
	   We still use this lock to block changes
	   caused by addrconf/ndisc.
	   */
	if (rt->rt6i_dev == arg->dev &&
	    !(rt->u.dst.mxlock&(1<<RTAX_MTU)))
		rt->u.dst.pmtu = arg->mtu;
	return 0;
}

void rt6_mtu_change(struct device *dev, unsigned mtu)
{
	struct rt6_mtu_change_arg arg;

	arg.dev = dev;
	arg.mtu = mtu;
	fib6_clean_tree(&ip6_routing_table, rt6_mtu_change_route, 0, &arg);
}

#ifdef CONFIG_RTNETLINK

static int inet6_rtm_to_rtmsg(struct rtmsg *r, struct rtattr **rta,
			      struct in6_rtmsg *rtmsg)
{
	memset(rtmsg, 0, sizeof(*rtmsg));

	rtmsg->rtmsg_dst_len = r->rtm_dst_len;
	rtmsg->rtmsg_src_len = r->rtm_src_len;
	rtmsg->rtmsg_flags = RTF_UP;
	if (r->rtm_type == RTN_UNREACHABLE)
		rtmsg->rtmsg_flags |= RTF_REJECT;

	if (rta[RTA_GATEWAY-1]) {
		if (rta[RTA_GATEWAY-1]->rta_len != RTA_LENGTH(16))
			return -EINVAL;
		memcpy(&rtmsg->rtmsg_gateway, RTA_DATA(rta[RTA_GATEWAY-1]), 16);
		rtmsg->rtmsg_flags |= RTF_GATEWAY;
	}
	if (rta[RTA_DST-1]) {
		if (RTA_PAYLOAD(rta[RTA_DST-1]) < ((r->rtm_dst_len+7)>>3))
			return -EINVAL;
		memcpy(&rtmsg->rtmsg_dst, RTA_DATA(rta[RTA_DST-1]), ((r->rtm_dst_len+7)>>3));
	}
	if (rta[RTA_SRC-1]) {
		if (RTA_PAYLOAD(rta[RTA_SRC-1]) < ((r->rtm_src_len+7)>>3))
			return -EINVAL;
		memcpy(&rtmsg->rtmsg_src, RTA_DATA(rta[RTA_SRC-1]), ((r->rtm_src_len+7)>>3));
	}
	if (rta[RTA_OIF-1]) {
		if (rta[RTA_OIF-1]->rta_len != RTA_LENGTH(sizeof(int)))
			return -EINVAL;
		memcpy(&rtmsg->rtmsg_ifindex, RTA_DATA(rta[RTA_OIF-1]), sizeof(int));
	}
	if (rta[RTA_PRIORITY-1]) {
		if (rta[RTA_PRIORITY-1]->rta_len != RTA_LENGTH(4))
			return -EINVAL;
		memcpy(&rtmsg->rtmsg_metric, RTA_DATA(rta[RTA_PRIORITY-1]), 4);
	}
	return 0;
}

int inet6_rtm_delroute(struct sk_buff *skb, struct nlmsghdr* nlh, void *arg)
{
	struct rtmsg *r = NLMSG_DATA(nlh);
	struct in6_rtmsg rtmsg;

	if (inet6_rtm_to_rtmsg(r, arg, &rtmsg))
		return -EINVAL;
	return ip6_route_del(&rtmsg);
}

int inet6_rtm_newroute(struct sk_buff *skb, struct nlmsghdr* nlh, void *arg)
{
	struct rtmsg *r = NLMSG_DATA(nlh);
	struct in6_rtmsg rtmsg;

	if (inet6_rtm_to_rtmsg(r, arg, &rtmsg))
		return -EINVAL;
	return ip6_route_add(&rtmsg);
}

struct rt6_rtnl_dump_arg
{
	struct sk_buff *skb;
	struct netlink_callback *cb;
};

static int rt6_fill_node(struct sk_buff *skb, struct rt6_info *rt,
			 struct in6_addr *dst,
			 struct in6_addr *src,
			 int iif,
			 int type, u32 pid, u32 seq)
{
	struct rtmsg *rtm;
	struct nlmsghdr  *nlh;
	unsigned char	 *b = skb->tail;
	struct rtattr *mx;
	struct rta_cacheinfo ci;

	nlh = NLMSG_PUT(skb, pid, seq, type, sizeof(*rtm));
	rtm = NLMSG_DATA(nlh);
	rtm->rtm_family = AF_INET6;
	rtm->rtm_dst_len = rt->rt6i_dst.plen;
	rtm->rtm_src_len = rt->rt6i_src.plen;
	rtm->rtm_tos = 0;
	rtm->rtm_table = RT_TABLE_MAIN;
	if (rt->rt6i_flags&RTF_REJECT)
		rtm->rtm_type = RTN_UNREACHABLE;
	else if (rt->rt6i_dev && (rt->rt6i_dev->flags&IFF_LOOPBACK))
		rtm->rtm_type = RTN_LOCAL;
	else
		rtm->rtm_type = RTN_UNICAST;
	rtm->rtm_flags = 0;
	rtm->rtm_scope = RT_SCOPE_UNIVERSE;
	rtm->rtm_protocol = RTPROT_BOOT;
	if (rt->rt6i_flags&RTF_DYNAMIC)
		rtm->rtm_protocol = RTPROT_REDIRECT;
	else if (rt->rt6i_flags&(RTF_ADDRCONF|RTF_ALLONLINK))
		rtm->rtm_protocol = RTPROT_KERNEL;
	else if (rt->rt6i_flags&RTF_DEFAULT)
		rtm->rtm_protocol = RTPROT_RA;

	if (rt->rt6i_flags&RTF_CACHE)
		rtm->rtm_flags |= RTM_F_CLONED;

	if (dst) {
		RTA_PUT(skb, RTA_DST, 16, dst);
	        rtm->rtm_dst_len = 128;
	} else if (rtm->rtm_dst_len)
		RTA_PUT(skb, RTA_DST, 16, &rt->rt6i_dst.addr);
#ifdef CONFIG_IPV6_SUBTREES
	if (src) {
		RTA_PUT(skb, RTA_SRC, 16, src);
	        rtm->rtm_src_len = 128;
	} else if (rtm->rtm_src_len)
		RTA_PUT(skb, RTA_SRC, 16, &rt->rt6i_src.addr);
#endif
	if (iif)
		RTA_PUT(skb, RTA_IIF, 4, &iif);
	else if (dst) {
		struct inet6_ifaddr *ifp = ipv6_get_saddr(&rt->u.dst, dst);
		if (ifp)
			RTA_PUT(skb, RTA_PREFSRC, 16, &ifp->addr);
	}
	mx = (struct rtattr*)skb->tail;
	RTA_PUT(skb, RTA_METRICS, 0, NULL);
	if (rt->u.dst.mxlock)
		RTA_PUT(skb, RTAX_LOCK, sizeof(unsigned), &rt->u.dst.mxlock);
	if (rt->u.dst.pmtu)
		RTA_PUT(skb, RTAX_MTU, sizeof(unsigned), &rt->u.dst.pmtu);
	if (rt->u.dst.window)
		RTA_PUT(skb, RTAX_WINDOW, sizeof(unsigned), &rt->u.dst.window);
	if (rt->u.dst.rtt)
		RTA_PUT(skb, RTAX_RTT, sizeof(unsigned), &rt->u.dst.rtt);
	mx->rta_len = skb->tail - (u8*)mx;
	if (mx->rta_len == RTA_LENGTH(0))
		skb_trim(skb, (u8*)mx - skb->data);
	if (rt->u.dst.neighbour)
		RTA_PUT(skb, RTA_GATEWAY, 16, &rt->u.dst.neighbour->primary_key);
	if (rt->u.dst.dev)
		RTA_PUT(skb, RTA_OIF, sizeof(int), &rt->rt6i_dev->ifindex);
	RTA_PUT(skb, RTA_PRIORITY, 4, &rt->rt6i_metric);
	ci.rta_lastuse = jiffies - rt->u.dst.lastuse;
	if (rt->rt6i_expires)
		ci.rta_expires = rt->rt6i_expires - jiffies;
	else
		ci.rta_expires = 0;
	ci.rta_used = atomic_read(&rt->u.dst.refcnt);
	ci.rta_clntref = atomic_read(&rt->u.dst.use);
	ci.rta_error = rt->u.dst.error;
	RTA_PUT(skb, RTA_CACHEINFO, sizeof(ci), &ci);
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int rt6_dump_route(struct rt6_info *rt, void *p_arg)
{
	struct rt6_rtnl_dump_arg *arg = (struct rt6_rtnl_dump_arg *) p_arg;

	return rt6_fill_node(arg->skb, rt, NULL, NULL, 0, RTM_NEWROUTE,
			     NETLINK_CB(arg->cb->skb).pid, arg->cb->nlh->nlmsg_seq);
}

static int fib6_dump_node(struct fib6_walker_t *w)
{
	int res;
	struct rt6_info *rt;

	for (rt = w->leaf; rt; rt = rt->u.next) {
		res = rt6_dump_route(rt, w->args);
		if (res < 0) {
			/* Frame is full, suspend walking */
			w->leaf = rt;
			return 1;
		}
		BUG_TRAP(res!=0);
	}
	w->leaf = NULL;
	return 0;
}

static int fib6_dump_done(struct netlink_callback *cb)
{
	struct fib6_walker_t *w = (void*)cb->args[0];

	if (w) {
		cb->args[0] = 0;
		start_bh_atomic();
		fib6_walker_unlink(w);
		end_bh_atomic();
		kfree(w);
	}
	if (cb->args[1]) {
		cb->done = (void*)cb->args[1];
		cb->args[1] = 0;
	}
	return cb->done(cb);
}

int inet6_dump_fib(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct rt6_rtnl_dump_arg arg;
	struct fib6_walker_t *w;
	int res;

	arg.skb = skb;
	arg.cb = cb;

	w = (void*)cb->args[0];
	if (w == NULL) {
		/* New dump:
		 * 
		 * 1. hook callback destructor.
		 */
		cb->args[1] = (long)cb->done;
		cb->done = fib6_dump_done;

		/*
		 * 2. allocate and initialize walker.
		 */
		w = kmalloc(sizeof(*w), GFP_KERNEL);
		if (w == NULL)
			return -ENOMEM;
		RT6_TRACE("dump<%p", w);
		memset(w, 0, sizeof(*w));
		w->root = &ip6_routing_table;
		w->func = fib6_dump_node;
		w->args = &arg;
		cb->args[0] = (long)w;
		start_bh_atomic();
		res = fib6_walk(w);
		end_bh_atomic();
	} else {
		w->args = &arg;
		start_bh_atomic();
		res = fib6_walk_continue(w);
		end_bh_atomic();
	}
#if RT6_DEBUG >= 3
	if (res <= 0 && skb->len == 0)
		RT6_TRACE("%p>dump end\n", w);
#endif
	/* res < 0 is an error. (really, impossible)
	   res == 0 means that dump is complete, but skb still can contain data.
	   res > 0 dump is not complete, but frame is full.
	 */
	return res < 0 ? res : skb->len;
}

int inet6_rtm_getroute(struct sk_buff *in_skb, struct nlmsghdr* nlh, void *arg)
{
	struct rtattr **rta = arg;
	int iif = 0;
	int err;
	struct sk_buff *skb;
	struct flowi fl;
	struct rt6_info *rt;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		return -ENOBUFS;

	/* Reserve room for dummy headers, this skb can pass
	   through good chunk of routing engine.
	 */
	skb->mac.raw = skb->data;
	skb_reserve(skb, MAX_HEADER + sizeof(struct ipv6hdr));

	fl.proto = 0;
	fl.nl_u.ip6_u.daddr = NULL;
	fl.nl_u.ip6_u.saddr = NULL;
	fl.uli_u.icmpt.type = 0;
	fl.uli_u.icmpt.code = 0;
	if (rta[RTA_SRC-1])
		fl.nl_u.ip6_u.saddr = (struct in6_addr*)RTA_DATA(rta[RTA_SRC-1]);
	if (rta[RTA_DST-1])
		fl.nl_u.ip6_u.daddr = (struct in6_addr*)RTA_DATA(rta[RTA_DST-1]);

	if (rta[RTA_IIF-1])
		memcpy(&iif, RTA_DATA(rta[RTA_IIF-1]), sizeof(int));

	if (iif) {
		struct device *dev;
		dev = dev_get_by_index(iif);
		if (!dev)
			return -ENODEV;
	}

	fl.oif = 0;
	if (rta[RTA_OIF-1])
		memcpy(&fl.oif, RTA_DATA(rta[RTA_OIF-1]), sizeof(int));

	rt = (struct rt6_info*)ip6_route_output(NULL, &fl);

	skb->dst = &rt->u.dst;

	NETLINK_CB(skb).dst_pid = NETLINK_CB(in_skb).pid;
	err = rt6_fill_node(skb, rt, 
			    fl.nl_u.ip6_u.daddr,
			    fl.nl_u.ip6_u.saddr,
			    iif,
			    RTM_NEWROUTE, NETLINK_CB(in_skb).pid, nlh->nlmsg_seq);
	if (err < 0)
		return -EMSGSIZE;

	err = netlink_unicast(rtnl, skb, NETLINK_CB(in_skb).pid, MSG_DONTWAIT);
	if (err < 0)
		return err;
	return 0;
}

void inet6_rt_notify(int event, struct rt6_info *rt)
{
	struct sk_buff *skb;
	int size = NLMSG_SPACE(sizeof(struct rtmsg)+256);

	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb) {
		netlink_set_err(rtnl, 0, RTMGRP_IPV6_ROUTE, ENOBUFS);
		return;
	}
	if (rt6_fill_node(skb, rt, NULL, NULL, 0, event, 0, 0) < 0) {
		kfree_skb(skb);
		netlink_set_err(rtnl, 0, RTMGRP_IPV6_ROUTE, EINVAL);
		return;
	}
	NETLINK_CB(skb).dst_groups = RTMGRP_IPV6_ROUTE;
	netlink_broadcast(rtnl, skb, 0, RTMGRP_IPV6_ROUTE, GFP_ATOMIC);
}

#endif

/*
 *	/proc
 */

#ifdef CONFIG_PROC_FS

#define RT6_INFO_LEN (32 + 4 + 32 + 4 + 32 + 40 + 5 + 1)

struct rt6_proc_arg
{
	char *buffer;
	int offset;
	int length;
	int skip;
	int len;
};

static int rt6_info_route(struct rt6_info *rt, void *p_arg)
{
	struct rt6_proc_arg *arg = (struct rt6_proc_arg *) p_arg;
	int i;

	if (arg->skip < arg->offset / RT6_INFO_LEN) {
		arg->skip++;
		return 0;
	}

	if (arg->len >= arg->length)
		return 0;

	for (i=0; i<16; i++) {
		sprintf(arg->buffer + arg->len, "%02x",
			rt->rt6i_dst.addr.s6_addr[i]);
		arg->len += 2;
	}
	arg->len += sprintf(arg->buffer + arg->len, " %02x ",
			    rt->rt6i_dst.plen);

#ifdef CONFIG_IPV6_SUBTREES
	for (i=0; i<16; i++) {
		sprintf(arg->buffer + arg->len, "%02x",
			rt->rt6i_src.addr.s6_addr[i]);
		arg->len += 2;
	}
	arg->len += sprintf(arg->buffer + arg->len, " %02x ",
			    rt->rt6i_src.plen);
#else
	sprintf(arg->buffer + arg->len,
		"00000000000000000000000000000000 00 ");
	arg->len += 36;
#endif

	if (rt->rt6i_nexthop) {
		for (i=0; i<16; i++) {
			sprintf(arg->buffer + arg->len, "%02x",
				rt->rt6i_nexthop->primary_key[i]);
			arg->len += 2;
		}
	} else {
		sprintf(arg->buffer + arg->len,
			"00000000000000000000000000000000");
		arg->len += 32;
	}
	arg->len += sprintf(arg->buffer + arg->len,
			    " %08x %08x %08x %08x %8s\n",
			    rt->rt6i_metric, atomic_read(&rt->u.dst.use),
			    atomic_read(&rt->u.dst.refcnt), rt->rt6i_flags, 
			    rt->rt6i_dev ? rt->rt6i_dev->name : "");
	return 0;
}

static int rt6_proc_info(char *buffer, char **start, off_t offset, int length,
			 int dummy)
{
	struct rt6_proc_arg arg;
	arg.buffer = buffer;
	arg.offset = offset;
	arg.length = length;
	arg.skip = 0;
	arg.len = 0;

	fib6_clean_tree(&ip6_routing_table, rt6_info_route, 0, &arg);

	*start = buffer;
	if (offset)
		*start += offset % RT6_INFO_LEN;

	arg.len -= offset % RT6_INFO_LEN;

	if (arg.len > length)
		arg.len = length;
	if (arg.len < 0)
		arg.len = 0;

	return arg.len;
}

extern struct rt6_statistics rt6_stats;

static int rt6_proc_stats(char *buffer, char **start, off_t offset, int length,
			  int dummy)
{
	int len;

	len = sprintf(buffer, "%04x %04x %04x %04x %04x %04x\n",
		      rt6_stats.fib_nodes, rt6_stats.fib_route_nodes,
		      rt6_stats.fib_rt_alloc, rt6_stats.fib_rt_entries,
		      rt6_stats.fib_rt_cache,
		      atomic_read(&ip6_dst_ops.entries));

	len -= offset;

	if (len > length)
		len = length;
	if(len < 0)
		len = 0;

	*start = buffer + offset;

	return len;
}

static struct proc_dir_entry proc_rt6_info = {
	PROC_NET_RT6, 10, "ipv6_route",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	rt6_proc_info
};
static struct proc_dir_entry proc_rt6_stats = {
	PROC_NET_RT6_STATS, 9, "rt6_stats",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	rt6_proc_stats
};
#endif	/* CONFIG_PROC_FS */

#ifdef CONFIG_SYSCTL

static int flush_delay;

static
int ipv6_sysctl_rtcache_flush(ctl_table *ctl, int write, struct file * filp,
			      void *buffer, size_t *lenp)
{
	if (write) {
		proc_dointvec(ctl, write, filp, buffer, lenp);
		if (flush_delay < 0)
			flush_delay = 0;
		start_bh_atomic();
		fib6_run_gc((unsigned long)flush_delay);
		end_bh_atomic();
		return 0;
	} else
		return -EINVAL;
}

ctl_table ipv6_route_table[] = {
        {NET_IPV6_ROUTE_FLUSH, "flush",
         &flush_delay, sizeof(int), 0644, NULL,
         &ipv6_sysctl_rtcache_flush},
	{NET_IPV6_ROUTE_GC_THRESH, "gc_thresh",
         &ip6_dst_ops.gc_thresh, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV6_ROUTE_MAX_SIZE, "max_size",
         &ip6_rt_max_size, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV6_ROUTE_GC_MIN_INTERVAL, "gc_min_interval",
         &ip6_rt_gc_min_interval, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies},
	{NET_IPV6_ROUTE_GC_TIMEOUT, "gc_timeout",
         &ip6_rt_gc_timeout, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies},
	{NET_IPV6_ROUTE_GC_INTERVAL, "gc_interval",
         &ip6_rt_gc_interval, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies},
	{NET_IPV6_ROUTE_GC_ELASTICITY, "gc_elasticity",
         &ip6_rt_gc_elasticity, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies},
	 {0}
};

#endif


__initfunc(void ip6_route_init(void))
{
#ifdef 	CONFIG_PROC_FS
	proc_net_register(&proc_rt6_info);
	proc_net_register(&proc_rt6_stats);
#endif
#ifdef CONFIG_IPV6_NETLINK
	netlink_attach(NETLINK_ROUTE6, rt6_msgrcv);
#endif
}

#ifdef MODULE
void ip6_route_cleanup(void)
{
#ifdef CONFIG_PROC_FS
	proc_net_unregister(PROC_NET_RT6);
	proc_net_unregister(PROC_NET_RT6_STATS);
#endif
#ifdef CONFIG_IPV6_NETLINK
	netlink_detach(NETLINK_ROUTE6);
#endif
	rt6_ifdown(NULL);
	fib6_gc_cleanup();
}
#endif	/* MODULE */

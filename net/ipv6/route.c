/*
 *	Linux INET6 implementation
 *	FIB front-end.
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: route.c,v 1.32 1998/07/25 23:28:52 davem Exp $
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
#else
#define RDBG(x)
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
	{{NULL, ATOMIC_INIT(1), ATOMIC_INIT(1), NULL,
	  -1, 0, 0, 0, 0, 0, 0, 0, 0,
	  -ENETUNREACH, NULL, NULL,
	  ip6_pkt_discard, ip6_pkt_discard,
#ifdef CONFIG_NET_CLS_ROUTE
	  0,
#endif
	  &ip6_dst_ops}},
	NULL, {{{0}}}, 256, RTF_REJECT|RTF_NONEXTHOP, ~0U,
	255, 0, {NULL}, {{{{0}}}, 0}, {{{{0}}}, 0}
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

static atomic_t	rt6_tbl_lock	= ATOMIC_INIT(0);
static int	rt6_bh_mask	= 0;

#define RT_BH_REQUEST		1
#define RT_BH_GC		2

static void __rt6_run_bh(void);

/*
 *	request queue operations
 *	FIFO queue/dequeue
 */

static struct rt6_req request_queue = {
	0, NULL, &request_queue, &request_queue
};

static __inline__ void rtreq_queue(struct rt6_req * req)
{
	unsigned long flags;
	struct rt6_req *next = &request_queue;

	save_flags(flags);
	cli();

	req->prev = next->prev;
	req->prev->next = req;
	next->prev = req;
	req->next = next;
	restore_flags(flags);
}

static __inline__ struct rt6_req * rtreq_dequeue(void)
{
	struct rt6_req *next = &request_queue;
	struct rt6_req *head;

	head = next->next;

	if (head == next)
		return NULL;

	head->next->prev = head->prev;
	next->next = head->next;

	head->next = NULL;
	head->prev = NULL;

	return head;
}

void rtreq_add(struct rt6_info *rt, int operation)
{
	struct rt6_req *rtreq;

	rtreq = kmalloc(sizeof(struct rt6_req), GFP_ATOMIC);
	
	if (rtreq == NULL)
		return;

	memset(rtreq, 0, sizeof(struct rt6_req));

	rtreq->operation = operation;
	rtreq->ptr = rt;
	rtreq_queue(rtreq);

	rt6_bh_mask |= RT_BH_REQUEST;
}

static __inline__ void rt6_lock(void)
{
	atomic_inc(&rt6_tbl_lock);
}

static __inline__ void rt6_unlock(void)
{
	if (atomic_dec_and_test(&rt6_tbl_lock) && rt6_bh_mask) {
		start_bh_atomic();
		__rt6_run_bh();
		end_bh_atomic();
	}
}

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
			if (sprt->rt6i_dev) {
				if (sprt->rt6i_dev->ifindex == oif)
					return sprt;
				if (sprt->rt6i_dev->flags&IFF_LOOPBACK)
					local = sprt;
			}
		}

		if (local)
			return local;

		if (strict) {
			RDBG(("nomatch & STRICT --> ip6_null_entry\n"));
			return &ip6_null_entry;
		}
	}
	RDBG(("!dev or (no match and !strict) --> rt(%p)\n", rt));
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

			if (oif && sprt->rt6i_dev && sprt->rt6i_dev->ifindex == oif) {
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
			    int oif, int flags)
{
	struct fib6_node *fn;
	struct rt6_info *rt;

	rt6_lock();
	fn = fib6_lookup(&ip6_routing_table, daddr, saddr);
	rt = rt6_device_match(fn->leaf, oif, flags&RTF_LINKRT);
	rt6_unlock();
	return rt;
}

static struct rt6_info *rt6_cow(struct rt6_info *ort, struct in6_addr *daddr,
				struct in6_addr *saddr)
{
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

		if (rt->rt6i_src.plen) {
			ipv6_addr_copy(&rt->rt6i_src.addr, saddr);
			rt->rt6i_src.plen = 128;
		}

		rt->rt6i_nexthop = ndisc_get_neigh(rt->rt6i_dev, &rt->rt6i_gateway);

		rtreq_add(rt, RT_OPER_ADD);
	} else {
		rt = &ip6_null_entry;
	}
	return rt;
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

void ip6_route_input(struct sk_buff *skb)
{
	struct fib6_node *fn;
	struct rt6_info *rt;
	struct dst_entry *dst;

	RDBG(("ip6_route_input(%p) from %p\n", skb, __builtin_return_address(0)));
	if ((dst = skb->dst) != NULL)
		goto looped_back;
	rt6_lock();
	fn = fib6_lookup(&ip6_routing_table, &skb->nh.ipv6h->daddr,
			 &skb->nh.ipv6h->saddr);

	rt = fn->leaf;

	if ((rt->rt6i_flags & RTF_CACHE)) {
		if (ip6_rt_policy == 0) {
			rt = rt6_device_match(rt, skb->dev->ifindex, 0);
			goto out;
		}

#ifdef CONFIG_RT6_POLICY
		if ((rt->rt6i_flags & RTF_FLOW)) {
			struct rt6_info *sprt;

			for (sprt = rt; sprt; sprt = sprt->u.next) {
				if (rt6_flow_match_in(sprt, skb)) {
					rt = sprt;
					goto out;
				}
			}
		}
#endif
	}

	rt = rt6_device_match(rt, skb->dev->ifindex, 0);

	if (ip6_rt_policy == 0) {
		if (!rt->rt6i_nexthop && rt->rt6i_dev &&
		    ((rt->rt6i_flags & RTF_NONEXTHOP) == 0)) {
			rt = rt6_cow(rt, &skb->nh.ipv6h->daddr,
				     &skb->nh.ipv6h->saddr);
		}
	} else {
#ifdef CONFIG_RT6_POLICY
		rt = rt6_flow_lookup_in(rt, skb);
#endif
	}

out:
	dst = dst_clone((struct dst_entry *) rt);
	rt6_unlock();

	skb->dst = dst;
looped_back:
	dst->input(skb);
}

struct dst_entry * ip6_route_output(struct sock *sk, struct flowi *fl)
{
	struct fib6_node *fn;
	struct rt6_info *rt;
	struct dst_entry *dst;
	int strict;

	strict = ipv6_addr_type(fl->nl_u.ip6_u.daddr) & (IPV6_ADDR_MULTICAST|IPV6_ADDR_LINKLOCAL);

	rt6_lock();
	fn = fib6_lookup(&ip6_routing_table, fl->nl_u.ip6_u.daddr,
			 fl->nl_u.ip6_u.saddr);

restart:
	rt = fn->leaf;

	if ((rt->rt6i_flags & RTF_CACHE)) {
		RDBG(("RTF_CACHE "));
		if (ip6_rt_policy == 0) {
			rt = rt6_device_match(rt, fl->oif, strict);

			/* BUGGGG! It is capital bug, that was hidden
			   by not-cloning multicast routes. However,
			   the same problem was with link-local addresses.
			   Fix is the following if-statement,
			   but it will not properly handle Pedro's subtrees --ANK
			 */
			if (rt == &ip6_null_entry && strict) {
				while ((fn = fn->parent) != NULL) {
					if (fn->fn_flags & RTN_ROOT)
						goto out;
					if (fn->fn_flags & RTN_RTINFO)
						goto restart;
				}
			}
			RDBG(("devmatch(%p) ", rt));
			goto out;
		}

#ifdef CONFIG_RT6_POLICY
		if ((rt->rt6i_flags & RTF_FLOW)) {
			struct rt6_info *sprt;

			for (sprt = rt; sprt; sprt = sprt->u.next) {
				if (rt6_flow_match_out(sprt, sk)) {
					rt = sprt;
					goto out;
				}
			}
		}
#endif
	}
	RDBG(("!RTF_CACHE "));
	if (rt->rt6i_flags & RTF_DEFAULT) {
		RDBG(("RTF_DEFAULT "));
		if (rt->rt6i_metric >= IP6_RT_PRIO_ADDRCONF) {
			rt = rt6_best_dflt(rt, fl->oif);
			RDBG(("best_dflt(%p) ", rt));
		}
	} else {
		rt = rt6_device_match(rt, fl->oif, strict);
		RDBG(("!RTF_DEFAULT devmatch(%p) ", rt));
	}

	if (ip6_rt_policy == 0) {
		if (!rt->rt6i_nexthop && rt->rt6i_dev &&
		    ((rt->rt6i_flags & RTF_NONEXTHOP) == 0)) {
			rt = rt6_cow(rt, fl->nl_u.ip6_u.daddr,
				     fl->nl_u.ip6_u.saddr);
			RDBG(("(!nhop&&rt6i_dev&&!RTF_NONEXTHOP) cow(%p) ", rt));
		}
	} else {
#ifdef CONFIG_RT6_POLICY
		rt = rt6_flow_lookup_out(rt, sk, fl);
#endif
	}

out:
	dst = dst_clone((struct dst_entry *) rt);
	rt6_unlock();
	RDBG(("dclone/ret(%p)\n", dst));
	return dst;
}


static void rt6_ins(struct rt6_info *rt)
{
	start_bh_atomic();
	if (atomic_read(&rt6_tbl_lock) == 1)
		fib6_add(&ip6_routing_table, rt);
	else
		rtreq_add(rt, RT_OPER_ADD);
	end_bh_atomic();
}

/*
 *	Destination cache support functions
 *
 *	BUGGG! This function is absolutely wrong.
 *	First of all it is never called. (look at include/net/dst.h)
 *	Second, even when it is called rt->rt6i_node == NULL
 *	  ** partially fixed: now dst->obsolete = -1 for IPv6 not cache routes.
 *	Third, even we fixed previous bugs,
 *	it will not work because sernum is incorrectly checked/updated and
 *	it does not handle change of the parent of cloned route.
 *	Purging stray clones is not easy task, it would require
 *	massive remake of ip6_fib.c. Alas...
 *							--ANK
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
		return 576;
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

struct rt6_info *ip6_route_add(struct in6_rtmsg *rtmsg, int *err)
{
	struct rt6_info *rt;
	struct device *dev = NULL;
	int addr_type;
	
	if (rtmsg->rtmsg_dst_len > 128 || rtmsg->rtmsg_src_len > 128) {
		*err = -EINVAL;
		return NULL;
	}
	if (rtmsg->rtmsg_metric == 0)
		rtmsg->rtmsg_metric = IP6_RT_PRIO_USER;

	*err = 0;
	
	rt = dst_alloc(sizeof(struct rt6_info), &ip6_dst_ops);

	if (rt == NULL) {
		RDBG(("dalloc fails, "));
		*err = -ENOMEM;
		return NULL;
	}

	rt->u.dst.obsolete = -1;
	rt->rt6i_expires = rtmsg->rtmsg_info;

	addr_type = ipv6_addr_type(&rtmsg->rtmsg_dst);

	if (addr_type & IPV6_ADDR_MULTICAST) {
		RDBG(("MCAST, "));
		rt->u.dst.input = ip6_mc_input;
	} else {
		RDBG(("!MCAST "));
		rt->u.dst.input = ip6_forward;
	}

	rt->u.dst.output = ip6_output;

	if (rtmsg->rtmsg_ifindex) {
		dev = dev_get_by_index(rtmsg->rtmsg_ifindex);
		if (dev == NULL) {
			*err = -ENODEV;
			goto out;
		}
	}

	ipv6_addr_copy(&rt->rt6i_dst.addr, &rtmsg->rtmsg_dst);
	rt->rt6i_dst.plen = rtmsg->rtmsg_dst_len;
	ipv6_wash_prefix(&rt->rt6i_dst.addr, rt->rt6i_dst.plen);

	ipv6_addr_copy(&rt->rt6i_src.addr, &rtmsg->rtmsg_src);
	rt->rt6i_src.plen = rtmsg->rtmsg_src_len;
	ipv6_wash_prefix(&rt->rt6i_src.addr, rt->rt6i_src.plen);

	/* We cannot add true routes via loopback here,
	   they would result in kernel looping; promote them to reject routes
	 */
	if ((rtmsg->rtmsg_flags&RTF_REJECT) ||
	    (dev && (dev->flags&IFF_LOOPBACK) && !(addr_type&IPV6_ADDR_LOOPBACK))) {
		dev = dev_get("lo");
		rt->u.dst.output = ip6_pkt_discard;
		rt->u.dst.input = ip6_pkt_discard;
		rt->u.dst.error = -ENETUNREACH;
		rt->rt6i_flags = RTF_REJECT|RTF_NONEXTHOP;
		rt->rt6i_metric = rtmsg->rtmsg_metric;
		rt->rt6i_dev = dev;
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
			   It is very good, but in some (rare!) curcumstances
			   (SIT, NBMA NOARP links) it is handy to allow
			   some exceptions.
			 */
			if (!(gwa_type&IPV6_ADDR_UNICAST)) {
				*err = -EINVAL;
				goto out;
			}

			grt = rt6_lookup(gw_addr, NULL, rtmsg->rtmsg_ifindex, RTF_LINKRT);

			if (grt == NULL || (grt->rt6i_flags&RTF_GATEWAY)) {
				*err = -EHOSTUNREACH;
				goto out;
			}
			dev = grt->rt6i_dev;
		}
		if (dev == NULL || (dev->flags&IFF_LOOPBACK)) {
			*err = -EINVAL;
			goto out;
		}
	}

	if (dev == NULL) {
		RDBG(("!dev, "));
		*err = -ENODEV;
		goto out;
	}

	if (rtmsg->rtmsg_flags & (RTF_GATEWAY|RTF_NONEXTHOP)) {
		rt->rt6i_nexthop = ndisc_get_neigh(dev, &rt->rt6i_gateway);
		if (rt->rt6i_nexthop == NULL) {
			RDBG(("!nxthop, "));
			*err = -ENOMEM;
			goto out;
		}
		RDBG(("nxthop, "));
	}

	rt->rt6i_metric = rtmsg->rtmsg_metric;

	rt->rt6i_dev = dev;
	rt->u.dst.pmtu = ipv6_get_mtu(dev);
	rt->u.dst.rtt = TCP_TIMEOUT_INIT;
	if (ipv6_addr_is_multicast(&rt->rt6i_dst.addr))
		rt->rt6i_hoplimit = IPV6_DEFAULT_MCASTHOPS;
	else
		rt->rt6i_hoplimit = ipv6_get_hoplimit(dev);
	rt->rt6i_flags = rtmsg->rtmsg_flags;

install_route:
	RDBG(("rt6ins(%p) ", rt));

	rt6_lock();
	rt6_ins(rt);
	rt6_unlock();

	/* BUGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG!

	   If rt6_ins will fail (and it occurs regularly f.e. if route
	   already existed), the route will be freed -> Finita.
	   Crash. No recovery. NO FIX. Unfortunately, it is not the only
	   place will it is fatal. It is sad, I believed this
	   code is a bit more accurate :-(

	   Really, the problem can be solved in two ways:

	   * As I did in old 2.0 IPv4: to increase use count and force
	     user to destroy stray route. It requires some care,
	     well, much more care.
	   * Second and the best: to get rid of this damn backlogging
	     system. I wonder why Pedro so liked it. It was the most
	     unhappy day when I invented it (well, by a strange reason
	     I believed that it is very clever :-)),
	     and when I managed to clean IPv4 of this crap,
	     it was really great win.
	     BTW I forgot how 2.0 route/arp works :-) :-)
	                                                               --ANK
	 */

out:
	if (*err) {
		RDBG(("dfree(%p) ", rt));
		dst_free((struct dst_entry *) rt);
		rt = NULL;
	}
	RDBG(("ret(%p)\n", rt));
#if 0
	return rt;
#else
	/* BUGGG! For now always return NULL. (see above)

	   Really, it was used only in two places, and one of them
	   (rt6_add_dflt_router) is repaired, ip6_fw is not essential
	   at all. --ANK
	 */
	return NULL;
#endif
}

int ip6_del_rt(struct rt6_info *rt)
{
	rt6_lock();

	start_bh_atomic();

	/* I'd add here couple of cli()
	   cli(); cli(); cli();

	   Now it is really LOCKED. :-) :-) --ANK
	 */

	rt6_dflt_pointer = NULL;

	if (atomic_read(&rt6_tbl_lock) == 1)
		fib6_del(rt);
	else
		rtreq_add(rt, RT_OPER_DEL);
	end_bh_atomic();
	rt6_unlock();
	return 0;
}

int ip6_route_del(struct in6_rtmsg *rtmsg)
{
	struct fib6_node *fn;
	struct rt6_info *rt;

	rt6_lock();
	fn = fib6_lookup(&ip6_routing_table, &rtmsg->rtmsg_dst, &rtmsg->rtmsg_src);
	rt = fn->leaf;

	/*
	 *	Blow it away
	 *
	 *	BUGGGG It will not help with Pedro's subtrees.
	 *	We urgently need fib6_locate_node function, and
	 *	it is not the only place where rt6_lookup is used
	 *	for wrong purpose.
	 *							--ANK
	 */
restart:
	if (rt && rt->rt6i_src.plen == rtmsg->rtmsg_src_len) {
		if (rt->rt6i_dst.plen > rtmsg->rtmsg_dst_len) {
			struct fib6_node *fn = rt->rt6i_node;
			while ((fn = fn->parent) != NULL) {
				if (fn->fn_flags & RTN_ROOT)
					break;
				if (fn->fn_flags & RTN_RTINFO) {
					rt = fn->leaf;
					goto restart;
				}
			}
		}

		if (rt->rt6i_dst.plen == rtmsg->rtmsg_dst_len) {
			for ( ; rt; rt = rt->u.next) {
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
				ip6_del_rt(rt);
				rt6_unlock();
				return 0;
			}
		}
	}
	rt6_unlock();

	return -ESRCH;
}


/*
 *	bottom handler, runs with atomic_bh protection
 */
void __rt6_run_bh(void)
{
	struct rt6_req *rtreq;

	while ((rtreq = rtreq_dequeue())) {
		switch (rtreq->operation) {
		case RT_OPER_ADD:
			fib6_add(&ip6_routing_table, rtreq->ptr);
			break;
		case RT_OPER_DEL:
			fib6_del(rtreq->ptr);
			break;
		};
		kfree(rtreq);
	}
	rt6_bh_mask = 0;
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
			ip6_route_add(rtmsg, &err);
			break;
		case RTMSG_DELROUTE:
			ip6_route_del(rtmsg);
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
struct rt6_info *rt6_redirect(struct in6_addr *dest, struct in6_addr *saddr,
			      struct in6_addr *target, struct device *dev,
			      int on_link)
{
	struct rt6_info *rt, *nrt;

	/* Locate old route to this destination. */
	rt = rt6_lookup(dest, NULL, dev->ifindex, 0);

	if (rt == NULL || rt->u.dst.error)
		return NULL;

	/* Redirect received -> path was valid.
	   Look, redirects are sent only in response to data packets,
	   so that this nexthop apparently is reachable. --ANK
	 */
	dst_confirm(&rt->u.dst);

	/* Duplicate redirect: silently ignore. */
	if (ipv6_addr_cmp(target, &rt->rt6i_gateway) == 0)
		return NULL;

	/* Current route is on-link; redirect is always invalid. */
	if (!(rt->rt6i_flags&RTF_GATEWAY))
		return NULL;

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
			rt = ip6_routing_table.leaf;

			for (; rt; rt = rt->u.next) {
				if (!ipv6_addr_cmp(saddr, &rt->rt6i_gateway))
					goto source_ok;
			}
		}
		printk(KERN_DEBUG "rt6_redirect: source isn't a valid nexthop "
			       "for redirect target\n");
		return NULL;
	}

source_ok:
#endif

	/*
	 *	We have finally decided to accept it.
	 */
	if (rt->rt6i_dst.plen == 128) {
		/* BUGGGG! Very bad bug. Fast path code does not protect
		 * itself of changing nexthop on the fly, it was supposed
		 * that crucial parameters (dev, nexthop, hh) ARE VOLATILE.
		 *                                                   --ANK
		 * Not fixed!! I plugged it to avoid random crashes
		 * (they are very unlikely, but I do not want to shrug
		 *  every time when redirect arrives)
		 * but the plug must be removed. --ANK
		 */

#if 0
		/*
		 *	Already a host route.
		 *
		 */
		if (rt->rt6i_nexthop)
			neigh_release(rt->rt6i_nexthop);
		rt->rt6i_flags |= RTF_MODIFIED | RTF_CACHE;
		if (on_link)
			rt->rt6i_flags &= ~RTF_GATEWAY;
		ipv6_addr_copy(&rt->rt6i_gateway, target);
		rt->rt6i_nexthop = ndisc_get_neigh(rt->rt6i_dev, target);
		return rt;
#else
		return NULL;
#endif
	}

	nrt = ip6_rt_copy(rt);
	nrt->rt6i_flags = RTF_GATEWAY|RTF_UP|RTF_DYNAMIC|RTF_CACHE;
	if (on_link)
		nrt->rt6i_flags &= ~RTF_GATEWAY;

	ipv6_addr_copy(&nrt->rt6i_dst.addr, dest);
	nrt->rt6i_dst.plen = 128;

	ipv6_addr_copy(&nrt->rt6i_gateway, target);
	nrt->rt6i_nexthop = ndisc_get_neigh(nrt->rt6i_dev, target);
	nrt->rt6i_dev = dev;
	nrt->u.dst.pmtu = ipv6_get_mtu(dev);
	if (!ipv6_addr_is_multicast(&nrt->rt6i_dst.addr))
		nrt->rt6i_hoplimit = ipv6_get_hoplimit(dev);

	rt6_lock();
	rt6_ins(nrt);
	rt6_unlock();

	/* BUGGGGGGG! nrt can point to nowhere. */
	return nrt;
}

/*
 *	Handle ICMP "packet too big" messages
 *	i.e. Path MTU discovery
 */

void rt6_pmtu_discovery(struct in6_addr *addr, struct device *dev, int pmtu)
{
	struct rt6_info *rt, *nrt;

	if (pmtu < 576 || pmtu > 65536) {
#if RT6_DEBUG >= 1
		printk(KERN_DEBUG "rt6_pmtu_discovery: invalid MTU value %d\n",
		       pmtu);
#endif
		return;
	}

	rt = rt6_lookup(addr, NULL, dev->ifindex, 0);

	if (rt == NULL || rt->u.dst.error) {
#if RT6_DEBUG >= 2
		printk(KERN_DEBUG "rt6_pmtu_discovery: no route to host\n");
#endif
		return;
	}

	if (pmtu >= rt->u.dst.pmtu)
		return;

	/* New mtu received -> path was valid.
	   They are sent only in response to data packets,
	   so that this nexthop apparently is reachable. --ANK
	 */
	dst_confirm(&rt->u.dst);

	/* It is wrong, but I plugged the hole here.
	   On-link routes are cloned differently,
	   look at rt6_redirect --ANK
	 */
	if (!(rt->rt6i_flags&RTF_GATEWAY))
		return;

	if (rt->rt6i_dst.plen == 128) {
		/*
		 *	host route
		 */
		rt->u.dst.pmtu = pmtu;
		rt->rt6i_flags |= RTF_MODIFIED;

		return;
	}

	nrt = ip6_rt_copy(rt);
	ipv6_addr_copy(&nrt->rt6i_dst.addr, addr);
	nrt->rt6i_dst.plen = 128;

	nrt->rt6i_flags |= (RTF_DYNAMIC | RTF_CACHE);

	/* It was missing. :-) :-)
	   I wonder, kernel was deemed to crash after pkt_too_big
	   and nobody noticed it. Hey, guys, do someone really
	   use it? --ANK
	 */
	nrt->rt6i_nexthop = neigh_clone(rt->rt6i_nexthop);

	rt6_lock();
	rt6_ins(rt);
	rt6_unlock();
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
		rt->rt6i_hoplimit = ort->rt6i_hoplimit;
		rt->rt6i_dev = ort->rt6i_dev;

		ipv6_addr_copy(&rt->rt6i_gateway, &ort->rt6i_gateway);
		rt->rt6i_keylen = ort->rt6i_keylen;
		rt->rt6i_flags = ort->rt6i_flags;
		rt->rt6i_metric = ort->rt6i_metric;

		memcpy(&rt->rt6i_dst, &ort->rt6i_dst, sizeof(struct rt6key));
		memcpy(&rt->rt6i_src, &ort->rt6i_src, sizeof(struct rt6key));
	}
	return rt;
}

struct rt6_info *rt6_get_dflt_router(struct in6_addr *addr, struct device *dev)
{	
	struct rt6_info *rt;
	struct fib6_node *fn;

	RDBG(("rt6_get_dflt_router(%p,%p)[%p]", addr, dev,
	      __builtin_return_address(0)));
#if RT6_DEBUG >= 3
	{
		int i;

		RDBG(("addr["));
		for(i = 0; i < 8; i++) {
			RDBG(("%04x%c", addr->s6_addr16[i],
			      i == 7 ? ']' : ':'));
		}
	}
#endif
	RDBG(("\n"));
	rt6_lock();

	fn = &ip6_routing_table;

	for (rt = fn->leaf; rt; rt=rt->u.next) {
		if (dev == rt->rt6i_dev &&
		    ipv6_addr_cmp(&rt->rt6i_gateway, addr) == 0)
			break;
	}

	rt6_unlock();
	return rt;
}

struct rt6_info *rt6_add_dflt_router(struct in6_addr *gwaddr,
				     struct device *dev)
{
	struct in6_rtmsg rtmsg;
	struct rt6_info *rt;
	int err;

	RDBG(("rt6_add_dflt_router(%p,%p)[%p] ", gwaddr, dev,
	      __builtin_return_address(0)));
#if RT6_DEBUG >= 3
	{
		struct in6_addr *addr = gwaddr;
		int i;

		RDBG(("gwaddr["));
		for(i = 0; i < 8; i++) {
			RDBG(("%04x%c", addr->s6_addr16[i],
			      i == 7 ? ']' : ':'));
		}
	}
#endif
	RDBG(("\n"));

	memset(&rtmsg, 0, sizeof(struct in6_rtmsg));
	rtmsg.rtmsg_type = RTMSG_NEWROUTE;
	ipv6_addr_copy(&rtmsg.rtmsg_gateway, gwaddr);
	rtmsg.rtmsg_metric = 1024;
	rtmsg.rtmsg_flags = RTF_GATEWAY | RTF_ADDRCONF | RTF_DEFAULT | RTF_UP;

	rtmsg.rtmsg_ifindex = dev->ifindex;

	rt = ip6_route_add(&rtmsg, &err);

	/* BUGGGGGGGGGGGGGGGGGGGG!
	   rt can be not NULL, but point to heavens.
	 */

	if (err) {
		printk(KERN_DEBUG "rt6_add_dflt: ip6_route_add error %d\n",
		       err);
	}
	return rt;
}

void rt6_purge_dflt_routers(int last_resort)
{
	struct rt6_info *rt;
	struct fib6_node *fn;
	u32 flags;

	RDBG(("rt6_purge_dflt_routers(%d)[%p]\n", last_resort,
	      __builtin_return_address(0)));
	fn = &ip6_routing_table;

	rt6_dflt_pointer = NULL;

	if (last_resort)
		flags = RTF_ALLONLINK;
	else
		flags = RTF_DEFAULT | RTF_ADDRCONF;	

	for (rt = fn->leaf; rt; ) {
		if ((rt->rt6i_flags & flags)) {
			struct rt6_info *drt;
#if RT6_DEBUG >= 2
			printk(KERN_DEBUG "rt6_purge_dflt: deleting entry\n");
#endif
			drt = rt;
			rt = rt->u.next;
			ip6_del_rt(drt);
			continue;
		}
		rt = rt->u.next;
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
			ip6_route_add(&rtmsg, &err);
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

	RDBG(("ip6_rt_addr_add(%p,%p)[%p]\n", addr, dev,
	      __builtin_return_address(0)));
#if RT6_DEBUG >= 3
	{
		int i;

		RDBG(("addr["));
		for(i = 0; i < 8; i++) {
			RDBG(("%04x%c", addr->s6_addr16[i],
			      i == 7 ? ']' : ':'));
		}
	}
#endif
	RDBG(("\n"));

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

	rt6_lock();
	rt6_ins(rt);
	rt6_unlock();

	return 0;
}

/* Delete address. Warning: you should check that this address
   disappeared before calling this function.
 */

int ip6_rt_addr_del(struct in6_addr *addr, struct device *dev)
{
	struct rt6_info *rt;

	rt = rt6_lookup(addr, NULL, loopback_dev.ifindex, RTF_LINKRT);
	if (rt && rt->rt6i_dst.plen == 128)
		return ip6_del_rt(rt);

	return 0;
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
	return &ip6_null_entry;

found:

	if (nrt == NULL)
		goto error;

	nrt->rt6i_flags |= RTF_CACHE;
	/* BUGGGG! nrt can point to nowhere! */
	rt6_ins(nrt);

	return nrt;
}
#endif

/* 
 * Nope, I am not idiot. I see that it is the ugliest of ugly routines.
 * Anyone is advertised to write better one. --ANK
 */

struct rt6_ifdown_arg {
	struct device *dev;
	struct rt6_info *rt;
};


static void rt6_ifdown_node(struct fib6_node *fn, void *p_arg)
{
	struct rt6_info *rt;
	struct rt6_ifdown_arg *arg = (struct rt6_ifdown_arg *) p_arg;

	if (arg->rt != NULL)
		return;

	for (rt = fn->leaf; rt; rt = rt->u.next) {
		if (rt->rt6i_dev == arg->dev || arg->dev == NULL) {
			arg->rt = rt;
			return;
		}
	}
}

void rt6_ifdown(struct device *dev)
{
	int count = 0;
	struct rt6_ifdown_arg arg;
	struct rt6_info *rt;

	do {
		arg.dev = dev;
		arg.rt = NULL;
		fib6_walk_tree(&ip6_routing_table, rt6_ifdown_node, &arg,
			       RT6_FILTER_RTNODES);
		if (arg.rt != NULL)
			ip6_del_rt(arg.rt);
		count++;
	} while (arg.rt != NULL);

	/* And default routes ... */

	for (rt = ip6_routing_table.leaf; rt; ) {
		if (rt != &ip6_null_entry && (rt->rt6i_dev == dev || dev == NULL)) {
			struct rt6_info *deleting = rt;
			rt = rt->u.next;
			ip6_del_rt(deleting);
			continue;
		}
		rt = rt->u.next;
	}
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
	int err = 0;

	if (inet6_rtm_to_rtmsg(r, arg, &rtmsg))
		return -EINVAL;
	ip6_route_add(&rtmsg, &err);
	return err;
}

struct rt6_rtnl_dump_arg
{
	struct sk_buff *skb;
	struct netlink_callback *cb;
	int skip;
	int count;
	int stop;
};

static int rt6_fill_node(struct sk_buff *skb, struct rt6_info *rt,
			 struct in6_addr *dst,
			 struct in6_addr *src,
			 int iif,
			 int type, pid_t pid, u32 seq)
{
	struct rtmsg *rtm;
	struct nlmsghdr  *nlh;
	unsigned char	 *b = skb->tail;
#ifdef CONFIG_RTNL_OLD_IFINFO
	unsigned char 	 *o;
#else
	struct rtattr *mx;
#endif
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
#ifdef CONFIG_RTNL_OLD_IFINFO
	rtm->rtm_nhs = 0;
#endif
	rtm->rtm_protocol = RTPROT_BOOT;
	if (rt->rt6i_flags&RTF_DYNAMIC)
		rtm->rtm_protocol = RTPROT_REDIRECT;
	else if (rt->rt6i_flags&(RTF_ADDRCONF|RTF_ALLONLINK))
		rtm->rtm_protocol = RTPROT_KERNEL;
	else if (rt->rt6i_flags&RTF_DEFAULT)
		rtm->rtm_protocol = RTPROT_RA;

	if (rt->rt6i_flags&RTF_CACHE)
		rtm->rtm_flags |= RTM_F_CLONED;

#ifdef CONFIG_RTNL_OLD_IFINFO
	o = skb->tail;
#endif
	if (dst) {
		RTA_PUT(skb, RTA_DST, 16, dst);
	        rtm->rtm_dst_len = 128;
	} else if (rtm->rtm_dst_len)
		RTA_PUT(skb, RTA_DST, 16, &rt->rt6i_dst.addr);
	if (src) {
		RTA_PUT(skb, RTA_SRC, 16, src);
	        rtm->rtm_src_len = 128;
	} else if (rtm->rtm_src_len)
		RTA_PUT(skb, RTA_SRC, 16, &rt->rt6i_src.addr);
	if (iif)
		RTA_PUT(skb, RTA_IIF, 4, &iif);
	else if (dst) {
		struct inet6_ifaddr *ifp = ipv6_get_saddr(&rt->u.dst, dst);
		if (ifp)
			RTA_PUT(skb, RTA_PREFSRC, 16, &ifp->addr);
	}
#ifdef CONFIG_RTNL_OLD_IFINFO
	if (rt->u.dst.pmtu)
		RTA_PUT(skb, RTA_MTU, sizeof(unsigned), &rt->u.dst.pmtu);
	if (rt->u.dst.window)
		RTA_PUT(skb, RTA_WINDOW, sizeof(unsigned), &rt->u.dst.window);
	if (rt->u.dst.rtt)
		RTA_PUT(skb, RTA_RTT, sizeof(unsigned), &rt->u.dst.rtt);
#else
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
#endif
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
	ci.rta_used = 0;
	ci.rta_clntref = atomic_read(&rt->u.dst.use);
	ci.rta_error = rt->u.dst.error;
	RTA_PUT(skb, RTA_CACHEINFO, sizeof(ci), &ci);
#ifdef CONFIG_RTNL_OLD_IFINFO
	rtm->rtm_optlen = skb->tail - o;
#endif
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static void rt6_dump_node(struct fib6_node *fn, void *p_arg)
{
	struct rt6_info *rt;
	struct rt6_rtnl_dump_arg *arg = (struct rt6_rtnl_dump_arg *) p_arg;

	if (arg->stop)
		return;

	for (rt = fn->leaf; rt; rt = rt->u.next) {
		if (arg->count < arg->skip) {
			arg->count++;
			continue;
		}
		if (rt6_fill_node(arg->skb, rt, NULL, NULL, 0, RTM_NEWROUTE,
				  NETLINK_CB(arg->cb->skb).pid, arg->cb->nlh->nlmsg_seq) <= 0) {
			arg->stop = 1;
			break;
		}
		arg->count++;
	}
}


int inet6_dump_fib(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct rt6_rtnl_dump_arg arg;

	arg.skb = skb;
	arg.cb = cb;
	arg.skip = cb->args[0];
	arg.count = 0;
	arg.stop = 0;
	start_bh_atomic();
	fib6_walk_tree(&ip6_routing_table, rt6_dump_node, &arg, RT6_FILTER_RTNODES);
	if (arg.stop == 0)
		rt6_dump_node(&ip6_routing_table, &arg);
	end_bh_atomic();
	cb->args[0] = arg.count;
	return skb->len;
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

struct rt6_proc_arg {
	char *buffer;
	int offset;
	int length;
	int skip;
	int len;
};

static void rt6_info_node(struct fib6_node *fn, void *p_arg)
{
	struct rt6_info *rt;
	struct rt6_proc_arg *arg = (struct rt6_proc_arg *) p_arg;

	for (rt = fn->leaf; rt; rt = rt->u.next) {
		int i;

		if (arg->skip < arg->offset / RT6_INFO_LEN) {
			arg->skip++;
			continue;
		}

		if (arg->len >= arg->length)
			return;
		
		for (i=0; i<16; i++) {
			sprintf(arg->buffer + arg->len, "%02x",
				rt->rt6i_dst.addr.s6_addr[i]);
			arg->len += 2;
		}
		arg->len += sprintf(arg->buffer + arg->len, " %02x ",
				    rt->rt6i_dst.plen);

		for (i=0; i<16; i++) {
			sprintf(arg->buffer + arg->len, "%02x",
				rt->rt6i_src.addr.s6_addr[i]);
			arg->len += 2;
		}
		arg->len += sprintf(arg->buffer + arg->len, " %02x ",
				    rt->rt6i_src.plen);
		
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
				    rt->rt6i_metric, atomic_read(&rt->rt6i_use),
				    atomic_read(&rt->rt6i_ref), rt->rt6i_flags, 
				    rt->rt6i_dev ? rt->rt6i_dev->name : "");
	}
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

	fib6_walk_tree(&ip6_routing_table, rt6_info_node, &arg,
		       RT6_FILTER_RTNODES);

	rt6_info_node(&ip6_routing_table, &arg);

	*start = buffer;
	if (offset)
		*start += offset % RT6_INFO_LEN;

	arg.len -= offset % RT6_INFO_LEN;

	if(arg.len > length)
		arg.len = length;
	if(arg.len < 0)
		arg.len = 0;

	return arg.len;
}

#define PTR_SZ (sizeof(void *) * 2)
#define FI_LINE_SZ (2 * (PTR_SZ) + 7 + 32 + 4 + 32 + 4)

static void rt6_tree_node(struct fib6_node *fn, void *p_arg)
{
	struct rt6_proc_arg *arg = (struct rt6_proc_arg *) p_arg;
	struct rt6_info *rt;
	char f;
	int i;

	rt = fn->leaf;

	if (arg->skip < arg->offset / FI_LINE_SZ) {
		arg->skip++;
		return;
	}

	if (arg->len + FI_LINE_SZ >= arg->length)
		return;

	f = (fn->fn_flags & RTN_RTINFO) ? 'r' : 'n';
	arg->len += sprintf(arg->buffer + arg->len, "%p %p %02x %c ",
			    fn, fn->parent, fn->fn_bit, f);

	for (i=0; i<16; i++) {
		sprintf(arg->buffer + arg->len, "%02x",
			rt->rt6i_dst.addr.s6_addr[i]);
		arg->len += 2;
	}
	arg->len += sprintf(arg->buffer + arg->len, " %02x ",
			    rt->rt6i_dst.plen);
	
	for (i=0; i<16; i++) {
		sprintf(arg->buffer + arg->len, "%02x",
			rt->rt6i_src.addr.s6_addr[i]);
		arg->len += 2;
	}
	arg->len += sprintf(arg->buffer + arg->len, " %02x\n",
			    rt->rt6i_src.plen);

}

static int rt6_proc_tree(char *buffer, char **start, off_t offset, int length,
			 int dummy)
{
	struct rt6_proc_arg arg;
	arg.buffer = buffer;
	arg.offset = offset;
	arg.length = length;
	arg.skip = 0;
	arg.len = 0;

	fib6_walk_tree(&ip6_routing_table, rt6_tree_node, &arg, 0);

	*start = buffer;
	if (offset)
		*start += offset % RT6_INFO_LEN;

	arg.len -= offset % RT6_INFO_LEN;

	if(arg.len > length)
		arg.len = length;
	if(arg.len < 0)
		arg.len = 0;

	return arg.len;
}


extern struct rt6_statistics rt6_stats;

static int rt6_proc_stats(char *buffer, char **start, off_t offset, int length,
			  int dummy)
{
	int len;

	len = sprintf(buffer, "%04x %04x %04x %04x %04x\n",
		      rt6_stats.fib_nodes, rt6_stats.fib_route_nodes,
		      rt6_stats.fib_rt_alloc, rt6_stats.fib_rt_entries,
		      rt6_stats.fib_rt_cache);

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
static struct proc_dir_entry proc_rt6_tree = {
	PROC_NET_RT6_TREE, 7, "ip6_fib",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	rt6_proc_tree
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
	proc_net_register(&proc_rt6_tree);
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
	proc_net_unregister(PROC_NET_RT6_TREE);
	proc_net_unregister(PROC_NET_RT6_STATS);
#endif
#ifdef CONFIG_IPV6_NETLINK
	netlink_detach(NETLINK_ROUTE6);
#endif
	rt6_ifdown(NULL);
	fib6_gc_cleanup();
}
#endif	/* MODULE */

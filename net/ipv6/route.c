/*
 *	Linux INET6 implementation
 *	FIB front-end.
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: route.c,v 1.13 1997/07/19 11:11:35 davem Exp $
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

#ifdef 	CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#include <net/snmp.h>
#include <net/ipv6.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/netlink.h>

#include <asm/uaccess.h>

#undef CONFIG_RT6_POLICY

/* Set to 3 to get tracing. */
#define RT6_DEBUG 2

#if RT6_DEBUG >= 3
#define RDBG(x) printk x
#else
#define RDBG(x)
#endif

static struct dst_entry	*ip6_dst_check(struct dst_entry *dst, u32 cookie);
static struct dst_entry	*ip6_dst_reroute(struct dst_entry *dst,
					 struct sk_buff *skb);

static int		ip6_pkt_discard(struct sk_buff *skb);

struct dst_ops ip6_dst_ops = {
	AF_INET6,
	ip6_dst_check,
	ip6_dst_reroute,
	NULL
};

struct rt6_info ip6_null_entry = {
	{{NULL, ATOMIC_INIT(0), ATOMIC_INIT(0), NULL,
	  0, 0, 0, 0, 0, 0, 0, 0, -ENETUNREACH, NULL, NULL,
	  ip6_pkt_discard, ip6_pkt_discard, &ip6_dst_ops}},
	NULL, {{{0}}}, 256, RTF_REJECT|RTF_NONEXTHOP, ~0UL,
	0, {NULL}, {{{{0}}}, 128}, {{{{0}}}, 128}
};

struct fib6_node ip6_routing_table = {
	NULL, NULL, NULL, NULL,
	&ip6_null_entry,
	0, RTN_ROOT|RTN_TL_ROOT, 0
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
						    struct device *dev,
						    int strict)
{
	struct rt6_info *sprt;

	RDBG(("rt6_device_match: (%p,%p,%d) ", rt, dev, strict));
	if (dev) {
		for (sprt = rt; sprt; sprt = sprt->u.next) {
			if (sprt->rt6i_dev == dev) {
				RDBG(("match --> %p\n", sprt));
				return sprt;
			}
		}

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

static struct rt6_info *rt6_best_dflt(struct rt6_info *rt, struct device *dev)
{
	struct rt6_info *match = NULL;
	struct rt6_info *sprt;
	int mpri = 0;

	RDBG(("rt6_best_dflt(%p,%p): ", rt, dev));
	for (sprt = rt; sprt; sprt = sprt->u.next) {
		struct nd_neigh *ndn;

		RDBG(("sprt(%p): ", sprt));
		if ((ndn = (struct nd_neigh *) sprt->rt6i_nexthop)) {
			int m = -1;

			RDBG(("nxthop(%p,%d) ", ndn, ndn->ndn_nud_state));
			switch (ndn->ndn_nud_state) {
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

			if (dev && sprt->rt6i_dev == dev) {
				RDBG(("dev&&sprt->rt6i_dev==dev(%p), m+=2, ", dev));
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
			    struct device *dev, int flags)
{
	struct fib6_node *fn;
	struct rt6_info *rt;

	RDBG(("rt6_lookup(%p,%p,%p,%x) from %p\n",
	      daddr, saddr, dev, flags, __builtin_return_address(0)));
	rt6_lock();
	fn = fib6_lookup(&ip6_routing_table, daddr, saddr);

	rt = rt6_device_match(fn->leaf, dev, 0);
	rt6_unlock();
	return rt;
}

static struct rt6_info *rt6_cow(struct rt6_info *rt, struct in6_addr *daddr,
				struct in6_addr *saddr)
{
	/*
	 *	Clone the route.
	 */

	rt = ip6_rt_copy(rt);

	if (rt) {
		ipv6_addr_copy(&rt->rt6i_dst.addr, daddr);

		rt->rt6i_dst.plen = 128;
		rt->rt6i_flags |= RTF_CACHE;

		if (rt->rt6i_src.plen) {
			ipv6_addr_copy(&rt->rt6i_src.addr, saddr);
			rt->rt6i_src.plen = 128;
		}

		rt->rt6i_nexthop = ndisc_get_neigh(rt->rt6i_dev, daddr);

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
	rt6_lock();
	fn = fib6_lookup(&ip6_routing_table, &skb->nh.ipv6h->daddr,
			 &skb->nh.ipv6h->saddr);

	rt = fn->leaf;

	if ((rt->rt6i_flags & RTF_CACHE)) {
		if (ip6_rt_policy == 0) {
			rt = rt6_device_match(rt, skb->dev, 0);
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

	rt = rt6_device_match(rt, skb->dev, 0);

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
	dst->input(skb);
}

struct dst_entry * ip6_route_output(struct sock *sk, struct flowi *fl)
{
	struct fib6_node *fn;
	struct rt6_info *rt;
	struct dst_entry *dst;
	int strict;

	RDBG(("ip6_route_output(%p,%p) from(%p)", sk, fl,
	      __builtin_return_address(0)));
	strict = ipv6_addr_type(fl->nl_u.ip6_u.daddr) & IPV6_ADDR_MULTICAST;

	rt6_lock();
#if RT6_DEBUG >= 3
	RDBG(("lkup("));
	if(fl->nl_u.ip6_u.daddr) {
		struct in6_addr *addr = fl->nl_u.ip6_u.daddr;
		int i;
		RDBG(("daddr["));
		for(i = 0; i < 8; i++) {
			RDBG(("%04x%c", addr->s6_addr16[i],
			      i == 7 ? ']' : ':'));
		}
	}
	if(fl->nl_u.ip6_u.saddr) {
		struct in6_addr *addr = fl->nl_u.ip6_u.saddr;
		int i;
		RDBG(("saddr["));
		for(i = 0; i < 8; i++) {
			RDBG(("%04x%c", addr->s6_addr16[i],
			      i == 7 ? ']' : ':'));
		}
	}
#endif
	fn = fib6_lookup(&ip6_routing_table, fl->nl_u.ip6_u.daddr,
			 fl->nl_u.ip6_u.saddr);

	RDBG(("-->(%p[%s])) ", fn, fn == &ip6_routing_table ? "ROOT" : "!ROOT"));

	rt = fn->leaf;

	if ((rt->rt6i_flags & RTF_CACHE)) {
		RDBG(("RTF_CACHE "));
		if (ip6_rt_policy == 0) {
			rt = rt6_device_match(rt, fl->dev, strict);
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
			rt = rt6_best_dflt(rt, fl->dev);
			RDBG(("best_dflt(%p) ", rt));
		}
	} else {
		rt = rt6_device_match(rt, fl->dev, strict);
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


void rt6_ins(struct rt6_info *rt)
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
 */

struct dst_entry *ip6_dst_check(struct dst_entry *dst, u32 cookie)
{
	struct rt6_info *rt;

	RDBG(("ip6dstchk(%p,%08x)[%p]\n", dst, cookie,
	      __builtin_return_address(0)));

	rt = (struct rt6_info *) dst;

	if (rt->rt6i_node && (rt->rt6i_node->fn_sernum == cookie)) {
		if (rt->rt6i_nexthop)
			ndisc_event_send(rt->rt6i_nexthop, NULL);

		return dst;
	}

	dst_release(dst);
	return NULL;
}

struct dst_entry *ip6_dst_reroute(struct dst_entry *dst, struct sk_buff *skb)
{
	/*
	 *	FIXME
	 */
	RDBG(("ip6_dst_reroute(%p,%p)[%p] (AIEEE)\n", dst, skb,
	      __builtin_return_address(0)));
	return NULL;
}

/*
 *
 */

struct rt6_info *ip6_route_add(struct in6_rtmsg *rtmsg, int *err)
{
	struct rt6_info *rt;
	struct device *dev = NULL;
	int addr_type;
	
	RDBG(("ip6_route_add(%p)[%p] ", rtmsg, __builtin_return_address(0)));
	*err = 0;
	
	rt = dst_alloc(sizeof(struct rt6_info), &ip6_dst_ops);

	if (rt == NULL) {
		RDBG(("dalloc fails, "));
		*err = -ENOMEM;
		goto out;
	}

	/*
	 *	default... this should be chosen according to route flags
	 */

#if RT6_DEBUG >= 3
	{
		struct in6_addr *addr = &rtmsg->rtmsg_dst;
		int i;

		RDBG(("daddr["));
		for(i = 0; i < 8; i++) {
			RDBG(("%04x%c", addr->s6_addr16[i],
			      i == 7 ? ']' : ':'));
		}
		addr = &rtmsg->rtmsg_src;
		RDBG(("saddr["));
		for(i = 0; i < 8; i++) {
			RDBG(("%04x%c", addr->s6_addr16[i],
			      i == 7 ? ']' : ':'));
		}
	}
#endif

	addr_type = ipv6_addr_type(&rtmsg->rtmsg_dst);
	
	if (addr_type & IPV6_ADDR_MULTICAST) {
		RDBG(("MCAST, "));
		rt->u.dst.input = ip6_mc_input;
	} else {
		RDBG(("!MCAST "));
		rt->u.dst.input = ip6_forward;
	}
	
	rt->u.dst.output = dev_queue_xmit;
	
	if (rtmsg->rtmsg_ifindex)
		dev = dev_get_by_index(rtmsg->rtmsg_ifindex);
	if(dev)
		RDBG(("d[%s] ", dev->name));

	ipv6_addr_copy(&rt->rt6i_dst.addr, &rtmsg->rtmsg_dst);
	rt->rt6i_dst.plen = rtmsg->rtmsg_dst_len;

	/* XXX Figure out what really is supposed to be happening here -DaveM */
	ipv6_addr_copy(&rt->rt6i_src.addr, &rtmsg->rtmsg_src);
	rt->rt6i_src.plen = rtmsg->rtmsg_src_len;
	
	if ((rt->rt6i_src.plen = rtmsg->rtmsg_src_len)) {
		RDBG(("splen, "));
		ipv6_addr_copy(&rt->rt6i_src.addr, &rtmsg->rtmsg_src);
	} else {
		RDBG(("!splen, "));
	}
	/* XXX */

	if (rtmsg->rtmsg_flags & (RTF_GATEWAY | RTF_NONEXTHOP)) {
		struct rt6_info *grt;
		struct in6_addr *gw_addr;
		u32 flags = 0;

		RDBG(("RTF_GATEWAY, "));
		/*
		 *	1. gateway route lookup
		 *	2. ndisc_get_neigh
		 */

		gw_addr = &rtmsg->rtmsg_gateway;

#if RT6_DEBUG >= 3
		{
			struct in6_addr *addr = gw_addr;
			int i;

			RDBG(("gwaddr["));
			for(i = 0; i < 8; i++) {
				RDBG(("%04x%c", addr->s6_addr16[i],
				      i == 7 ? ']' : ':'));
			}
		}
#endif

		if ((rtmsg->rtmsg_flags & RTF_GATEWAY) &&
		    (rtmsg->rtmsg_flags & RTF_ADDRCONF) == 0) {
			RDBG(("RTF_GATEWAY && !RTF_ADDRCONF, "));
			if (dev)
				flags |= RTF_LINKRT;

			grt = rt6_lookup(gw_addr, NULL, dev, flags);

			if (grt == NULL)
			{
				RDBG(("!grt, "));
				*err = -EHOSTUNREACH;
				goto out;
			}
			dev = grt->rt6i_dev;
			RDBG(("grt(d=%s), ", dev ? dev->name : "NULL"));
		}

		rt->rt6i_nexthop = ndisc_get_neigh(dev, gw_addr);

		if (rt->rt6i_nexthop == NULL) {
			RDBG(("!nxthop, "));
			*err = -ENOMEM;
			goto out;
		}
		RDBG(("nxthop, "));
	}

	if (dev == NULL) {
		RDBG(("!dev, "));
		*err = -ENODEV;
		goto out;
	}

	rt->rt6i_metric = rtmsg->rtmsg_metric;

	rt->rt6i_dev = dev;
	rt->u.dst.pmtu = dev->mtu;
	rt->rt6i_flags = rtmsg->rtmsg_flags;

	RDBG(("rt6ins(%p) ", rt));

	rt6_lock();
	rt6_ins(rt);
	rt6_unlock();

out:
	if (*err) {
		RDBG(("dfree(%p) ", rt));
		dst_free((struct dst_entry *) rt);
		rt = NULL;
	}
	RDBG(("ret(%p)\n", rt));
	return rt;
}

int ip6_del_rt(struct rt6_info *rt)
{
	rt6_lock();

	start_bh_atomic();

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
	return 0;
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

/*
 *	NETLINK interface
 *	routing socket moral equivalent
 */

static int rt6_msgrcv(int unit, struct sk_buff *skb)
{
	int count = 0;
	struct in6_rtmsg *rtmsg;
	int err;

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
	kfree_skb(skb, FREE_READ);	
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
		kfree_skb(skb, FREE_WRITE);
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
		kfree_skb(skb, FREE_WRITE);
}

/*
 *	Handle redirects
 */
struct rt6_info *rt6_redirect(struct in6_addr *dest, struct in6_addr *saddr,
			      struct in6_addr *target, struct device *dev,
			      int on_link)
{
	struct rt6_info *rt, *tgtr, *nrt;

	RDBG(("rt6_redirect(%s)[%p]: ",
	      dev ? dev->name : "NULL",
	      __builtin_return_address(0)));
	rt = rt6_lookup(dest, NULL, dev, 0);

	if (rt == NULL || rt->u.dst.error) {
		RDBG(("!rt\n"));
		printk(KERN_DEBUG "rt6_redirect: no route to destination\n");
		return NULL;
	}

	if (rt->rt6i_flags & RTF_GATEWAY) {
		/*
		 *	This can happen due to misconfiguration
		 *	if we are dealing with an "on link" redirect.
		 */
		RDBG(("RTF_GATEWAY\n"));
		printk(KERN_DEBUG "rt6_redirect: destination not directly "
		       "connected\n");
		return NULL;
	}
	RDBG(("tgt_lkup, "));
	tgtr = rt6_lookup(target, NULL, dev, 0);

	if (tgtr == NULL || tgtr->u.dst.error) {
		/*
		 *	duh?! no route to redirect target.
		 *	How where we talking to it in the first place ?
		 */
		RDBG(("!tgtr||dsterr\n"));
		printk(KERN_DEBUG "rt6_redirect: no route to target\n");
		return NULL;
	}

	if ((tgtr->rt6i_flags & RTF_GATEWAY) &&
	    ipv6_addr_cmp(dest, &tgtr->rt6i_gateway) == 0) {
		RDBG(("tgt RTF_GATEWAY && dstmatch, dup\n"));
		/*
		 *	Check if we already have the right route.
		 */
#if RT6_DEBUG >= 1
		printk(KERN_DEBUG "rt6_redirect: duplicate\n");
#endif
		return NULL;
	}

	/*
	 *	RFC 1970 specifies that redirects should only be
	 *	accepted if they come from the nexthop to the target.
	 *	Due to the way default routers are chosen, this notion
	 *	is a bit fuzzy and one might need to check all default
	 *	routers.
	 */

	if (ipv6_addr_cmp(saddr, &tgtr->rt6i_gateway)) {
		RDBG(("saddr/tgt->gway match, "));
		if (tgtr->rt6i_flags & RTF_DEFAULT) {
			tgtr = ip6_routing_table.leaf;

			for (; tgtr; tgtr = tgtr->u.next) {
				if (!ipv6_addr_cmp(saddr, &tgtr->rt6i_gateway)) {
					RDBG(("found srcok, "));
					goto source_ok;
				}
			}
		}
		RDBG(("!dflt||!srcok, "));
		printk(KERN_DEBUG "rt6_redirect: source isn't a valid nexthop "
		       "for redirect target\n");
	}

source_ok:

	/*
	 *	We have finally decided to accept it.
	 */
	RDBG(("srcok: "));
	if ((tgtr->rt6i_flags & RTF_HOST)) {
		/*
		 *	Already a host route.
		 *
		 */
		RDBG(("hralready, "));
		if (tgtr->rt6i_nexthop) {
			RDBG(("nrel(nxthop) "));
			neigh_release(tgtr->rt6i_nexthop);
		}
		/*
		 *	purge hh_cache
		 */
		tgtr->rt6i_flags |= RTF_MODIFIED | RTF_CACHE;
		ipv6_addr_copy(&tgtr->rt6i_gateway, dest);
		tgtr->rt6i_nexthop = ndisc_get_neigh(tgtr->rt6i_dev, dest);
		RDBG(("hhpurge, getnewneigh, ret(%p)\n", tgtr));
		return tgtr;
	}

	nrt = ip6_rt_copy(tgtr);
	nrt->rt6i_flags = RTF_GATEWAY|RTF_HOST|RTF_UP|RTF_DYNAMIC|RTF_CACHE;

	ipv6_addr_copy(&nrt->rt6i_dst.addr, target);
	nrt->rt6i_dst.plen = 128;

	ipv6_addr_copy(&nrt->rt6i_gateway, dest);
	nrt->rt6i_nexthop = ndisc_get_neigh(nrt->rt6i_dev, dest);
	nrt->rt6i_dev = dev;
	nrt->u.dst.pmtu = dev->mtu;

	RDBG(("rt6_ins(%p)\n", nrt));

	rt6_lock();
	rt6_ins(nrt);
	rt6_unlock();

	return nrt;
}

/*
 *	Handle ICMP "packet too big" messages
 *	i.e. Path MTU discovery
 */

void rt6_pmtu_discovery(struct in6_addr *addr, struct device *dev, int pmtu)
{
	struct rt6_info *rt;

	if (pmtu < 576 || pmtu > 65536) {
#if RT6_DEBUG >= 1
		printk(KERN_DEBUG "rt6_pmtu_discovery: invalid MTU value %d\n",
		       pmtu);
#endif
		return;
	}

	rt = rt6_lookup(addr, NULL, dev, 0);

	if (rt == NULL || rt->u.dst.error) {
#if RT6_DEBUG >= 2
		printk(KERN_DEBUG "rt6_pmtu_discovery: no route to host\n");
#endif
		return;
	}

	if (rt->rt6i_flags & RTF_HOST) {
		/*
		 *	host route
		 */
		rt->u.dst.pmtu = pmtu;
		rt->rt6i_flags |= RTF_MODIFIED;

		return;
	}

	rt = ip6_rt_copy(rt);
	ipv6_addr_copy(&rt->rt6i_dst.addr, addr);
	rt->rt6i_dst.plen = 128;

	rt->rt6i_flags |= (RTF_HOST | RTF_DYNAMIC | RTF_CACHE);

	rt6_lock();
	rt6_ins(rt);
	rt6_unlock();
}

/*
 *	Misc support functions
 */

struct rt6_info * ip6_rt_copy(struct rt6_info *ort)
{
	struct rt6_info *rt;

	rt = dst_alloc(sizeof(struct rt6_info), &ip6_dst_ops);
	
	if (rt) {
		rt->u.dst.input = ort->u.dst.input;
		rt->u.dst.output = ort->u.dst.output;

		rt->u.dst.pmtu = ort->u.dst.pmtu;
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
		    ipv6_addr_cmp(&rt->rt6i_dst.addr, addr) == 0)
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
		if (!suser())
			return -EPERM;
		err = copy_from_user(&rtmsg, arg,
				     sizeof(struct in6_rtmsg));
		if (err)
			return -EFAULT;
			
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

		if (err == 0)
				rt6_sndrtmsg(&rtmsg);
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
	kfree_skb(skb, FREE_WRITE);
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
	
	memset(rt, 0, sizeof(struct rt6_info));
	
	rt->u.dst.input = ip6_input;
	rt->u.dst.output = dev_queue_xmit;
	rt->rt6i_dev = dev_get("lo");
	rt->u.dst.pmtu = rt->rt6i_dev->mtu;

	rt->rt6i_flags = RTF_HOST | RTF_LOCAL | RTF_UP | RTF_NONEXTHOP;
	
	ipv6_addr_copy(&rt->rt6i_dst.addr, addr);
	rt->rt6i_dst.plen = 128;

	rt6_lock();
	rt6_ins(rt);
	rt6_unlock();

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
	rt6_ins(nrt);

	return nrt;
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
				struct nd_neigh *ndn;

				ndn = (struct nd_neigh *) rt->rt6i_nexthop;
				sprintf(arg->buffer + arg->len, "%02x",
					ndn->ndn_addr.s6_addr[i]);
				arg->len += 2;
			}
		} else {
			sprintf(arg->buffer + arg->len,
				"00000000000000000000000000000000");
			arg->len += 32;
		}
		arg->len += sprintf(arg->buffer + arg->len,
				    " %08lx %08x %08x %08lx %8s\n",
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
static struct proc_dir_entry proc_rt6_stats = {
	PROC_NET_RT6_STATS, 9, "rt6_stats",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	rt6_proc_stats
};
static struct proc_dir_entry proc_rt6_tree = {
	PROC_NET_RT6_TREE, 7, "ip6_fib",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	rt6_proc_tree
};
#endif	/* CONFIG_PROC_FS */

__initfunc(void ip6_route_init(void))
{
#ifdef 	CONFIG_PROC_FS
	proc_net_register(&proc_rt6_info);
	proc_net_register(&proc_rt6_stats);
	proc_net_register(&proc_rt6_tree);
#endif
	netlink_attach(NETLINK_ROUTE6, rt6_msgrcv);
}

#ifdef MODULE
void ip6_route_cleanup(void)
{
#ifdef CONFIG_PROC_FS
	proc_net_unregister(PROC_NET_RT6);
	proc_net_unregister(PROC_NET_RT6_TREE);
	proc_net_unregister(PROC_NET_RT6_STATS);
#endif
	netlink_detach(NETLINK_ROUTE6);
#if 0
	fib6_flush();
#endif
}
#endif	/* MODULE */

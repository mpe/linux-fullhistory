#ifndef _NET_IP6_ROUTE_H
#define _NET_IP6_ROUTE_H

#define IP6_RT_PRIO_FW		16
#define IP6_RT_PRIO_USER	1024
#define IP6_RT_PRIO_ADDRCONF	256
#define IP6_RT_PRIO_KERN	512
#define IP6_RT_FLOW_MASK	0x00ff

#ifdef __KERNEL__

#include <net/flow.h>
#include <net/ip6_fib.h>

/*
 *	Structure for assync processing of operations on the routing
 *	table
 */

struct rt6_req {
	int			operation;
	struct rt6_info		*ptr;

	struct rt6_req		*next;
	struct rt6_req		*prev;

#define RT_OPER_ADD		1
#define RT_OPER_DEL		2
};


struct pol_chain {
	int			type;
	int			priority;
	struct fib6_node	*rules;
	struct pol_chain	*next;
};

extern struct rt6_info	ip6_null_entry;

extern int ip6_rt_max_size;
extern int ip6_rt_gc_min;
extern int ip6_rt_gc_timeout;
extern int ip6_rt_gc_interval;

extern void			ip6_route_input(struct sk_buff *skb);

extern struct dst_entry *	ip6_route_output(struct sock *sk,
						 struct flowi *fl);

extern void			ip6_route_init(void);
extern void			ip6_route_cleanup(void);

extern int			ipv6_route_ioctl(unsigned int cmd, void *arg);

extern struct rt6_info *	ip6_route_add(struct in6_rtmsg *rtmsg,
					      int *err);
extern int			ip6_del_rt(struct rt6_info *);

extern int			ip6_rt_addr_add(struct in6_addr *addr,
						struct device *dev);

extern int			ip6_rt_addr_del(struct in6_addr *addr,
						struct device *dev);

extern void			rt6_sndmsg(int type, struct in6_addr *dst,
					   struct in6_addr *src,
					   struct in6_addr *gw,
					   struct device *dev, 
					   int dstlen, int srclen,
					   int metric, __u32 flags);

extern struct rt6_info		*rt6_lookup(struct in6_addr *daddr,
					    struct in6_addr *saddr,
					    struct device *dev, int flags);

/*
 *	support functions for ND
 *
 */
extern struct rt6_info *	rt6_get_dflt_router(struct in6_addr *addr,
						    struct device *dev);
extern struct rt6_info *	rt6_add_dflt_router(struct in6_addr *gwaddr,
						    struct device *dev);

extern void			rt6_purge_dflt_routers(int lst_resort);

extern struct rt6_info *	rt6_redirect(struct in6_addr *dest,
					     struct in6_addr *saddr,
					     struct in6_addr *target,
					     struct device *dev,
					     int on_link);

extern void			rt6_pmtu_discovery(struct in6_addr *addr,
						   struct device *dev,
						   int pmtu);

struct nlmsghdr;
struct netlink_callback;
extern int inet6_dump_fib(struct sk_buff *skb, struct netlink_callback *cb);
extern int inet6_rtm_newroute(struct sk_buff *skb, struct nlmsghdr* nlh, void *arg);
extern int inet6_rtm_delroute(struct sk_buff *skb, struct nlmsghdr* nlh, void *arg);

extern void rt6_ifdown(struct device *dev);

/*
 *	Store a destination cache entry in a socket
 *	For UDP/RAW sockets this is done on udp_connect.
 */

extern __inline__ void ip6_dst_store(struct sock *sk, struct dst_entry *dst)
{
	struct ipv6_pinfo *np;
	struct rt6_info *rt;
		
	np = &sk->net_pinfo.af_inet6;
	sk->dst_cache = dst;
	
	rt = (struct rt6_info *) dst;
	
	np->dst_cookie = rt->rt6i_node ? rt->rt6i_node->fn_sernum : 0;
}

#endif
#endif

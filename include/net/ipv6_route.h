/*
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _NET_IPV6_ROUTE_H
#define _NET_IPV6_ROUTE_H

#include <linux/ipv6_route.h>


#ifdef __KERNEL__


struct fib6_node {
	struct fib6_node	*parent;
	struct fib6_node	*left;
	struct fib6_node	*right;

	struct rt6_info		*leaf;

	__u16			fn_bit;		/* bit key */
	__u16			fn_flags;
	__u32			fn_sernum;
};


struct rt6_info;

typedef void (*rt6_output_method_t) (struct sk_buff *skb, struct rt6_info *rt);

struct rt6_info {
	struct fib6_node	*fib_node;
	struct rt6_info		*next;

	struct in6_addr		rt_dst;
	
	atomic_t		rt_use;		/* dcache references	*/
	atomic_t		rt_ref;		/* fib references	*/

	struct neighbour        *rt_nexthop;
	struct device		*rt_dev;
	
	rt6_output_method_t	rt_output_method;

	__u16			rt_metric;
	__u16			rt_prefixlen;
	__u32			rt_flags;
	unsigned long		rt_expires;
};

extern struct rt6_info		*default_rt_list;
extern struct rt6_info		*last_resort_rt;

struct dest_entry {
	struct rt6_info		rt;

	__u32			dc_irtt;
	__u32			dc_window;
	__u16			dc_pmtu;

	unsigned long		dc_tstamp;	/* for garbage collection */

#define dc_addr			rt.rt_dst
#define dc_usecnt		rt.rt_use
#define dc_nexthop		rt.rt_nexthop
#define dc_flags		rt.rt_flags
};

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

struct rt6_statistics {
	__u32		fib_nodes;
	__u32		fib_route_nodes;
	__u32		fib_rt_alloc;
	__u32		fib_rt_entries;
	__u32		fib_dc_alloc;
};

#define RTN_ROOT	0x0001		/* root node			*/
#define RTN_BACKTRACK	0x0002		/* backtrack point		*/
#define RTN_TAG		0x0010

/*
 *	Values for destination cache garbage colection
 *	These are wild guesses for now...
 */

#define	DC_WATER_MARK		512
#define DC_SHORT_TIMEOUT	(5*HZ)
#define DC_LONG_TIMEOUT	        (15*HZ)

#define DC_TIME_RUN		(5*HZ)
#define DC_TIME_RETRY		HZ

/*
 *	Prototypes
 */

/*
 *	check/obtain destination cache from routing table
 */

extern struct dest_entry *	ipv6_dst_check(struct dest_entry *dc, 
					       struct in6_addr * daddr,
					       __u32 sernum, int flags);

extern struct dest_entry *	ipv6_dst_route(struct in6_addr * daddr,
					       struct device *src_dev,
					       int flags);

extern void			ipv6_dst_unlock(struct dest_entry *dest);

extern struct rt6_info *	fibv6_lookup(struct in6_addr *addr,
					     struct device *dev,
					     int flags);

/*
 *	user space set/del route
 */

extern int			ipv6_route_ioctl(unsigned int cmd, void *arg);


extern void			ipv6_route_init(void);
extern void			ipv6_route_cleanup(void);

extern int			ipv6_route_add(struct in6_rtmsg *rt);

extern int			fib6_del_rt(struct rt6_info *rt);

extern void			rt6_sndmsg(__u32 type, struct in6_addr *dst,
					   struct in6_addr *gw, __u16 plen,
					   __u16 metric, char *devname,
					   __u16 flags);
/*
 *	ICMP interface
 */

extern struct rt6_info *	ipv6_rt_redirect(struct device *dev,
						 struct in6_addr *dest,
						 struct in6_addr *target,
						 int on_link);

extern void			rt6_handle_pmtu(struct in6_addr *addr,
						int pmtu);
/*
 *
 */

extern struct fib6_node		routing_table;
extern struct rt6_statistics	rt6_stats;

static __inline__ void rt_release(struct rt6_info *rt)
{
	atomic_dec(&rt->rt_ref);
	if ((rt->rt_use | rt->rt_ref) == 0)
	{
		if (rt->rt_nexthop)
		{
			ndisc_dec_neigh(rt->rt_nexthop);
		}

		if (rt->rt_flags & RTI_DCACHE)
		{
			rt6_stats.fib_dc_alloc--;
		}
		rt6_stats.fib_rt_alloc--;
		kfree(rt);
	}
}

#endif

#endif

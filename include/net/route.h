/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET  is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the IP router.
 *
 * Version:	@(#)route.h	1.0.4	05/27/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 * Fixes:
 *		Alan Cox	:	Reformatted. Added ip_rt_local()
 *		Alan Cox	:	Support for TCP parameters.
 *		Alexey Kuznetsov:	Major changes for new routing code.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _ROUTE_H
#define _ROUTE_H

#include <net/ip_fib.h>
#include <net/dst.h>


#define RT_HASH_DIVISOR	    	256
#define RT_CACHE_MAX_SIZE    	256

/*
 * Maximal time to live for unused entry.
 */
#define RT_CACHE_TIMEOUT		(HZ*300)

/*
 * Cache invalidations can be delayed by:
 */
#define RT_FLUSH_DELAY (2*HZ)

#define RT_REDIRECT_NUMBER		9
#define RT_REDIRECT_LOAD		(HZ/50)	/* 20 msec */
#define RT_REDIRECT_SILENCE		(RT_REDIRECT_LOAD<<(RT_REDIRECT_NUMBER+1))
                                                /* 20sec */

#define RT_ERROR_LOAD			(1*HZ)


/*
 * Prevents LRU trashing, entries considered equivalent,
 * if the difference between last use times is less then this number.
 */
#define RT_CACHE_BUBBLE_THRESHOLD	(5*HZ)

#include <linux/route.h>

struct rtable 
{
	union
	{
		struct dst_entry	dst;
		struct rtable		*rt_next;
	} u;

	unsigned		rt_flags;

	u32			rt_dst;		/* Path destination	*/
	u32			rt_src;		/* Path source		*/
	struct device		*rt_src_dev;	/* Path source device	*/

	/* Info on neighbour */
	u32			rt_gateway;

	/* Cache lookup keys */
	struct
	{
		u32			dst;
		u32			src;
		struct device		*src_dev;
		struct device		*dst_dev;
		u8			tos;
	} key;

	/* Miscellaneous cached information */
	u32			rt_spec_dst;	/* RFC1122 specific destination */
	u32			rt_src_map;
	u32			rt_dst_map;

	/* ICMP statistics */
	unsigned long		last_error;
	unsigned long		errors;
};


#define RTF_IFBRD	(RTF_UP|RTF_MAGIC|RTF_LOCAL|RTF_BROADCAST)
#define RTF_IFLOCAL	(RTF_UP|RTF_MAGIC|RTF_LOCAL|RTF_INTERFACE)
#define RTF_IFPREFIX	(RTF_UP|RTF_MAGIC|RTF_INTERFACE)

/*
 *	Flags not visible at user level.
 */
#define RTF_INTERNAL	0xFFFF8000	/* to get RTF_MAGIC as well... */

/*
 *	Flags saved in FIB.
 */
#define RTF_FIB		(RTF_UP|RTF_GATEWAY|RTF_REJECT|RTF_THROW|RTF_STATIC|\
			 RTF_XRESOLVE|RTF_NOPMTUDISC|RTF_NOFORWARD|RTF_INTERNAL)

extern void		ip_rt_init(void);
extern void		ip_rt_redirect(u32 old_gw, u32 dst, u32 new_gw,
				       u32 src, u8 tos, struct device *dev);
extern void		ip_rt_check_expire(void);
extern void		ip_rt_advice(struct rtable **rp, int advice);
extern void		rt_cache_flush(int how);
extern int		ip_route_output(struct rtable **, u32 dst, u32 src, u8 tos, struct device *devout);
extern int		ip_route_output_dev(struct rtable **, u32 dst, u32 src, u8 tos, int);
extern int		ip_route_input(struct sk_buff*, u32 dst, u32 src, u8 tos, struct device *devin);
extern unsigned short	ip_rt_frag_needed(struct iphdr *iph, unsigned short new_mtu);
extern void		ip_rt_send_redirect(struct sk_buff *skb);

static __inline__ void ip_rt_put(struct rtable * rt)
{
	if (rt)
		dst_release(&rt->u.dst);
}

static __inline__ char rt_tos2priority(u8 tos)
{
	if (tos & IPTOS_LOWDELAY)
		return SOPRI_INTERACTIVE;
	if (tos & (IPTOS_THROUGHPUT|IPTOS_MINCOST))
		return SOPRI_BACKGROUND;
	return SOPRI_NORMAL;
}


static __inline__ int ip_route_connect(struct rtable **rp, u32 dst, u32 src, u32 tos)
{
	int err;
	err = ip_route_output(rp, dst, src, tos, NULL);
	if (err || (dst && src))
		return err;
	dst = (*rp)->rt_dst;
	src = (*rp)->rt_src;
	ip_rt_put(*rp);
	*rp = NULL;
	return ip_route_output(rp, dst, src, tos, NULL);
}

static __inline__ void ip_ll_header(struct sk_buff *skb)
{
	struct rtable *rt = (struct rtable*)skb->dst;
	struct device *dev = rt->u.dst.dev;
	struct hh_cache *hh = rt->u.dst.hh;
	int hh_len = dev->hard_header_len;

	skb->dev = dev;
	skb->arp = 1;
	skb->protocol = htons(ETH_P_IP);

	if (hh) {
		memcpy(skb_push(skb, hh_len), hh->hh_data, hh_len);
		skb->arp = hh->hh_uptodate;
	} else if (dev->hard_header &&
		   dev->hard_header(skb, dev, ETH_P_IP, NULL, NULL, 0)<0)
		skb->arp = 0;
		
	skb->mac.raw = skb->data;
}


#endif	/* _ROUTE_H */

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
 *	FIXME:
 *		Make atomic ops more generic and hide them in asm/...
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _ROUTE_H
#define _ROUTE_H

#include <linux/config.h>

/*
 * 0 - no debugging messages
 * 1 - rare events and bugs situations (default)
 * 2 - trace mode.
 */
#define RT_CACHE_DEBUG		0

#define RT_HASH_DIVISOR	    	256
#define RT_CACHE_SIZE_MAX    	256

#define RTZ_HASH_DIVISOR	256

#if RT_CACHE_DEBUG >= 2
#define RTZ_HASHING_LIMIT 0
#else
#define RTZ_HASHING_LIMIT 16
#endif

/*
 * Maximal time to live for unused entry.
 */
#define RT_CACHE_TIMEOUT		(HZ*300)

/*
 * Prevents LRU trashing, entries considered equivalent,
 * if the difference between last use times is less then this number.
 */
#define RT_CACHE_BUBBLE_THRESHOLD	(HZ*5)

#include <linux/route.h>

#ifdef __KERNEL__
#define RTF_LOCAL 0x8000
#endif

struct rtable 
{
	struct rtable		*rt_next;
	__u32			rt_dst;
	__u32			rt_src;
	__u32			rt_gateway;
	atomic_t		rt_refcnt;
	atomic_t		rt_use;
	unsigned long		rt_window;
	atomic_t		rt_lastuse;
	struct hh_cache		*rt_hh;
	struct device		*rt_dev;
	unsigned short		rt_flags;
	unsigned short		rt_mtu;
	unsigned short		rt_irtt;
	unsigned char		rt_tos;
};

extern void		ip_rt_flush(struct device *dev);
extern void		ip_rt_update(int event, struct device *dev);
extern void		ip_rt_redirect(__u32 src, __u32 dst, __u32 gw, struct device *dev);
extern struct rtable	*ip_rt_slow_route(__u32 daddr, int local);
extern int		rt_get_info(char * buffer, char **start, off_t offset, int length, int dummy);
extern int		rt_cache_get_info(char *buffer, char **start, off_t offset, int length, int dummy);
extern int		ip_rt_ioctl(unsigned int cmd, void *arg);
extern int		ip_rt_new(struct rtentry *rt);
extern int		ip_rt_kill(struct rtentry *rt);
extern void		ip_rt_check_expire(void);
extern void		ip_rt_advice(struct rtable **rp, int advice);

extern void		ip_rt_run_bh(void);
extern atomic_t	    	ip_rt_lock;
extern unsigned		ip_rt_bh_mask;
extern struct rtable 	*ip_rt_hash_table[RT_HASH_DIVISOR];

extern __inline__ void ip_rt_fast_lock(void)
{
	atomic_inc(&ip_rt_lock);
}

extern __inline__ void ip_rt_fast_unlock(void)
{
	atomic_dec(&ip_rt_lock);
}

extern __inline__ void ip_rt_unlock(void)
{
	if (atomic_dec_and_test(&ip_rt_lock) && ip_rt_bh_mask)
		ip_rt_run_bh();
}

extern __inline__ unsigned ip_rt_hash_code(__u32 addr)
{
	unsigned tmp = addr + (addr>>16);
	return (tmp + (tmp>>8)) & 0xFF;
}


extern __inline__ void ip_rt_put(struct rtable * rt)
#ifndef MODULE
{
	if (rt)
		atomic_dec(&rt->rt_refcnt);
}
#else
;
#endif

#ifdef CONFIG_KERNELD
extern struct rtable * ip_rt_route(__u32 daddr, int local);
#else
extern __inline__ struct rtable * ip_rt_route(__u32 daddr, int local)
#ifndef MODULE
{
	struct rtable * rth;

	ip_rt_fast_lock();

	for (rth=ip_rt_hash_table[ip_rt_hash_code(daddr)^local]; rth; rth=rth->rt_next)
	{
		if (rth->rt_dst == daddr)
		{
			rth->rt_lastuse = jiffies;
			atomic_inc(&rth->rt_use);
			atomic_inc(&rth->rt_refcnt);
			ip_rt_unlock();
			return rth;
		}
	}
	return ip_rt_slow_route (daddr, local);
}
#else
;
#endif
#endif

extern __inline__ struct rtable * ip_check_route(struct rtable ** rp,
						       __u32 daddr, int local)
{
	struct rtable * rt = *rp;

	if (!rt || rt->rt_dst != daddr || !(rt->rt_flags&RTF_UP)
	    || ((local==1)^((rt->rt_flags&RTF_LOCAL) != 0)))
	{
		ip_rt_put(rt);
		rt = ip_rt_route(daddr, local);
		*rp = rt;
	}
	return rt;
}	


#endif	/* _ROUTE_H */

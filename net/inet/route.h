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
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _ROUTE_H
#define _ROUTE_H


#include <linux/route.h>


/* This is an entry in the IP routing table. */
struct rtable {
  struct rtable		*rt_next;
  unsigned long		rt_dst;
  unsigned long		rt_gateway;
  u_char		rt_flags;
  u_char		rt_metric;
  short			rt_refcnt;
  u_long		rt_use;
  struct device		*rt_dev;
};


extern void		rt_flush(struct device *dev);
extern void		rt_add(short flags, unsigned long addr,
			       unsigned long gw, struct device *dev);
extern struct rtable	*rt_route(unsigned long daddr, struct options *opt);
extern int		rt_get_info(char * buffer);
extern int		rt_ioctl(unsigned int cmd, void *arg);

#endif	/* _ROUTE_H */

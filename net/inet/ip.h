/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the IP module.
 *
 * Version:	@(#)ip.h	1.0.2	05/07/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _IP_H
#define _IP_H


#include <linux/ip.h>

extern int		backoff(int n);

extern void		ip_print(struct iphdr *ip);
extern int		ip_ioctl(struct sock *sk, int cmd,
				 unsigned long arg);
extern void		ip_route_check(unsigned long daddr);
extern int		ip_build_header(struct sk_buff *skb,
					unsigned long saddr,
					unsigned long daddr,
					struct device **dev, int type,
					struct options *opt, int len);
extern unsigned short	ip_compute_csum(unsigned char * buff, int len);
extern int		ip_rcv(struct sk_buff *skb, struct device *dev,
			       struct packet_type *pt);
extern void		ip_queue_xmit(struct sock *sk,
				      struct device *dev, struct sk_buff *skb,
				      int free);
extern void		ip_retransmit(struct sock *sk, int all);

#endif	/* _IP_H */

/*
 *	inet6 interface/address list definitions
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _NET_IF_INET6_H
#define _NET_IF_INET6_H

#define DAD_COMPLETE	0x00
#define DAD_INCOMPLETE	0x01
#define DAD_STATUS	0x01

#define ADDR_STATUS	0x06
#define ADDR_DEPRECATED 0x02
#define ADDR_INVALID	0x04

#define ADDR_PERMANENT	0x80

#define IF_RA_RCVD	0x20
#define IF_RS_SENT	0x10

#ifdef __KERNEL__

struct inet6_ifaddr 
{
	struct in6_addr		addr;
	__u32			prefix_len;
	
	__u32			valid_lft;
	__u32			prefered_lft;
	unsigned long		tstamp;

	__u8			probes;
	__u8			flags;

	__u16			scope;

	struct timer_list	timer;

	struct inet6_dev	*idev;

	struct inet6_ifaddr	*lst_next;      /* next addr in addr_lst */
	struct inet6_ifaddr	*if_next;       /* next addr in inet6_dev */
};


struct ipv6_mc_socklist {
	struct in6_addr		addr;
	struct device		*dev;
	struct ipv6_mc_socklist *next;
};

struct ipv6_mc_list {
	struct in6_addr		addr;
	struct device		*dev;
	struct ipv6_mc_list	*next;
	struct ipv6_mc_list	*if_next;
	struct timer_list	timer;
        int			tm_running;
        atomic_t		users;	
};

#define	IFA_HOST	IPV6_ADDR_LOOPBACK
#define	IFA_LINK	IPV6_ADDR_LINKLOCAL
#define	IFA_SITE	IPV6_ADDR_SITELOCAL
#define	IFA_GLOBAL	0x0000U

extern int		in6_ifnum;

struct inet6_dev 
{
	struct device		*dev;

	struct inet6_ifaddr	*addr_list;
	struct ipv6_mc_list	*mc_list;

	__u32			if_index;
	__u32			if_flags;
	__u32			router:1,
				unused:31;

	struct inet6_dev	*next;
};


extern __inline__ void ipv6_mc_map(struct in6_addr *addr, char *buf)
{
	/*
	 *	+-------+-------+-------+-------+-------+-------+
	 *      |   33  |   33  | DST13 | DST14 | DST15 | DST16 |
	 *      +-------+-------+-------+-------+-------+-------+
	 */

	buf[0]= 0x33;
	buf[1]= 0x33;

	memcpy(buf + 2, &addr->s6_addr32[3], sizeof(__u32));
}
#endif
#endif

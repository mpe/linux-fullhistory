/*
 *	Types and definitions for AF_INET6 
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Sources:
 *	IPv6 Program Interfaces for BSD Systems
 *      <draft-ietf-ipngwg-bsd-api-05.txt>
 *
 *	Advanced Sockets API for IPv6
 *	<draft-stevens-advanced-api-00.txt>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_IN6_H
#define _LINUX_IN6_H


/*
 *	IPv6 address structure
 */

struct in6_addr
{
	union 
	{
		__u8		u6_addr8[16];
		__u32		u6_addr32[4];
#if (~0UL) > 0xffffffff
		__u64		u6_addr64[2];
#endif
	} in6_u;
#define s6_addr			in6_u.u6_addr8
#define s6_addr32		in6_u.u6_addr32
#define s6_addr64		in6_u.u6_addr64
};

struct sockaddr_in6 {
	unsigned short int	sin6_family;    /* AF_INET6 */
	__u16			sin6_port;      /* Transport layer port # */
	__u32			sin6_flowinfo;  /* IPv6 flow information */
	struct in6_addr		sin6_addr;      /* IPv6 address */
};


struct ipv6_mreq {
	/* IPv6 multicast address of group */
	struct in6_addr ipv6mr_multiaddr;

	/* local IPv6 address of interface */
	int		ipv6mr_ifindex;
};

/*
 *	Bitmask constant declarations to help applications select out the 
 *	flow label and priority fields.
 *
 *	Note that this are in host byte order while the flowinfo field of
 *	sockaddr_in6 is in network byte order.
 */

#define IPV6_FLOWINFO_FLOWLABEL		0x00ff
#define IPV6_FLOWINFO_PRIORITY		0x0f00

#define IPV6_PRIORITY_UNCHARACTERIZED	0x0000
#define IPV6_PRIORITY_FILLER		0x0100
#define IPV6_PRIORITY_UNATTENDED	0x0200
#define IPV6_PRIORITY_RESERVED1		0x0300
#define IPV6_PRIORITY_BULK		0x0400
#define IPV6_PRIORITY_RESERVED2		0x0500
#define IPV6_PRIORITY_INTERACTIVE	0x0600
#define IPV6_PRIORITY_CONTROL		0x0700
#define IPV6_PRIORITY_8			0x0800
#define IPV6_PRIORITY_9			0x0900
#define IPV6_PRIORITY_10		0x0a00
#define IPV6_PRIORITY_11		0x0b00
#define IPV6_PRIORITY_12		0x0c00
#define IPV6_PRIORITY_13		0x0d00
#define IPV6_PRIORITY_14		0x0e00
#define IPV6_PRIORITY_15		0x0f00

/*
 *	IPV6 socket options
 */

#define IPV6_ADDRFORM		1
#define IPV6_RXINFO		2
#define IPV6_RXHOPOPTS		3
#define IPV6_RXDSTOPTS		4
#define IPV6_RXSRCRT		5
#define IPV6_PKTOPTIONS		6
#define IPV6_CHECKSUM		7
#define IPV6_HOPLIMIT		8

/*
 *	Alternative names
 */
#define IPV6_TXINFO		IPV6_RXINFO
#define SCM_SRCINFO		IPV6_TXINFO
#define SCM_SRCRT		IPV6_RXSRCRT

#define IPV6_UNICAST_HOPS	16
#define IPV6_MULTICAST_IF	17
#define IPV6_MULTICAST_HOPS	18
#define IPV6_MULTICAST_LOOP	19
#define IPV6_ADD_MEMBERSHIP	20
#define IPV6_DROP_MEMBERSHIP	21

#endif

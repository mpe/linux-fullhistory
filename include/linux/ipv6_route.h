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

#ifndef _LINUX_IPV6_ROUTE_H
#define _LINUX_IPV6_ROUTE_H


#define RTF_DEFAULT	0x00010000	/* default - learned via ND	*/
#define RTF_ALLONLINK	0x00020000	/* fallback, no routers on link	*/
#define RTF_ADDRCONF	0x00040000	/* addrconf route - RA		*/

#define RTF_LINKRT	0x00100000	/* link specific - device match	*/
#define RTF_NONEXTHOP	0x00200000	/* route with no nexthop	*/

#define RTF_CACHE	0x01000000	/* cache entry			*/
#define RTF_FLOW	0x02000000	/* flow significant route	*/
#define RTF_POLICY	0x04000000	/* policy route			*/

struct in6_rtmsg {
	struct in6_addr		rtmsg_dst;
	struct in6_addr		rtmsg_src;
	struct in6_addr		rtmsg_gateway;
	__u32			rtmsg_type;
	__u16			rtmsg_dst_len;
	__u16			rtmsg_src_len;
	__u32			rtmsg_metric;
	unsigned long		rtmsg_info;
        __u32			rtmsg_flags;
	int			rtmsg_ifindex;
};

#endif

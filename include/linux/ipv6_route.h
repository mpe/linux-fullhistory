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

#define RTI_DEVRT	0x00010000	/* route lookup, dev must match	*/
#define RTI_ALLONLINK	0x00020000	/* all destinations on link	*/
#define RTI_DCACHE	RTF_DCACHE	/* rt6_info is a dcache entry	*/
#define RTI_INVALID	RTF_INVALID	/* invalid route/dcache entry	*/

#define RTI_DYNAMIC	RTF_DYNAMIC	/* rt6_info created dynamicly	*/
#define RTI_GATEWAY	RTF_GATEWAY
#define RTI_DYNMOD	RTF_MODIFIED	/* more specific route may exist*/

#define DCF_PMTU	RTF_MSS		/* dest cache has valid PMTU	*/
#define DCF_INVALID	RTF_INVALID

struct in6_rtmsg {
	struct in6_addr		rtmsg_dst;
	struct in6_addr		rtmsg_gateway;
	__u32			rtmsg_type;
	__u16			rtmsg_prefixlen;
	__u16			rtmsg_metric;
	unsigned long		rtmsg_info;
        __u32			rtmsg_flags;
	int			rtmsg_ifindex;
};

#endif

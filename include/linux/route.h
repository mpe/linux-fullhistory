/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Global definitions for the IP router interface.
 *
 * Version:	@(#)route.h	1.0.3	05/27/93
 *
 * Authors:	Original taken from Berkeley UNIX 4.3, (c) UCB 1986-1988
 *		for the purposes of compatibility only.
 *
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_ROUTE_H
#define _LINUX_ROUTE_H

#include <linux/if.h>


/* This structure gets passed by the SIOCADDRT and SIOCDELRT calls. */
struct rtentry 
{
	unsigned long	rt_pad1;
	struct sockaddr	rt_dst;		/* target address		*/
	struct sockaddr	rt_gateway;	/* gateway addr (RTF_GATEWAY)	*/
	struct sockaddr	rt_genmask;	/* target network mask (IP)	*/
	unsigned short	rt_flags;
	short		rt_pad2;
	unsigned long	rt_pad3;
	unsigned char	rt_tos;
	unsigned char	rt_class;
	short		rt_pad4;
	short		rt_metric;	/* +1 for binary compatibility!	*/
	char		*rt_dev;	/* forcing the device at add	*/
	unsigned long	rt_mtu;		/* per route MTU/Window 	*/
#ifndef __KERNEL__
#define rt_mss	rt_mtu			/* Compatibility :-(            */
#endif
	unsigned long	rt_window;	/* Window clamping 		*/
	unsigned short	rt_irtt;	/* Initial RTT			*/
	
};


#define	RTF_UP		0x0001		/* route usable		  	*/
#define	RTF_GATEWAY	0x0002		/* destination is a gateway	*/

#define	RTF_HOST	0x0004		/* host entry (net otherwise)	*/
#define RTF_REINSTATE	0x0008		/* reinstate route after tmout	*/
#define	RTF_DYNAMIC	0x0010		/* created dyn. (by redirect)	*/
#define	RTF_MODIFIED	0x0020		/* modified dyn. (by redirect)	*/
#define RTF_MTU		0x0040		/* specific MTU for this route	*/
#define RTF_MSS		RTF_MTU		/* Compatibility :-(		*/
#define RTF_WINDOW	0x0080		/* per route window clamping	*/
#define RTF_IRTT	0x0100		/* Initial round trip time	*/
#define RTF_REJECT	0x0200		/* Reject route			*/
#define	RTF_STATIC	0x0400		/* Manually injected route	*/
#define	RTF_XRESOLVE	0x0800		/* External resolver		*/
#define RTF_NOFORWARD   0x1000		/* Forwarding inhibited		*/
#define RTF_THROW	0x2000		/* Go to next class		*/
#define RTF_NOPMTUDISC  0x4000		/* Do not send packets with DF	*/

#define RTF_MAGIC	0x8000		/* Route added/deleted authomatically,
					 * when interface changes its state.
					 */

/*
 *	<linux/ipv6_route.h> uses RTF values >= 64k
 */

#define RTCF_VALVE	0x00200000
#define RTCF_MASQ	0x00400000
#define RTCF_NAT	0x00800000
#define RTCF_DOREDIRECT 0x01000000
#define RTCF_LOG	0x02000000
#define RTCF_DIRECTSRC	0x04000000

#define RTF_LOCAL	0x80000000
#define RTF_INTERFACE	0x40000000
#define RTF_MULTICAST	0x20000000
#define RTF_BROADCAST	0x10000000
#define RTF_NAT		0x08000000

#define RTF_ADDRCLASSMASK	0xF8000000
#define RT_ADDRCLASS(flags)	((__u32)flags>>23)

#define RT_TOS(tos)		((tos)&IPTOS_TOS_MASK)

#define RT_LOCALADDR(flags)	((flags&RTF_ADDRCLASSMASK) == (RTF_LOCAL|RTF_INTERFACE))

#define RT_CLASS_UNSPEC		0
#define RT_CLASS_DEFAULT	253

#define RT_CLASS_MAIN		254
#define RT_CLASS_LOCAL		255
#define RT_CLASS_MAX		255

#ifdef _LINUX_IN_H 	/* hack to check that in.h included */
/*
 *	This structure is passed from the kernel to user space by netlink
 *	routing/device announcements
 */

struct in_rtmsg
{
	struct in_addr	rtmsg_prefix;
	struct in_addr	rtmsg_gateway;
	unsigned	rtmsg_flags;
	unsigned long	rtmsg_mtu;
	unsigned long	rtmsg_window;
	unsigned short	rtmsg_rtt;
	short		rtmsg_metric;
	unsigned char	rtmsg_tos;
	unsigned char	rtmsg_class;
	unsigned char	rtmsg_prefixlen;
	unsigned char	rtmsg_reserved;
	int		rtmsg_ifindex;
};


struct in_ifmsg
{
	struct sockaddr ifmsg_lladdr;
	struct in_addr	ifmsg_prefix;
	struct in_addr	ifmsg_brd;
	unsigned	ifmsg_flags;
	unsigned long	ifmsg_mtu;
	short		ifmsg_metric;
	unsigned char	ifmsg_prefixlen;
	unsigned char	ifmsg_reserved;
	int		ifmsg_index;
	char		ifmsg_name[16];
};

enum rtrule_actions
{
	RTP_GO,
	RTP_NAT,
	RTP_DROP,
	RTP_UNREACHABLE,
	RTP_PROHIBIT,
	RTP_MASQUERADE
};

#define RTRF_LOG		1	/* Log route creations		*/
#define RTRF_VALVE		2	/* One-way route		*/

struct in_rtrulemsg
{
	struct in_addr	rtrmsg_src;
	struct in_addr	rtrmsg_dst;
	struct in_addr	rtrmsg_srcmap;
	int		rtrmsg_ifindex;
	unsigned char	rtrmsg_srclen;
	unsigned char	rtrmsg_dstlen;
	unsigned char	rtrmsg_tos;
	unsigned char	rtrmsg_class;
	unsigned char	rtrmsg_flags;
	unsigned char	rtrmsg_action;
	unsigned char	rtrmsg_preference;
	unsigned char	rtrmsg_rtmsgs;
	struct in_rtmsg rtrmsg_rtmsg[1];
};

struct in_rtctlmsg
{
        unsigned	rtcmsg_flags;
        int		rtcmsg_delay;
};

#define RTCTL_ECHO	1	/* Echo route changes */
#define RTCTL_FLUSH	2	/* Send flush updates */
#define RTCTL_ACK	4	/* Send acks	      */
#define RTCTL_DELAY	8	/* Set netlink delay  */
#define RTCTL_OWNER	0x10	/* Set netlink reader */
#endif

#define RTMSG_ACK		NLMSG_ACK
#define RTMSG_OVERRUN		NLMSG_OVERRUN

#define RTMSG_NEWDEVICE		0x11
#define RTMSG_DELDEVICE		0x12
#define RTMSG_NEWROUTE		0x21
#define RTMSG_DELROUTE		0x22
#define RTMSG_NEWRULE		0x31
#define RTMSG_DELRULE		0x32
#define RTMSG_CONTROL		0x40

#define RTMSG_AR_FAILED		0x51	/* Address Resolution failed	*/
#endif	/* _LINUX_ROUTE_H */


/*
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	$Id: ipv6.h,v 1.8 1997/12/29 19:52:09 kuznet Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _NET_IPV6_H
#define _NET_IPV6_H

#include <linux/ipv6.h>
#include <asm/hardirq.h>
#include <net/ndisc.h>
#include <net/flow.h>

/*
 *	NextHeader field of IPv6 header
 */

#define NEXTHDR_HOP		0	/* Hop-by-hop option header. */
#define NEXTHDR_TCP		6	/* TCP segment. */
#define NEXTHDR_UDP		17	/* UDP message. */
#define NEXTHDR_IPV6		41	/* IPv6 in IPv6 */
#define NEXTHDR_ROUTING		43	/* Routing header. */
#define NEXTHDR_FRAGMENT	44	/* Fragmentation/reassembly header. */
#define NEXTHDR_ESP		50	/* Encapsulating security payload. */
#define NEXTHDR_AUTH		51	/* Authentication header. */
#define NEXTHDR_ICMP		58	/* ICMP for IPv6. */
#define NEXTHDR_NONE		59	/* No next header */
#define NEXTHDR_DEST		60	/* Destination options header. */

#define NEXTHDR_MAX		255



#define IPV6_DEFAULT_HOPLIMIT   64
#define IPV6_DEFAULT_MCASTHOPS	1

/*
 *	Addr type
 *	
 *	type	-	unicast | multicast | anycast
 *	scope	-	local	| site	    | global
 *	v4	-	compat
 *	v4mapped
 *	any
 *	loopback
 */

#define IPV6_ADDR_ANY		0x0000U

#define IPV6_ADDR_UNICAST      	0x0001U	
#define IPV6_ADDR_MULTICAST    	0x0002U	
#define IPV6_ADDR_ANYCAST	0x0004U

#define IPV6_ADDR_LOOPBACK	0x0010U
#define IPV6_ADDR_LINKLOCAL	0x0020U
#define IPV6_ADDR_SITELOCAL	0x0040U

#define IPV6_ADDR_COMPATv4	0x0080U

#define IPV6_ADDR_SCOPE_MASK	0x00f0U

#define IPV6_ADDR_MAPPED	0x1000U
#define IPV6_ADDR_RESERVED	0x2000U	/* reserved address space */

/*
 *	fragmentation header
 */

struct frag_hdr {
	unsigned char	nexthdr;
	unsigned char	reserved;	
	unsigned short	frag_off;
	__u32		identification;
};

#ifdef __KERNEL__

#include <net/sock.h>

extern struct ipv6_mib	ipv6_statistics;

struct ipv6_config {
	int		forwarding;
	int		hop_limit;
	int		accept_ra;
	int		accept_redirects;
	
	int		autoconf;
	int		dad_transmits;
	int		rtr_solicits;
	int		rtr_solicit_interval;
	int		rtr_solicit_delay;

	int		rt_cache_timeout;
	int		rt_gc_period;
};

extern struct ipv6_config ipv6_config;

struct ipv6_frag {
	__u16			offset;
	__u16			len;
	struct sk_buff		*skb;

	struct frag_hdr		*fhdr;

	struct ipv6_frag	*next;
};

/*
 *	Equivalent of ipv4 struct ipq
 */

struct frag_queue {

	struct frag_queue	*next;
	struct frag_queue	*prev;

	__u32			id;		/* fragment id		*/
	struct timer_list	timer;		/* expire timer		*/
	struct ipv6_frag	*fragments;
	struct device		*dev;
	__u8			last_in;	/* has last segment arrived? */
	__u8			nexthdr;
	__u8			*nhptr;
};

extern int			ipv6_routing_header(struct sk_buff **skb, 
						    struct device *dev,
						    __u8 *nhptr, 
						    struct ipv6_options *opt);

extern int			ipv6_reassembly(struct sk_buff **skb, 
						struct device *dev, 
						__u8 *nhptr,
						struct ipv6_options *opt);

#define IPV6_FRAG_TIMEOUT	(60*HZ)		/* 60 seconds */

/*
 *	Function prototype for build_xmit
 */

typedef int		(*inet_getfrag_t) (const void *data,
					   struct in6_addr *addr,
					   char *,
					   unsigned int, unsigned int);


extern int		ipv6_addr_type(struct in6_addr *addr);

extern __inline__ int ipv6_addr_scope(struct in6_addr *addr)
{
	return ipv6_addr_type(addr) & IPV6_ADDR_SCOPE_MASK;
}

extern __inline__ int ipv6_addr_cmp(struct in6_addr *a1, struct in6_addr *a2)
{
	return memcmp((void *) a1, (void *) a2, sizeof(struct in6_addr));
}

extern __inline__ void ipv6_addr_copy(struct in6_addr *a1, struct in6_addr *a2)
{
	memcpy((void *) a1, (void *) a2, sizeof(struct in6_addr));
}

#ifndef __HAVE_ARCH_ADDR_SET
extern __inline__ void ipv6_addr_set(struct in6_addr *addr, 
				     __u32 w1, __u32 w2,
				     __u32 w3, __u32 w4)
{
	addr->s6_addr32[0] = w1;
	addr->s6_addr32[1] = w2;
	addr->s6_addr32[2] = w3;
	addr->s6_addr32[3] = w4;
}
#endif

extern __inline__ int ipv6_addr_any(struct in6_addr *a)
{
	return ((a->s6_addr32[0] | a->s6_addr32[1] | 
		 a->s6_addr32[2] | a->s6_addr32[3] ) == 0); 
}

extern __inline__ int gfp_any(void)
{
	int pri = GFP_KERNEL;
	if (in_interrupt())
		pri = GFP_ATOMIC;
	return pri;
}

/*
 *	Prototypes exported by ipv6
 */

/*
 *	rcv function (called from netdevice level)
 */

extern int			ipv6_rcv(struct sk_buff *skb, 
					 struct device *dev, 
					 struct packet_type *pt);

/*
 *	upper-layer output functions
 */
extern int			ip6_xmit(struct sock *sk,
					 struct sk_buff *skb,
					 struct flowi *fl,
					 struct ipv6_options *opt);

extern int			ip6_nd_hdr(struct sock *sk,
					   struct sk_buff *skb,
					   struct device *dev,
					   struct in6_addr *saddr,
					   struct in6_addr *daddr,
					   int proto, int len);

extern int			ip6_build_xmit(struct sock *sk,
					       inet_getfrag_t getfrag,
					       const void *data,
					       struct flowi *fl,
					       unsigned length,
					       struct ipv6_options *opt,
					       int hlimit, int flags);

/*
 *	skb processing functions
 */

extern int			ip6_output(struct sk_buff *skb);
extern int			ip6_forward(struct sk_buff *skb);
extern int			ip6_input(struct sk_buff *skb);
extern int			ip6_mc_input(struct sk_buff *skb);

/*
 *	Extension header (options) processing
 */

extern int			ipv6opt_bld_rthdr(struct sk_buff *skb,
						  struct ipv6_options *opt,
						  struct in6_addr *addr,
						  int proto);

extern int			ipv6opt_srcrt_co(struct sockaddr_in6 *sin6, 
						 int len, 
						 struct ipv6_options *opt);

extern int			ipv6opt_srcrt_cl(struct sockaddr_in6 *sin6, 
						 int num_addrs, 
						 struct ipv6_options *opt);

extern int			ipv6opt_srt_tosin(struct ipv6_options *opt,
						  struct sockaddr_in6 *sin6,
						  int len);

extern void			ipv6opt_free(struct ipv6_options *opt);


/*
 *	socket options (ipv6_sockglue.c)
 */

extern int			ipv6_setsockopt(struct sock *sk, int level, 
						int optname, char *optval, 
						int optlen);
extern int			ipv6_getsockopt(struct sock *sk, int level, 
						int optname, char *optval, 
						int *optlen);


extern void			ipv6_init(void);
extern void			ipv6_cleanup(void);
#endif
#endif




/*
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	$Id: ipv6.h,v 1.19 1996/09/24 17:04:20 roque Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _NET_IPV6_H
#define _NET_IPV6_H

#include <linux/ipv6.h>
#include <net/ndisc.h>

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

extern int		ipv6_forwarding;	/* host/router switch */
extern int		ipv6_hop_limit;		/* default hop limit */

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

/*
 *	Prototypes exported by ipv6
 */

#if 0
extern int			ipv6_build_header(struct sk_buff *skb,
						  struct device *dev,
						  struct in6_addr *saddr_in, 
						  struct in6_addr *daddr_in,
						  int proto, int len, 
						  struct ipv6_pinfo *np);
#endif

extern void			ipv6_redo_mac_hdr(struct sk_buff *skb,
						  struct neighbour *neigh,
						  int len);

extern int			ipv6_bld_hdr_2(struct sock *sk,
					       struct sk_buff *skb,
					       struct device *dev,
					       struct neighbour *neigh,
					       struct in6_addr *saddr,
					       struct in6_addr *daddr,
					       int proto, int len);

extern int			ipv6_xmit(struct sock *sk,
					  struct sk_buff *skb,
					  struct in6_addr *saddr,
					  struct in6_addr *daddr,
					  struct ipv6_options *opt,
					  int proto);

extern void			ipv6_queue_xmit(struct sock *sk,
						struct device *dev,
						struct sk_buff *skb,
						int free);

extern int			ipv6_build_xmit(struct sock *sk,
						inet_getfrag_t getfrag,
						const void * data,
						struct in6_addr * daddr,
						unsigned short int length,
						struct in6_addr * saddr,
						struct device *dev,
						struct ipv6_options *opt,
						int proto, int hlimit,
						int noblock);

/*
 *	rcv function (called from netdevice level)
 */

extern int			ipv6_rcv(struct sk_buff *skb, 
					 struct device *dev, 
					 struct packet_type *pt);

extern void			ipv6_forward(struct sk_buff *skb,
					     struct device *dev,
					     int flags);

#define IP6_FW_SRCRT	0x1
#define	IP6_FW_STRICT	0x2

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
 *	socket lookup (af_inet6.c)
 */

extern struct sock *		inet6_get_sock(struct proto *prot, 
					       struct in6_addr *loc_addr, 
					       struct in6_addr *rmt_addr,
					       unsigned short loc_port,
					       unsigned short rmt_port);

extern struct sock *		inet6_get_sock_raw(struct sock *sk, 
						   unsigned short num,
						   struct in6_addr *loc_addr, 
						   struct in6_addr *rmt_addr);

extern struct sock *		inet6_get_sock_mcast(struct sock *sk, 
						     unsigned short num,
						     unsigned short rmt_port,
						     struct in6_addr *loc_addr, 
						     struct in6_addr *rmt_addr);

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




#ifndef _IPV6_H
#define _IPV6_H

#include <linux/in6.h>
#include <asm/byteorder.h>


/*
 *	Advanced API
 *	source interface/address selection, source routing, etc...
 *	*under construction*
 */


struct in6_pktinfo {
	struct in6_addr	ipi6_addr;
	int		ipi6_ifindex;
};


struct in6_ifreq {
	struct in6_addr	ifr6_addr;
	__u32		ifr6_prefixlen;
	int		ifr6_ifindex; 
};

#define IPV6_SRCRT_STRICT	0x01	/* this hop must be a neighbor	*/
#define IPV6_SRCRT_TYPE_0	0	/* IPv6 type 0 Routing Header	*/

/*
 *	routing header
 */
struct ipv6_rt_hdr {
	__u8		nexthdr;
	__u8		hdrlen;
	__u8		type;
	__u8		segments_left;

	/*
	 *	type specific data
	 *	variable length field
	 */
};

/*
 *	routing header type 0 (used in cmsghdr struct)
 */

struct rt0_hdr {
	struct ipv6_rt_hdr	rt_hdr;
	__u32			bitmap;		/* strict/loose bit map */
	struct in6_addr		addr[0];

#define rt0_type		rt_hdr.type;
};

#ifdef __KERNEL__

/*
 *	IPv6 fixed header
 */

struct ipv6hdr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8			priority:4,
				version:4;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u8			version:4,
				priority:4;
#else
#error	"Please fix <asm/byteorder.h>"
#endif						
	__u8			flow_lbl[3];

	__u16			payload_len;
	__u8			nexthdr;
	__u8			hop_limit;

	struct	in6_addr	saddr;
	struct	in6_addr	daddr;
};

/*
 *	The length of this struct cannot be greater than the length of
 *	the proto_priv field in a sk_buff which is currently
 *	defined to be 16 bytes.
 *	Pointers take upto 8 bytes (sizeof(void *) is 8 on the alpha).
 */
struct ipv6_options 
{
	/* length of extension headers   */

	__u16			opt_flen;	/* after fragment hdr */
	__u16			opt_nflen;	/* before fragment hdr */

	/* 
	 * protocol options 
	 * usualy carried in IPv6 extension headers
	 */

	struct ipv6_rt_hdr		*srcrt;	/* Routing Header */

};

#endif

#endif

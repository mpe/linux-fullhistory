/* $Id: byteorder.h,v 1.1 1996/11/20 14:01:53 davem Exp $ */
#ifndef _SPARC64_BYTEORDER_H
#define _SPARC64_BYTEORDER_H

#define ntohl(x) x
#define ntohs(x) x
#define htonl(x) x
#define htons(x) x

/* Some programs depend upon these being around. */
#define __constant_ntohl(x) x
#define __constant_ntohs(x) x
#define __constant_htonl(x) x
#define __constant_htons(x) x

#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN 4321
#endif

#ifndef __BIG_ENDIAN_BITFIELD
#define __BIG_ENDIAN_BITFIELD
#endif

#ifdef __KERNEL__

/* Convert from CPU byte order, to specified byte order. */
extern __inline__ __u16 cpu_to_le16(__u16 value)
{
	return (value >> 8) | (value << 8);
}

extern __inline__ __u32 cpu_to_le32(__u32 value)
{
	return((value>>24) | ((value>>8)&0xff00) |
	       ((value<<8)&0xff0000) | (value<<24));
}
#define cpu_to_be16(x)  (x)
#define cpu_to_be32(x)  (x)

/* Convert from specified byte order, to CPU byte order. */
extern __inline__ __u16 le16_to_cpu(__u16 value)
{
	return (value >> 8) | (value << 8);
}

extern __inline__ __u32 le32_to_cpu(__u32 value)
{
	return((value>>24) | ((value>>8)&0xff00) |
	       ((value<<8)&0xff0000) | (value<<24));
}
#define be16_to_cpu(x)  (x)
#define be32_to_cpu(x)  (x)

#endif /* __KERNEL__ */

#endif /* !(_SPARC64_BYTEORDER_H) */

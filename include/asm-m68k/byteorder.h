#ifndef _M68K_BYTEORDER_H
#define _M68K_BYTEORDER_H

#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN 4321
#endif

#ifndef __BIG_ENDIAN_BITFIELD
#define __BIG_ENDIAN_BITFIELD
#endif

#ifdef __KERNEL__
#include <linux/config.h>
#include <asm/types.h>

/*
 * In-kernel byte order macros to handle stuff like
 * byte-order-dependent filesystems etc.
 */

#define le16_to_cpu(__val) __swab16(__val)
#define cpu_to_le16(__val) __swab16(__val)
#define le32_to_cpu(x) \
(__builtin_constant_p(x) ? __constant_swab32(x) : __swab32(x))
#define cpu_to_le32(x) \
(__builtin_constant_p(x) ? __constant_swab32(x) : __swab32(x))

extern __inline__ __u16 __swab16 (__u16 val)
{
	return (val << 8) | (val >> 8);
}

extern __inline__ __u32 __constant_swab32 (__u32 val)
{
	return (val << 24) | ((val << 8) & 0xff0000) |
	       ((val >> 8) & 0xff00) | (val >> 24);
}

extern __inline__ __u32 __swab32 (__u32 val)
{
	__asm__ ("rolw #8,%0; swap %0; rolw #8,%0" : "=d" (val) : "0" (val));
	return val;
}

#define cpu_to_be32(x) (x)
#define be32_to_cpu(x) (x)
#define cpu_to_be16(x) (x)
#define be16_to_cpu(x) (x)

#endif

#undef ntohl
#undef ntohs
#undef htonl
#undef htons

extern unsigned long int	ntohl(unsigned long int);
extern unsigned short int	ntohs(unsigned short int);
extern unsigned long int	htonl(unsigned long int);
extern unsigned short int	htons(unsigned short int);

extern __inline__ unsigned long int	__ntohl(unsigned long int);
extern __inline__ unsigned short int	__ntohs(unsigned short int);

extern __inline__ unsigned long int
__ntohl(unsigned long int x)
{
	return x;
}

extern __inline__ unsigned short int
__ntohs(unsigned short int x)
{
	return x;
}

#define __htonl(x) __ntohl(x)
#define __htons(x) __ntohs(x)

#define __constant_htonl(x) (x)
#define __constant_htons(x) (x)
#define __constant_ntohl(x) (x)
#define __constant_ntohs(x) (x)

#ifdef __OPTIMIZE__
#define ntohl(x) __ntohl(x)
#define ntohs(x) __ntohs(x)
#define htonl(x) __htonl(x)
#define htons(x) __htons(x)
#endif

#endif

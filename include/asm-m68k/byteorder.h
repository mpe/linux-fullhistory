#ifndef _M68K_BYTEORDER_H
#define _M68K_BYTEORDER_H

#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN 4321
#endif

#ifndef __BIG_ENDIAN_BITFIELD
#define __BIG_ENDIAN_BITFIELD
#endif

#ifdef __KERNEL__
#include <asm/types.h>

/*
 * In-kernel byte order macros to handle stuff like
 * byte-order-dependent filesystems etc.
 */

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

/* Convert from CPU byte order, to specified byte order. */
#define cpu_to_le16(__val) __swab16(__val)
#define cpu_to_le32(__val) \
(__builtin_constant_p(__val) ? __constant_swab32(__val) : __swab32(__val))
#define cpu_to_be16(x)  (x)
#define cpu_to_be32(x)  (x)

/* The same, but returns converted value from the location pointer by addr. */
extern __inline__ __u16 cpu_to_le16p(__u16 *addr)
{
	return cpu_to_le16(*addr);
}

extern __inline__ __u32 cpu_to_le32p(__u32 *addr)
{
	return cpu_to_le32(*addr);
}

extern __inline__ __u16 cpu_to_be16p(__u16 *addr)
{
	return cpu_to_be16(*addr);
}

extern __inline__ __u32 cpu_to_be32p(__u32 *addr)
{
	return cpu_to_be32(*addr);
}

/* The same, but do the conversion in situ, ie. put the value back to addr. */
extern __inline__ void cpu_to_le16s(__u16 *addr)
{
	*addr = cpu_to_le16(*addr);
}

extern __inline__ void cpu_to_le32s(__u32 *addr)
{
	*addr = cpu_to_le32(*addr);
}

#define cpu_to_be16s(x) do { } while (0)
#define cpu_to_be32s(x) do { } while (0)

/* Convert from specified byte order, to CPU byte order. */
#define le16_to_cpu(x)  cpu_to_le16(x)
#define le32_to_cpu(x)  cpu_to_le32(x)
#define be16_to_cpu(x)  cpu_to_be16(x)
#define be32_to_cpu(x)  cpu_to_be32(x)

#define le16_to_cpup(x) cpu_to_le16p(x)
#define le32_to_cpup(x) cpu_to_le32p(x)
#define be16_to_cpup(x) cpu_to_be16p(x)
#define be32_to_cpup(x) cpu_to_be32p(x)

#define le16_to_cpus(x) cpu_to_le16s(x)
#define le32_to_cpus(x) cpu_to_le32s(x)
#define be16_to_cpus(x) cpu_to_be16s(x)
#define be32_to_cpus(x) cpu_to_be32s(x)

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

/* $Id: byteorder.h,v 1.5 1997/05/28 11:35:41 jj Exp $ */
#ifndef _SPARC64_BYTEORDER_H
#define _SPARC64_BYTEORDER_H

#include <asm/asi.h>

#define ntohl(x) ((unsigned long int)(x))
#define ntohs(x) ((unsigned short int)(x))
#define htonl(x) ((unsigned long int)(x))
#define htons(x) ((unsigned short int)(x))

/* Some programs depend upon these being around. */
#define __constant_ntohl(x) ((unsigned long int)(x))
#define __constant_ntohs(x) ((unsigned short int)(x))
#define __constant_htonl(x) ((unsigned long int)(x))
#define __constant_htons(x) ((unsigned short int)(x))

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

extern __inline__ __u64 cpu_to_le64(__u64 value)
{
        return (((value>>56) & 0x00000000000000ffUL) |
                ((value>>40) & 0x000000000000ff00UL) |
                ((value>>24) & 0x0000000000ff0000UL) |
                ((value>>8)  & 0x00000000ff000000UL) |
                ((value<<8)  & 0x000000ff00000000UL) |
                ((value<<24) & 0x0000ff0000000000UL) |
                ((value<<40) & 0x00ff000000000000UL) |
                ((value<<56) & 0xff00000000000000UL));
}
#define cpu_to_be16(x)  (x)
#define cpu_to_be32(x)  (x)
#define cpu_to_be64(x)	(x)

/* The same, but returns converted value from the location pointer by addr. */
extern __inline__ __u16 cpu_to_le16p(__u16 *addr)
{
	__u16 ret;
	__asm__ __volatile__ ("lduha [%1] %2, %0" : "=r" (ret) : "r" (addr), "i" (ASI_PL));
	return ret;
}

extern __inline__ __u32 cpu_to_le32p(__u32 *addr)
{
	__u32 ret;
	__asm__ __volatile__ ("lduwa [%1] %2, %0" : "=r" (ret) : "r" (addr), "i" (ASI_PL));
	return ret;
}

extern __inline__ __u64 cpu_to_le64p(__u64 *addr)
{
	__u64 ret;
	__asm__ __volatile__ ("ldxa [%1] %2, %0" : "=r" (ret) : "r" (addr), "i" (ASI_PL));
	return ret;
}
extern __inline__ __u16 cpu_to_be16p(__u16 *addr) { return *addr; }
extern __inline__ __u32 cpu_to_be32p(__u32 *addr) { return *addr; }
extern __inline__ __u64 cpu_to_be64p(__u64 *addr) { return *addr; }

/* The same, but do the conversion in situ, ie. put the value back to addr. */
extern __inline__ void cpu_to_le16s(__u16 *addr)
{
	*addr = cpu_to_le16p(addr);
}

extern __inline__ void cpu_to_le32s(__u32 *addr)
{
	*addr = cpu_to_le32p(addr);
}

extern __inline__ void cpu_to_le64s(__u64 *addr)
{
	*addr = cpu_to_le64p(addr);
}
#define cpu_to_be16s(x) do { } while (0)
#define cpu_to_be32s(x) do { } while (0)
#define cpu_to_be64s(x) do { } while (0)

/* Convert from specified byte order, to CPU byte order. */
#define le16_to_cpu(x)	cpu_to_le16(x)
#define le32_to_cpu(x)	cpu_to_le32(x)
#define le64_to_cpu(x)	cpu_to_le64(x)
#define be16_to_cpu(x)  cpu_to_be16(x)
#define be32_to_cpu(x)  cpu_to_be32(x)
#define be64_to_cpu(x)	cpu_to_be64(x)

#define le16_to_cpup(x)	cpu_to_le16p(x)
#define le32_to_cpup(x)	cpu_to_le32p(x)
#define le64_to_cpup(x)	cpu_to_le64p(x)
#define be16_to_cpup(x)	cpu_to_be16p(x)
#define be32_to_cpup(x)	cpu_to_be32p(x)
#define be64_to_cpup(x)	cpu_to_be64p(x)

#define le16_to_cpus(x)	cpu_to_le16s(x)
#define le32_to_cpus(x)	cpu_to_le32s(x)
#define le64_to_cpus(x)	cpu_to_le64s(x)
#define be16_to_cpus(x)	cpu_to_be16s(x)
#define be32_to_cpus(x)	cpu_to_be32s(x)
#define be64_to_cpus(x)	cpu_to_be64s(x)

#endif /* __KERNEL__ */

#endif /* !(_SPARC64_BYTEORDER_H) */

#ifndef _PPC_BYTEORDER_H
#define _PPC_BYTEORDER_H

#include <asm/types.h>

#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN
#endif

#ifndef __BIG_ENDIAN_BITFIELD
#define __BIG_ENDIAN_BITFIELD
#endif

#define ntohl(x) (x)
#define ntohs(x) (x)
#define htonl(x) (x)
#define htons(x) (x)

#define __htonl(x) ntohl(x)
#define __htons(x) ntohs(x)

#define __constant_ntohs(x) ntohs(x)
#define __constant_ntohl(x) ntohl(x)
#define __constant_htonl(x) ntohl(x)
#define __constant_htons(x) ntohs(x)

#ifdef __KERNEL__
/*
 * 16 and 32 bit little-endian loads and stores.
 */
extern inline unsigned ld_le16(volatile unsigned short *addr)
{
	unsigned val;

	asm volatile("lhbrx %0,0,%1" : "=r" (val) : "r" (addr));
	return val;
}

extern inline void st_le16(volatile unsigned short *addr, unsigned val)
{
	asm volatile("sthbrx %0,0,%1" : : "r" (val), "r" (addr) : "memory");
}

extern inline unsigned ld_le32(volatile unsigned *addr)
{
	unsigned val;

	asm volatile("lwbrx %0,0,%1" : "=r" (val) : "r" (addr));
	return val;
}

extern inline void st_le32(volatile unsigned *addr, unsigned val)
{
	asm volatile("stwbrx %0,0,%1" : : "r" (val), "r" (addr) : "memory");
}


extern __inline__ __u16 cpu_to_le16(__u16 value)
{
	return ld_le16(&value);
}
extern __inline__ __u32 cpu_to_le32(__u32 value)
{
	return ld_le32(&value);
}
#define cpu_to_be16(x)  (x)
#define cpu_to_be32(x)  (x)

/* The same, but returns converted value from the location pointer by addr. */
extern __inline__ __u16 cpu_to_le16p(__u16 *addr)
{
	return ld_le16(addr);
}

extern __inline__ __u32 cpu_to_le32p(__u32 *addr)
{
	return ld_le32(addr);
}

extern __inline__ __u16 cpu_to_be16p(__u16 *addr)
{
	return *addr;
}

extern __inline__ __u32 cpu_to_be32p(__u32 *addr)
{
	return *addr;
}

/* The same, but do the conversion in situ, ie. put the value back to addr. */
extern __inline__ void cpu_to_le16s(__u16 *addr)
{
	st_le16(addr,*addr);
}

extern __inline__ void cpu_to_le32s(__u32 *addr)
{
	st_le32(addr,*addr);
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


#endif /* __KERNEL__ */
#endif /* !(_PPC_BYTEORDER_H) */







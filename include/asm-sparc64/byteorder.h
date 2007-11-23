#ifndef _SPARC64_BYTEORDER_H
#define _SPARC64_BYTEORDER_H

#include <asm/types.h>
#include <asm/asi.h>

#ifdef __GNUC__

static __inline__ __u16 ___arch__swab16p(__u16 *addr)
{
	__u16 ret;

	__asm__ __volatile__ ("lduha [%1] %2, %0"
			      : "=r" (ret)
			      : "r" (addr), "i" (ASI_PL));
	return ret;
}

static __inline__ __u32 ___arch__swab32p(__u32 *addr)
{
	__u32 ret;

	__asm__ __volatile__ ("lduwa [%1] %2, %0"
			      : "=r" (ret)
			      : "r" (addr), "i" (ASI_PL));
	return ret;
}

static __inline__ __u64 ___arch__swab64p(__u64 *addr) {
	__u64 ret;

	__asm__ __volatile__ ("ldxa [%1] %2, %0"
			      : "=r" (ret)
			      : "r" (addr), "i" (ASI_PL));
	return ret;
}

#define __arch__swab16p(x) ___arch__swab16p(x)
#define __arch__swab32p(x) ___arch__swab32p(x)
#define __arch__swab64p(x) ___arch__swab64p(x)

#define __BYTEORDER_HAS_U64__

#include <linux/byteorder_big_endian.h>

#endif /* _SPARC64_BYTEORDER_H */

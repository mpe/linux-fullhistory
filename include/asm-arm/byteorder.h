#ifndef __ASM_ARM_BYTEORDER_H
#define __ASM_ARM_BYTEORDER_H

#include <asm/types.h>

#if defined(__GNUC__) && __GNUC__ == 2 && __GNUC_MINOR__ < 8

/* Recent versions of GCC can open code the swaps at least as well
   as we can write them by hand, so the "optimisations" here only 
   make sense for older compilers.  Worse, some versions of GCC
   actually go wrong in the presence of the assembler versions.
   We play it safe and only turn them on for compilers older than
   GCC 2.8.0.  */

static __inline__ __const__ __u32 ___arch__swab32(__u32 x)
{
	unsigned long xx;
	__asm__("eor\t%1, %0, %0, ror #16\n\t"
		"bic\t%1, %1, #0xff0000\n\t"
		"mov\t%0, %0, ror #8\n\t"
		"eor\t%0, %0, %1, lsr #8\n\t"
		: "=r" (x), "=&r" (xx)
		: "0" (x));
	return x;
}

static __inline__ __const__ __u16 ___arch__swab16(__u16 x)
{
	__asm__("eor\t%0, %0, %0, lsr #8\n\t"
		"eor\t%0, %0, %0, lsl #8\n\t"
		"bic\t%0, %0, #0xff0000\n\t"
		"eor\t%0, %0, %0, lsr #8\n\t"
		: "=r" (x) 
		: "0" (x));
	return x;
}

#define __arch__swab32(x) ___arch__swab32(x)
#define __arch__swab16(x) ___arch__swab16(x)

#endif /* __GNUC__ */

#if !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#  define __BYTEORDER_HAS_U64__
#  define __SWAB_64_THRU_32__
#endif

#include <linux/byteorder/little_endian.h>

#endif


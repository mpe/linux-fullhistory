#ifndef __ASM_ARM_BYTEORDER_H
#define __ASM_ARM_BYTEORDER_H

#include <asm/types.h>

#if defined(__GNUC__) && __GNUC__ == 2 && __GNUC_MINOR__ < 80

/* Recent versions of GCC can do as well or better than this
   on their own - we shouldn't interfere.  */

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

#include <linux/byteorder/little_endian.h>

#endif


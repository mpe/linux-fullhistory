/*
 * Inline functions to do unaligned accesses.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 by Ralf Baechle
 */
#ifndef __ASM_MIPS_UNALIGNED_H
#define __ASM_MIPS_UNALIGNED_H

#include <asm/string.h>

/*
 * Load quad unaligned.
 */
extern __inline__ unsigned long ldq_u(unsigned long long * __addr)
{
	unsigned long long __res;

	__asm__("uld\t%0,(%1)"
		:"=&r" (__res)
		:"r" (__addr));

	return __res;
}

/*
 * Load long unaligned.
 */
extern __inline__ unsigned long ldl_u(unsigned int * __addr)
{
	unsigned long __res;

	__asm__("ulw\t%0,(%1)"
		:"=&r" (__res)
		:"r" (__addr));

	return __res;
}

/*
 * Load word unaligned.
 */
extern __inline__ unsigned long ldw_u(unsigned short * __addr)
{
	unsigned long __res;

	__asm__("ulh\t%0,(%1)"
		:"=&r" (__res)
		:"r" (__addr));

	return __res;
}

/*
 * Store quad ununaligned.
 */
extern __inline__ void stq_u(unsigned long __val, unsigned long long * __addr)
{
	__asm__ __volatile__(
		"usd\t%0,(%1)"
		: /* No results */
		:"r" (__val),
		 "r" (__addr));
}

/*
 * Store long ununaligned.
 */
extern __inline__ void stl_u(unsigned long __val, unsigned int * __addr)
{
	__asm__ __volatile__(
		"usw\t%0,(%1)"
		: /* No results */
		:"r" (__val),
		 "r" (__addr));
}

/*
 * Store word ununaligned.
 */
extern __inline__ void stw_u(unsigned long __val, unsigned short * __addr)
{
	__asm__ __volatile__(
		"ush\t%0,(%1)"
		: /* No results */
		:"r" (__val),
		 "r" (__addr));
}

#define get_unaligned(ptr) \
  ({ __typeof__(*(ptr)) __tmp; memcpy(&__tmp, (ptr), sizeof(*(ptr))); __tmp; })

#define put_unaligned(val, ptr)				\
  ({ __typeof__(*(ptr)) __tmp = (val);			\
     memcpy((ptr), &__tmp, sizeof(*(ptr)));		\
     (void)0; })

#endif /* __ASM_MIPS_UNALIGNED_H */

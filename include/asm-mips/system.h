/* $Id: system.h,v 1.8 1998/07/20 17:52:21 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1997, 1998 by Ralf Baechle
 * Modified further for R[236]000 by Paul M. Antoine, 1996
 */
#ifndef __ASM_MIPS_SYSTEM_H
#define __ASM_MIPS_SYSTEM_H

#include <asm/sgidefs.h>
#include <linux/kernel.h>

extern __inline__ void
__sti(void)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"mfc0\t$1,$12\n\t"
		"ori\t$1,0x1f\n\t"
		"xori\t$1,0x1e\n\t"
		"mtc0\t$1,$12\n\t"
		".set\tat\n\t"
		".set\treorder"
		: /* no outputs */
		: /* no inputs */
		: "$1", "memory");
}

/*
 * For cli() we have to insert nops to make shure that the new value
 * has actually arrived in the status register before the end of this
 * macro.
 * R4000/R4400 need three nops, the R4600 two nops and the R10000 needs
 * no nops at all.
 */
extern __inline__ void
__cli(void)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"mfc0\t$1,$12\n\t"
		"ori\t$1,1\n\t"
		"xori\t$1,1\n\t"
		"mtc0\t$1,$12\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		".set\tat\n\t"
		".set\treorder"
		: /* no outputs */
		: /* no inputs */
		: "$1", "memory");
}

#define __save_flags(x)                  \
__asm__ __volatile__(                    \
	".set\tnoreorder\n\t"            \
	"mfc0\t%0,$12\n\t"               \
	".set\treorder"                  \
	: "=r" (x)                       \
	: /* no inputs */                \
	: "memory")

#define __save_and_cli(x)                \
__asm__ __volatile__(                    \
	".set\tnoreorder\n\t"            \
	".set\tnoat\n\t"                 \
	"mfc0\t%0,$12\n\t"               \
	"ori\t$1,%0,1\n\t"               \
	"xori\t$1,1\n\t"                 \
	"mtc0\t$1,$12\n\t"               \
	"nop\n\t"                        \
	"nop\n\t"                        \
	"nop\n\t"                        \
	".set\tat\n\t"                   \
	".set\treorder"                  \
	: "=r" (x)                       \
	: /* no inputs */                \
	: "$1", "memory")

extern void __inline__
__restore_flags(int flags)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		"mtc0\t%0,$12\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		".set\treorder"
		: /* no output */
		: "r" (flags)
		: "memory");
}

/*
 * Non-SMP versions ...
 */
#define sti() __sti()
#define cli() __cli()
#define save_flags(x) __save_flags(x)
#define save_and_cli(x) __save_and_cli(x)
#define restore_flags(x) __restore_flags(x)

#define mb()						\
__asm__ __volatile__(					\
	"# prevent instructions being moved around\n\t"	\
	".set\tnoreorder\n\t"				\
	"# 8 nops to fool the R4400 pipeline\n\t"	\
	"nop;nop;nop;nop;nop;nop;nop;nop\n\t"		\
	".set\treorder"					\
	: /* no output */				\
	: /* no input */				\
	: "memory")

#if !defined (_LANGUAGE_ASSEMBLY)
/*
 * switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 */
extern asmlinkage void (*resume)(void *tsk);
#endif /* !defined (_LANGUAGE_ASSEMBLY) */

#define switch_to(prev,next) \
do { \
	resume(next); \
} while(0)

/*
 * For 32 and 64 bit operands we can take advantage of ll and sc.
 * FIXME: This doesn't work for R3000 machines.
 */
extern __inline__ unsigned long xchg_u32(volatile int * m, unsigned long val)
{
#if (_MIPS_ISA == _MIPS_ISA_MIPS2) || (_MIPS_ISA == _MIPS_ISA_MIPS3) || \
    (_MIPS_ISA == _MIPS_ISA_MIPS4) || (_MIPS_ISA == _MIPS_ISA_MIPS5)
	unsigned long dummy;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"ll\t%0,(%1)\n"
		"1:\tmove\t$1,%2\n\t"
		"sc\t$1,(%1)\n\t"
		"beqzl\t$1,1b\n\t"
		"ll\t%0,(%1)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (val), "=r" (m), "=r" (dummy)
		: "1" (m), "2" (val)
		: "memory");
#else
	unsigned long flags, retval;

	save_flags(flags);
	cli();
	retval = *m;
	*m = val;
	restore_flags(flags);

#endif /* Processor-dependent optimization */
	return val;
}

/*
 * Only used for 64 bit kernel.
 */
extern __inline__ unsigned long xchg_u64(volatile long * m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"lld\t%0,(%1)\n"
		"1:\tmove\t$1,%2\n\t"
		"scd\t$1,(%1)\n\t"
		"beqzl\t$1,1b\n\t"
		"ll\t%0,(%1)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (val), "=r" (m), "=r" (dummy)
		: "1" (m), "2" (val)
		: "memory");

	return val;
}

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))
#define tas(ptr) (xchg((ptr),1))

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid xchg().
 *
 * This only works if the compiler isn't horribly bad at optimizing.
 * gcc-2.5.8 reportedly can't handle this, but I define that one to
 * be dead anyway.
 */
extern void __xchg_called_with_bad_pointer(void);

static __inline__ unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
		case 4:
			return xchg_u32(ptr, x);
#if defined(__mips64)
		case 8:
			return xchg_u64(ptr, x);
#endif
	}
	__xchg_called_with_bad_pointer();
	return x;
}

extern void set_except_vector(int n, void *addr);

#endif /* __ASM_MIPS_SYSTEM_H */

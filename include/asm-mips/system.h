/*
 * include/asm-mips/system.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Ralf Baechle
 */

#ifndef __ASM_MIPS_SYSTEM_H
#define __ASM_MIPS_SYSTEM_H

#include <linux/types.h>
#include <asm/segment.h>
#include <asm/mipsregs.h>
#include <asm/mipsconfig.h>

/*
 * (Currently empty to support debugging)
 */
#define move_to_user_mode()              \
__asm__ __volatile__ (                   \
	".set\tnoreorder\n\t"            \
	".set\tnoat\n\t"                 \
	"la\t$1,1f\n\t"                  \
	"subu\t$1,$1,%0\n\t"             \
	"jr\t$1\n\t"                     \
	"mfc0\t$1,$12\n\t"               \
	"1:ori\t$1,0x00\n\t"             \
	"mtc0\t$1,$12\n\t"               \
	"subu\t$29,%0\n\t"               \
	".set\tat\n\t"                   \
	".set\treorder"                  \
	: /* no outputs */               \
	: "r" (KERNELBASE));

#if defined (__R4000__)
#define sti() \
__asm__ __volatile__(                    \
	".set\tnoat\n\t"                 \
	"mfc0\t$1,"STR(CP0_STATUS)"\n\t" \
	"ori\t$1,$1,0x1f\n\t"            \
	"xori\t$1,$1,0x1e\n\t"           \
	"mtc0\t$1,"STR(CP0_STATUS)"\n\t" \
	".set\tat"                       \
	: /* no outputs */               \
	: /* no inputs */                \
	: "$1")

#define cli() \
__asm__ __volatile__(                    \
	".set\tnoat\n\t"                 \
	"mfc0\t$1,"STR(CP0_STATUS)"\n\t" \
	"ori\t$1,$1,1\n\t"               \
	"xori\t$1,$1,1\n\t"              \
	"mtc0\t$1,"STR(CP0_STATUS)"\n\t" \
	".set\tat"                       \
	: /* no outputs */               \
	: /* no inputs */                \
	: "$1")
#else /* !defined (__R4000__) */
/*
 * Cheese - I don't have a R3000 manual
 */
#error "Yikes - write cli()/sti() macros for  R3000!"
#endif /* !defined (__R4000__) */

#define nop() __asm__ __volatile__ ("nop")

#define save_flags(x)                    \
__asm__ __volatile__(                    \
	"mfc0\t%0,$12"                   \
	: "=r" (x))                      \

#define restore_flags(x)                 \
__asm__ __volatile__(                    \
	"mtc0\t%0,$12"                   \
	: /* no output */                \
	: "r" (x))                       \

extern inline unsigned long xchg_u8(char * m, unsigned long val)
{
	unsigned long flags, retval;

	save_flags(flags);
	sti();
	retval = *m;
	*m = val;
	restore_flags(flags);

	return retval;
}

extern inline unsigned long xchg_u16(short * m, unsigned long val)
{
	unsigned long flags, retval;

	save_flags(flags);
	sti();
	retval = *m;
	*m = val;
	restore_flags(flags);

	return retval;
}

/*
 * For 32 and 64 bit operands we can take advantage of ll and sc.
 * The later isn't currently being used.
 */
extern inline unsigned long xchg_u32(int * m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"ll\t%0,(%1)\n"
		"1:\tmove\t$1,%2\n\t"
		"sc\t$1,(%1)\n\t"
		"beqzl\t%3,1b\n\t"
		"ll\t%0,(%1)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (val), "=r" (m), "=r" (dummy)
		: "1" (*m), "2" (val));

	return val;
}

extern inline unsigned long xchg_u64(long * m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"lld\t%0,(%1)\n"
		"1:\tmove\t$1,%2\n\t"
		"scd\t$1,(%1)\n\t"
		"beqzl\t%3,1b\n\t"
		"ll\t%0,(%1)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (val), "=r" (m), "=r" (dummy)
		: "1" (*m), "2" (val));

	return val;
}

#if 0
extern inline int tas(char * m)
{
	return xchg_u8(m,1);
}
#endif

extern inline void * xchg_ptr(void * m, void * val)
{
	return (void *) xchg_u32(m, (unsigned long) val);
}

extern ulong IRQ_vectors[256];
extern ulong exception_handlers[256];

#define set_intr_gate(n,addr) \
	IRQ_vectors[n] = (ulong) (addr)

#define set_except_vector(n,addr) \
	exception_handlers[n] = (ulong) (addr)

/*
 * atomic exchange of one word
 */
#if defined (__R4000__)
#define atomic_exchange(m,r)             \
	__asm__ __volatile__(            \
		".set\tnoreorder\n\t"    \
		"ll\t%0,(%2)\n"          \
		"1:\tmove\t$8,%1\n\t"    \
		"sc\t$8,(%2)\n\t"        \
		"beql\t$0,$8,1b\n\t"     \
		"ll\t%0,(%2)\n\t"        \
		".set\treorder"          \
		: "=r" (r)               \
		: "r" (r), "r" (&(m))    \
		: "$8","memory")
#else
#define atomic_exchange(m,r)             \
	{                                \
		unsigned long flags;     \
		unsigned long tmp;       \
		save_flags(flags);       \	
		cli();                   \
		tmp = (m);               \
		(m) = (r);               \
		(r) = tmp;               \
		restore_flags(flags);    \	
	}
#endif

#endif /* __ASM_MIPS_SYSTEM_H */

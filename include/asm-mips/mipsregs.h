/*
 * include/asm-mips/mipsregs.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 by Ralf Baechle
 */

#ifndef __ASM_MIPS_MIPSREGS_H
#define __ASM_MIPS_MIPSREGS_H

/*
 * The following macros are especially useful for __asm__
 * inline assembler.
 */

#ifndef __STR
#define __STR(x) #x
#endif
#ifndef STR
#define STR(x) __STR(x)
#endif

/*
 * On the R2000/3000 load instructions are not interlocked -
 * we therefore sometimes need to fill load delay slots with a nop
 * which would be useless for ISA >= 2.
 */
#if !defined (__R4000__)
#define FILL_LDS nop
#else
#define FILL_LDS
#endif

/*
 * Coprocessor 0 register names
 */
#define CP0_INDEX $0
#define CP0_RANDOM $1
#define CP0_ENTRYLO0 $2
#define CP0_ENTRYLO1 $3
#define CP0_CONTEXT $4
#define CP0_PAGEMASK $5
#define CP0_WIRED $6
#define CP0_BADVADDR $8
#define CP0_COUNT $9
#define CP0_ENTRYHI $10
#define CP0_COMPARE $11
#define CP0_STATUS $12
#define CP0_CAUSE $13
#define CP0_EPC $14
#define CP0_PRID $15
#define CP0_CONFIG $16
#define CP0_LLADDR $17
#define CP0_WATCHLO $18
#define CP0_WATCHHI $19
#define CP0_XCONTEXT $20
#define CP0_FRAMEMASK $21
#define CP0_DIAGNOSTIC $22
#define CP0_PERFORMANCE $25
#define CP0_ECC $26
#define CP0_CACHEERR $27
#define CP0_TAGLO $28
#define CP0_TAGHI $29
#define CP0_ERROREPC $30

/*
 * Coprocessor 1 (FPU) register names
 */
#define CP1_REVISION   $0
#define CP1_STATUS     $31

/*
 * Values for PageMask register
 */
#define PM_4K   0x00000000
#define PM_16K  0x00006000
#define PM_64K  0x0001e000
#define PM_256K 0x0007e000
#define PM_1M   0x001fe000
#define PM_4M   0x007fe000
#define PM_16M  0x01ffe000

/*
 * Values used for computation of new tlb entries
 */
#define PL_4K   12
#define PL_16K  14
#define PL_64K  16
#define PL_256K 18
#define PL_1M   20
#define PL_4M   22
#define PL_16M  24

/*
 * Macros to access the system control coprocessor
 */
#define read_32bit_cp0_register(source)                         \
({ int __res;                                                   \
        __asm__ __volatile__(                                   \
        "mfc0\t%0,"STR(source)                                  \
        : "=r" (__res));                                        \
        __res;})

#define read_64bit_cp0_register(source)                         \
({ int __res;                                                   \
        __asm__ __volatile__(                                   \
        ".set\tmips3\n\t"                                       \
        "dmfc0\t%0,"STR(source)"\n\t"                           \
        ".set\tmips0"                                           \
        : "=r" (__res));                                        \
        __res;})

#define write_32bit_cp0_register(register,value)                \
        __asm__ __volatile__(                                   \
        "mtc0\t%0,"STR(register)                                \
        : : "r" (value));

#define write_64bit_cp0_register(register,value)                \
        __asm__ __volatile__(                                   \
        ".set\tmips3\n\t"                                       \
        "dmtc0\t%0,"STR(register)"\n\t"                         \
        ".set\tmips0"                                           \
        : : "r" (value))
/*
 * R4x00 interrupt enable / cause bits
 */
#define IE_SW0          (1<< 8)
#define IE_SW1          (1<< 9)
#define IE_IRQ0         (1<<10)
#define IE_IRQ1         (1<<11)
#define IE_IRQ2         (1<<12)
#define IE_IRQ3         (1<<13)
#define IE_IRQ4         (1<<14)
#define IE_IRQ5         (1<<15)

/*
 * R4x00 interrupt cause bits
 */
#define C_SW0           (1<< 8)
#define C_SW1           (1<< 9)
#define C_IRQ0          (1<<10)
#define C_IRQ1          (1<<11)
#define C_IRQ2          (1<<12)
#define C_IRQ3          (1<<13)
#define C_IRQ4          (1<<14)
#define C_IRQ5          (1<<15)

#ifndef __LANGUAGE_ASSEMBLY__
/*
 * Manipulate the status register.
 * Mostly used to access the interrupt bits.
 */
#define BUILD_SET_CP0(name,register)                            \
extern __inline__ unsigned int                                  \
set_cp0_##name(unsigned int change, unsigned int new)           \
{                                                               \
	unsigned int res;                                       \
                                                                \
	res = read_32bit_cp0_register(register);                \
	res &= ~change;                                         \
	res |= (new & change);                                  \
	if(change)                                              \
		write_32bit_cp0_register(register, res);        \
                                                                \
	return res;                                             \
}

BUILD_SET_CP0(status,CP0_STATUS)
BUILD_SET_CP0(cause,CP0_CAUSE)

#endif /* defined (__LANGUAGE_ASSEMBLY__) */

/*
 * Inline code for use of the ll and sc instructions
 *
 * FIXME: This instruction is only available on MIPS ISA >=3.
 * Since these operations are only being used for atomic operations
 * the easiest workaround for the R[23]00 is to disable interrupts.
 */
#define load_linked(addr)                                       \
({                                                              \
	unsigned int __res;                                     \
                                                                \
	__asm__ __volatile__(                                   \
	"ll\t%0,(%1)"                                           \
	: "=r" (__res)                                          \
	: "r" ((unsigned int) (addr)));                         \
                                                                \
	__res;                                                  \
})

#define store_conditional(addr,value)                           \
({                                                              \
	int	__res;                                          \
                                                                \
	__asm__ __volatile__(                                   \
	"sc\t%0,(%2)"                                           \
	: "=r" (__res)                                          \
	: "0" (value), "r" (addr));                             \
                                                                \
	__res;                                                  \
})

/*
 * Bitfields in the cp0 status register
 *
 * Refer to the MIPS R4xx0 manuals, chapter 5 for explanation.
 * FIXME: This doesn't cover all R4xx0 processors.
 */
#define ST0_IE			(1   <<  0)
#define ST0_EXL			(1   <<  1)
#define ST0_ERL			(1   <<  2)
#define ST0_KSU			(3   <<  3)
#  define KSU_USER		(2  <<   3)
#  define KSU_SUPERVISOR	(1  <<   3)
#  define KSU_KERNEL		(0  <<   3)
#define ST0_UX			(1   <<  5)
#define ST0_SX			(1   <<  6)
#define ST0_KX 			(1   <<  7)
#define ST0_IM			(255 <<  8)
#define ST0_DE			(1   << 16)
#define ST0_CE			(1   << 17)
#define ST0_CH			(1   << 18)
#define ST0_SR			(1   << 20)
#define ST0_BEV			(1   << 22)
#define ST0_RE			(1   << 25)
#define ST0_FR			(1   << 26)
#define ST0_CU			(15  << 28)
#define ST0_CU0			(1   << 28)
#define ST0_CU1			(1   << 29)
#define ST0_CU2			(1   << 30)
#define ST0_CU3			(1   << 31)
#define ST0_XX			(1   << 31)	/* R8000/R10000 naming */

/*
 * Bitfields and bit numbers in the coprocessor 0 cause register.
 *
 * Refer to to your MIPS R4xx0 manual, chapter 5 for explanation.
 */
#define  CAUSEB_EXCCODE		2
#define  CAUSEF_EXCCODE		(31  <<  2)
#define  CAUSEB_IP		8
#define  CAUSEF_IP		(255 <<  8)
#define  CAUSEB_IP0		8
#define  CAUSEF_IP0		(1   <<  8)
#define  CAUSEB_IP1		9
#define  CAUSEF_IP1		(1   <<  9)
#define  CAUSEB_IP2		10
#define  CAUSEF_IP2		(1   << 10)
#define  CAUSEB_IP3		11
#define  CAUSEF_IP3		(1   << 11)
#define  CAUSEB_IP4		12
#define  CAUSEF_IP4		(1   << 12)
#define  CAUSEB_IP5		13
#define  CAUSEF_IP5		(1   << 13)
#define  CAUSEB_IP6		14
#define  CAUSEF_IP6		(1   << 14)
#define  CAUSEB_IP7		15
#define  CAUSEF_IP7		(1   << 15)
#define  CAUSEB_CE		28
#define  CAUSEF_CE		(3   << 28)
#define  CAUSEB_BD		31
#define  CAUSEF_BD		(1   << 31)

#endif /* __ASM_MIPS_MIPSREGS_H */

/*
 * include/asm-mips/mipsregs.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Ralf Baechle
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
 * which are useless for >=R4000.
 *
 * FIXME: Don't know about R6000
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
#define CP0_ECC $26
#define CP0_CACHEERR $27
#define CP0_TAGLO $28
#define CP0_TAGHI $29
#define CP0_ERROREPC $30

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
 * Compute a vpn/pfn entry for EntryHi register
 */
#define VPN(addr,pagesizeshift) ((addr) & ~((1 << (pagesizeshift))-1))
#define PFN(addr,pagesizeshift) (((addr) & ((1 << (pagesizeshift))-1)) << 6)

/*
 * Macros to access the system control coprocessor
 */
#define read_32bit_cp0_register(source)                                        \
({ int __res;                                                                  \
        __asm__ __volatile__(                                                  \
        "mfc0\t%0,"STR(source)                                                 \
        : "=r" (__res));                                                       \
        __res;})

#define read_64bit_cp0_register(source)                                        \
({ int __res;                                                                  \
        __asm__ __volatile__(                                                  \
        "dmfc0\t%0,"STR(source)                                                \
        : "=r" (__res));                                                       \
        __res;})

#define write_32bit_cp0_register(register,value)                               \
        __asm__ __volatile__(                                                  \
        "mtc0\t%0,"STR(register)                                               \
        : : "r" (value));

/*
 * Inline code for use of the ll and sc instructions
 *
 * FIXME: This instruction is only available on MIPS ISA >=3.
 * Since these operations are only being used for atomic operations
 * the easiest workaround for the R[23]00 is to disable interrupts.
 */
#define load_linked(addr)                                                      \
({                                                                             \
	unsigned int __res;                                                    \
                                                                               \
	__asm__ __volatile__(                                                  \
	"ll\t%0,(%1)"                                                          \
	: "=r" (__res)                                                         \
	: "r" ((unsigned int) (addr)));                                        \
                                                                               \
	__res;                                                                 \
})

#define store_conditional(addr,value)                                          \
({                                                                             \
	int	__res;                                                         \
                                                                               \
	__asm__ __volatile__(                                                  \
	"sc\t%0,(%2)"                                                          \
	: "=r" (__res)                                                         \
	: "0" (value), "r" (addr));                                            \
                                                                               \
	__res;                                                                 \
})

/*
 * Bitfields in the cp0 status register
 *
 * Refer to MIPS R4600 manual, page 5-4 for explanation
 */
#define ST0_IE  (1   <<  0)
#define ST0_EXL (1   <<  1)
#define ST0_ERL (1   <<  2)
#define ST0_KSU (3   <<  3)
#define ST0_UX  (1   <<  5)
#define ST0_SX  (1   <<  6)
#define ST0_KX  (1   <<  7)
#define ST0_IM  (255 <<  8)
#define ST0_DE  (1   << 16)
#define ST0_CE  (1   << 17)
#define ST0_CH  (1   << 18)
#define ST0_SR  (1   << 20)
#define ST0_BEV (1   << 22)
#define ST0_RE  (1   << 25)
#define ST0_FR  (1   << 26)
#define ST0_CU  (15  << 28)
#define ST0_CU0 (1   << 28)
#define ST0_CU1 (1   << 29)
#define ST0_CU2 (1   << 30)
#define ST0_CU3 (1   << 31)

#endif /* __ASM_MIPS_MIPSREGS_H */

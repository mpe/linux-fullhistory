/*
 * include/asm-mips/mipsregs.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Ralf Baechle
 */

#ifndef _ASM_MIPS_MIPSREGS_H_
#define _ASM_MIPS_MIPSREGS_H_

/*
 * The following macros are espacially usefull for __asm__
 * inline assembler.
 */

#ifndef __STR
#define __STR(x) #x
#endif
#ifndef STR
#define STR(x) __STR(x)
#endif

/*
 * Coprozessor 0 register names
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
 * Values for pagemask register
 */
#define PM_4K   0x000000000
#define PM_16K  0x000060000
#define PM_64K  0x0001e0000
#define PM_256K 0x0007e0000
#define PM_1M   0x001fe0000
#define PM_4M   0x007fe0000
#define PM_16M  0x01ffe0000

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

#endif /* _ASM_MIPS_MIPSREGS_H_ */

/*
 * Kernel debug stuff to use the Watch registers.
 * Usefull to find stack overflows, dangeling pointers etc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996 by Ralf Baechle
 */
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>

		.set	noreorder
/*
 * Parameter: a0 - logic address to watch
 *                 Currently only KSEG0 addresses are allowed!
 *            a1 - set bit #1 to trap on load references
 *                     bit #0 to trap on store references
 * Results  : none
 */
		LEAF(__watch_set)
		li	t0,0x80000000
		subu	a0,t0
		ori	a0,7
		xori	a0,7
		or	a0,a1
		mtc0	a0,CP0_WATCHLO
		sw	a0,watch_savelo

		jr	ra
		 mtc0	zero,CP0_WATCHHI
		END(__watch_set)

/*
 * Parameter: none
 * Results  : none
 */
		LEAF(__watch_clear)
		jr	ra
		 mtc0	zero,CP0_WATCHLO
		END(__watch_clear)

/*
 * Parameter: none
 * Results  : none
 */
		LEAF(__watch_reenable)
		lw	t0,watch_savelo

		jr	ra
		 mtc0	t0,CP0_WATCHLO
		END(__watch_reenable)

/*
 * Saved value of the c0_watchlo register for watch_reenable()
 */
		.data
watch_savelo:	.word	0
		.text

/*
 * The stuff below are just some kernel debugging gadgets. It is only here
 * because it had to be somewhere and will go away.
 */

/*
 * Parameter: none
 * Results  : none
 */
		LEAF(get_sp)
		jr	ra
		 move	v0,sp
		END(get_sp)

/*
 * Parameter: none
 * Results  : none
 */
		LEAF(get_ra)
		jr	ra
		 move	v0,ra
		END(get_ra)

/*
 * Parameter: none
 * Results  : none
 */
		LEAF(get_status)
		jr	ra
		 mfc0	v0,CP0_STATUS
		END(get_status)

/*
 * Parameter: none
 * Results  : none
 */
		NESTED(print_sp, ((5*SZREG)+ALSZ)&ALMASK, sp)
		.mask	0x80000000,4*SZREG
		PTR_SUBU	sp,((5*SZREG)+ALSZ)&ALMASK
		REG_S		ra,4*SZREG(sp)
		move		a1,sp
		PRINT("$sp == %08lx\n")
		REG_L		ra,4*SZREG(sp)

		jr		ra
		 PTR_ADDU	sp,((5*SZREG)+ALSZ)&ALMASK
		END(print_sp)

/*
 * Parameter: none
 * Results  : none
 */
		NESTED(print_st, ((5*SZREG)+ALSZ)&ALMASK, sp)
		.mask	0x80000000,4*SZREG
		PTR_SUBU	sp,((5*SZREG)+ALSZ)&ALMASK
		REG_S		ra,4*SZREG(sp)
		mfc0		a1,CP0_STATUS
		PRINT("cp0_status == %08lx\n")
		REG_L		ra,4*SZREG(sp)

		jr		ra
		 PTR_ADDU	sp,((5*SZREG)+ALSZ)&ALMASK
		END(print_st)

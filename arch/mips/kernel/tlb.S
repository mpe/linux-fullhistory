/*
 * arch/mips/kernel/head.S
 *
 * Copyright (C) 1994 Waldorf Electronics
 * Written by Ralf Baechle and Andreas Busse
 *
 * Head.S contains the MIPS exception handler and startup code.
 * Flush the TLB
 *
 * FIXME: knows only how to handle R4x00
 * Read appendix f of the R4000 manual before you change something!
 */

#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/bootinfo.h>

		.globl	_tlbflush
_tlbflush:	li	t0,PM_4K
		mtc0	t0,CP0_PAGEMASK
		lw	t0,_boot_info+OFFSET_BOOTINFO_TLB_ENTRIES(t0)
	li	t0,48
		dmtc0	zero,CP0_ENTRYLO0
		dmtc0	zero,CP0_ENTRYLO1
		mfc0	t2,CP0_WIRED
1:		subu	t0,t0,1
		mtc0	t0,CP0_INDEX
		lui	t1,0x0008
		or	t1,t0,t1
		dsll	t1,t1,13
		dmtc0	t1,CP0_ENTRYHI
		bne	t2,t0,1b
		tlbwi					# delay slot
		jr	ra
		nop

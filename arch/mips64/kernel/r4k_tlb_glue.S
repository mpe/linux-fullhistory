/* $Id: r4k_tlb_glue.S,v 1.2 2000/01/17 23:32:46 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#define __ASSEMBLY__
#include <linux/init.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	__INIT
NESTED(__xtlb_refill_debug_tramp, PT_SIZE, sp)
	j	__xtlb_refill_debug
	END(__xtlb_refill_debug_tramp)

NESTED(__tlb_refill_debug_tramp, PT_SIZE, sp)
	j	__tlb_refill_debug
	END(__tlb_refill_debug_tramp)
	__FINIT

	.macro	tlb_handler name
	NESTED(__\name, PT_SIZE, sp)
	SAVE_ALL
	CLI
	dmfc0	t0, CP0_BADVADDR
	sd	t0, PT_BVADDR(sp)
	move	a0, sp
	jal	\name
	j	return
	END(__\name)
	.endm

	tlb_handler	tlb_refill_debug
	tlb_handler	xtlb_refill_debug
	tlb_handler	xtlb_mod_debug
	tlb_handler	xtlb_tlbl_debug
	tlb_handler	xtlb_tlbs_debug

/* $Id: head.S,v 1.4 2000/01/25 01:35:05 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Head.S contains the MIPS exception handler and startup code.
 *
 * Copyright (C) 1994, 1995 Waldorf Electronics
 * Written by Ralf Baechle and Andreas Busse
 * Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#define __ASSEMBLY__
#include <linux/config.h>
#include <linux/init.h>
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/processor.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>

	.text

EXPORT(stext)					# used for profiling
EXPORT(_stext)

	__INIT

NESTED(kernel_entry, 16, sp)			# kernel entry point

#ifdef CONFIG_ARC64
	/* We get launched at a XKPHYS address but the kernel is linked to
	   run at a KSEG0 address, so jump there.  */
	la	t0, 1f
	jr	t0
1:
#endif

	ori	sp, 0xf				# align stack on 16 byte.
	xori	sp, 0xf

	/* Note that all firmware passed argument registers still
	   have their values.  */
	jal	prom_init			# initialize firmware

	CLI					# disable interrupts

	mfc0	t0, CP0_STATUS
	li	t1, ~(ST0_CU1|ST0_CU2|ST0_CU3)
	and	t0, t1
	or	t0, (ST0_CU0|ST0_KX|ST0_SX)
	mtc0	t0, CP0_STATUS

	la	$28, init_task_union		# init current pointer
	daddiu	t0, $28, KERNEL_STACK_SIZE-32
	sd	t0, kernelsp
	dsubu	sp, t0, 4*SZREG			# init stack pointer

	jal	start_kernel
1:	b	1b				# just in case ...
	END(kernel_entry)

	__FINIT

	.comm	kernelsp, 8, 8			# current stackpointer
	.comm	current_pgd, 8, 8		# current page tables

#define PAGE_SIZE	0x1000

	.macro	page name, order=0
	.globl	\name
	.org	. + (PAGE_SIZE << \order)
\name:	.size	\name, (PAGE_SIZE << \order)
	.type	\name, @object
	.endm

	.align	12
	.data

	page	swapper_pg_dir, 1
	page	invalid_pte_table, 1
	page	invalid_pmd_table, 1
	page	empty_bad_page
	page	empty_bad_page_table

/* $Id: r4k_switch.S,v 1.4 1998/07/14 09:15:33 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1998 by Ralf Baechle
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1994, 1995, 1996, by Andreas Busse
 */
#include <asm/asm.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/current.h>
#include <asm/fpregdef.h>
#include <asm/mipsconfig.h>
#include <asm/mipsregs.h>
#include <asm/offset.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

#include <asm/asmmacro.h>

	.set	noreorder
	.set	mips3
	.align	5
	LEAF(r4xx0_resume)
	mfc0	t1, CP0_STATUS
	sw	t1, THREAD_STATUS($28)
	CPU_SAVE_NONSCRATCH($28)
	sw	ra, THREAD_REG31($28)

	/*
	 * The order of restoring the registers takes care of the race
	 * updating $28, $29 and kernelsp without disabling ints.
	 */
	move	$28, a0
	CPU_RESTORE_NONSCRATCH($28)
	addiu	t0, $28, KERNEL_STACK_SIZE-32
	sw	t0, kernelsp
	lw	a3, TASK_MM($28)
	lw	a2, THREAD_STATUS($28)
	lw	a3, MM_CONTEXT(a3)
	mtc0	a2, CP0_STATUS
	andi	a3, a3, 0xff
	jr	ra
	 mtc0	a3, CP0_ENTRYHI
	END(r4xx0_resume)

/*
 * Do lazy fpu context switch.  Saves FPU context to the process in a0
 * and loads the new context of the current process.
 */

#define ST_OFF (KERNEL_STACK_SIZE - 32 - PT_SIZE + PT_STATUS)

LEAF(r4xx0_lazy_fpu_switch)
	mfc0	t0, CP0_STATUS			# enable cp1
	li	t3, 0x20000000
	or	t0, t3
	mtc0	t0, CP0_STATUS

	beqz	a0, 2f				# Save floating point state
	 nor	t3, zero, t3
	lw	t1, ST_OFF(a0)			# last thread looses fpu
	and	t1, t3
	sw	t1, ST_OFF(a0)
	sll	t2, t1, 5
	bgez	t2, 1f
	 sdc1	$f0, (THREAD_FPU + 0x00)(a0)
	FPU_SAVE_16ODD(a0)
1:
	FPU_SAVE_16EVEN(a0, t1)			# clobbers t1
2:

	sll	t0, t0, 5			# load new fp state
	bgez	t0, 1f
	 ldc1	$f0, (THREAD_FPU + 0x00)($28)
	FPU_RESTORE_16ODD($28)
1:
	.set	reorder
	FPU_RESTORE_16EVEN($28, t0)		# clobbers t0
	jr	ra
	END(r4xx0_lazy_fpu_switch)

/*
 * Save a thread's fp context.
 */
	.set	noreorder
LEAF(r4xx0_save_fp)
	mfc0	t0, CP0_STATUS
	sll	t1, t0, 5
	bgez	t1, 1f				# 16 register mode?
	 nop
	FPU_SAVE_16ODD(a0)
1:
	FPU_SAVE_16EVEN(a0, t1)			# clobbers t1
	jr	ra
	 sdc1	$f0, (THREAD_FPU + 0x00)(a0)
	END(r4xx0_save_fp)

/*
 * Load the FPU with signalling NANS.  This bit pattern we're using has
 * the property that no matter wether considered as single or as double
 * precission represents signaling NANS.
 *
 * We initialize fcr31 to rounding to nearest, no exceptions.
 */

#define FPU_DEFAULT  0x00000000

LEAF(r4xx0_init_fpu)
	mfc0	t0, CP0_STATUS
	li	t1, 0x20000000
	or	t0, t1
	mtc0	t0, CP0_STATUS
	sll	t0, t0, 5

	li	t1, FPU_DEFAULT
	ctc1	t1, fcr31

	bgez	t0, 1f			# 16 / 32 register mode?
	 li	t0, -1

	dmtc1	t0, $f1
	dmtc1	t0, $f3
	dmtc1	t0, $f5
	dmtc1	t0, $f7
	dmtc1	t0, $f9
	dmtc1	t0, $f11
	dmtc1	t0, $f13
	dmtc1	t0, $f15
	dmtc1	t0, $f17
	dmtc1	t0, $f19
	dmtc1	t0, $f21
	dmtc1	t0, $f23
	dmtc1	t0, $f25
	dmtc1	t0, $f27
	dmtc1	t0, $f29
	dmtc1	t0, $f31

1:	dmtc1	t0, $f0
	dmtc1	t0, $f2
	dmtc1	t0, $f4
	dmtc1	t0, $f6
	dmtc1	t0, $f8
	dmtc1	t0, $f10
	dmtc1	t0, $f12
	dmtc1	t0, $f14
	dmtc1	t0, $f16
	dmtc1	t0, $f18
	dmtc1	t0, $f20
	dmtc1	t0, $f22
	dmtc1	t0, $f24
	dmtc1	t0, $f26
	dmtc1	t0, $f28
	jr	ra
	 dmtc1	t0, $f30
	END(r4xx0_init_fpu)

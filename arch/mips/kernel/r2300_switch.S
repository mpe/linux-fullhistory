/*
 * r2300_switch.S: R3000/R2000 specific task switching code.
 *
 * Copyright (C) 1994, 1995, 1996 by Ralf Baechle and Andreas Busse
 *
 * Multi-cpu abstraction and macros for easier reading:
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * $Id: r2300_switch.S,v 1.4 1998/04/04 13:59:38 ralf Exp $
 */
#include <asm/asm.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/current.h>
#include <asm/fpregdef.h>
#include <asm/mipsconfig.h>
#include <asm/mipsregs.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

#include <asm/asmmacro.h>

/* XXX The following is fucking losing... find a better way dave. */
MODE_ALIAS	=	0x00e0			# uncachable, dirty, valid

	.text
	.set	mips1
	.set	noreorder
/*
 * Code necessary to switch tasks on an Linux/MIPS machine.
 * FIXME: We don't need to disable interrupts anymore.
 */
	.align	5
	LEAF(r2300_resume)
	mfc0	t1,CP0_STATUS		# Save status register
	sw	t1,THREAD_STATUS($28)
	ori	t2,t1,0x1f		# Disable interrupts
	xori	t2,0x1e
	mtc0	t2,CP0_STATUS
	CPU_SAVE_NONSCRATCH($28)
	sll	t2,t1,2			# Save floating point state
	bgez	t2,1f
	 sw	ra,THREAD_REG31($28)
	FPU_SAVE($28, t0)
1:
	move	$28, a0
	lw	t0,THREAD_PGDIR($28)	# Switch the root pointer
	li	t1,TLB_ROOT		# get PFN
	mtc0	t1,CP0_ENTRYHI
	mtc0	zero,CP0_INDEX
	srl	t0,12			# PFN is 12 bits west
	ori	t0,MODE_ALIAS		# want uncachable, dirty, valid
	mtc0	t0,CP0_ENTRYLO0
	lw	a2,THREAD_STATUS($28)
	tlbwi

	/* Flush TLB. */
	mfc0	t3,CP0_STATUS		# disable interrupts...
	ori	t4,t3,1
	xori	t4,1
	mtc0	t4,CP0_STATUS
	lw      t0,mips_tlb_entries
	mtc0	zero,CP0_ENTRYLO0
1:
	subu	t0,1
	mtc0	t0,CP0_INDEX
	lui	t1,0x0008
	or	t1,t0,t1
	sll	t1,12
	mtc0	t1,CP0_ENTRYHI
	bne	t2,t0,1b
	 tlbwi

	ori	t1,a2,1			# Restore FPU, pipeline magic
	xori	t1,1
	mtc0	t1,CP0_STATUS
	sll	t0,a2,2
	bgez	t0,1f
	 lw	ra,THREAD_REG31($28)
	FPU_RESTORE($28, t0)
1:
	CPU_RESTORE_NONSCRATCH($28)
	addiu	t0, $28, KERNEL_STACK_SIZE-32
	sw	t0,kernelsp
	jr	ra
	 mtc0	a2,CP0_STATUS			# Restore status register
	END(r2300_resume)

/* $Id: winfixup.S,v 1.3 1997/05/18 22:52:26 davem Exp $
 *
 * winfixup.S: Handle cases where user stack pointer is found to be bogus.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/asi.h>
#include <asm/head.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/asm_offsets.h>

	.text
	.align	32

	/* Here are the rules, pay attention.
	 *
	 * The kernel is disallowed from touching user space while
	 * the trap level is greater than zero, except for from within
	 * the window spill/fill handlers.  This must be followed
	 * so that we can easily detect the case where we tried to
	 * spill/fill with a bogus (or unmapped) user stack pointer.
	 *
	 * These are layed out in a special way for cache reasons,
	 * don't touch...
	 */
	.globl	winfix_trampoline, fill_fixup, spill_fixup
fill_fixup:
	ba,pt	%xcc, etrap
	 rd	%pc, %g7
	mov	%l5, %o4
	mov	%l4, %o5
	srlx	%l5, PAGE_SHIFT, %o3
	clr	%o1
	sllx	%o3, PAGE_SHIFT, %o3
	and	%l4, 0x4, %o2

	call	do_sparc64_fault
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,a,pt	%xcc, rtrap
	nop
winfix_trampoline:
	andn		%g5, 0x7f, %g5
	add		%g5, 0x7c, %g5
	wrpr		%g5, %tnpc
	done

spill_fixup:
	rd	%pic, %g1
	ldx	[%g1 + AOFF_task_tss + AOFF_thread_w_saved], %g2
	sll	%g2, 3, %g5
	ldx	[%g1 + AOFF_task_tss + AOFF_thread_flags], %g7
	add	%g1, %g5, %g5
	andcc	%g7, SPARC_FLAG_32BIT, %g0
	stx	%sp, [%g5 + AOFF_task_tss + AOFF_thread_rwbuf_stkptrs]
	sll	%g2, 5, %g5

	bne,pt	%xcc, 1f
	 add	%g1, %g5, %g5
	stx	%l0, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x00]
	stx	%l1, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x08]
	stx	%l2, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x10]
	stx	%l3, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x18]
	stx	%l4, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x20]
	stx	%l5, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x28]

	stx	%l6, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x30]
	stx	%l7, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x38]
	stx	%i0, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x40]
	stx	%i1, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x48]
	stx	%i2, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x50]
	stx	%i3, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x58]
	stx	%i4, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x60]
	stx	%i5, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x68]

	stx	%i6, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x70]
	stx	%i7, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x78]
	b,a,pt	%xcc, 2f
	 add	%g2, 1, %g2
1:
	std	%l0, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x00]
	std	%l2, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x08]
	std	%l4, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x10]
	std	%l6, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x18]

	std	%i0, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x20]
	std	%i2, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x28]
	std	%i4, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x30]
	std	%i6, [%g5 + AOFF_task_tss + AOFF_thread_reg_window + 0x38]
	add	%g2, 1, %g2
2:
	stx	%g2, [%g1 + AOFF_task_tss + AOFF_thread_w_saved]
	rdpr	%tstate, %g1
	nop

	andcc	%g1, TSTATE_PRIV, %g0
	be,pn	%xcc, fill_fixup
	 saved
	retry

/* $Id: locks.S,v 1.4 1997/03/04 16:26:41 jj Exp $
 * locks.S: SMP low-level lock primitives on Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/cprefix.h>
#include <asm/ptrace.h>
#include <asm/psr.h>

	.text
	.align	4

	/* This is called when the initial acquisition attempt of a spin
	 * lock fails.  The calling convention is weird, return address
	 * is in %o7 as usual but we agree with the caller to only touch
	 * and use %g2 as a temporary.  We are passed a ptr to the lock
	 * itself in %g1, %g4 must be restored into %o7 when we return,
	 * and the caller wants us to return to him at three instructions
	 * previous to the call instruction which got us here.  See how
	 * this is used in asm-sparc/smp_lock.h if what I just said
	 * confuses you to no end.
	 */
	.globl	___spinlock_waitfor
___spinlock_waitfor:
1:	orcc	%g2, 0x0, %g0
	bne	1b
	 ldub	[%g1], %g2
	ldstub	[%g1], %g2
	jmpl	%o7 - 12, %g0
	 mov	%g4, %o7

	/* This is called when the kernel master lock holder changes,
	 * caller's PC is in %o7, %o7 must be restored to the value
	 * in %g4 when returning.  The interrupt receiver cpu is to
	 * change to the new kernel lock holder before returning.
	 * The current implementation assumes that irq_rcvreg is a
	 * pointer to a word sized register which can be written with
	 * the MID value of the cpu to receive undirected interrupts.
	 * CPUID is in %g5, and mid_xlate is a byte table which translates
	 * CPUID values into the corresponding MID.
	 */
	.globl	___become_idt
___become_idt:
#ifdef __SMP__
	sethi	%hi(C_LABEL(mid_xlate)), %g2
	or	%g2, %lo(C_LABEL(mid_xlate)), %g2
	ldub	[%g5 + %g2], %g7
	sethi	%hi(C_LABEL(irq_rcvreg)), %g2
	ld	[%g2 + %lo(C_LABEL(irq_rcvreg))], %g2
	st	%g7, [%g2]
#endif
	jmpl	%o7 + 8, %g0
	 mov	%g4, %o7

___lk_busy_spin:
	orcc	%g2, 0, %g0
	bne	___lk_busy_spin
	 ldub	[%g1 + 0], %g2
	b	1f
	 ldstub	[%g1 + 0], %g2	

	.globl	___lock_kernel
___lock_kernel:
	addcc	%g2, -1, %g2
	bcs,a	9f
	 st	%g2, [%g6 + AOFF_task_lock_depth]
	rd	%psr, %g3
	or	%g3, PSR_PIL, %g2
	wr	%g2, 0x0, %psr
	nop; nop; nop
	ldstub	[%g1 + 0], %g2
1:	orcc	%g2, 0, %g0
	bne,a	___lk_busy_spin
	 ldub	[%g1 + 0], %g2
	ldub	[%g1 + 2], %g2
	cmp	%g2, %g5
	be	2f
	 stb	%g5, [%g1 + 1]
	stb	%g5, [%g1 + 2]
#ifdef __SMP__
	set	C_LABEL(mid_xlate), %g2
	ldub	[%g2 + %g5], %g7
	sethi	%hi(C_LABEL(irq_rcvreg)), %g2
	ld	[%g2 + %lo(C_LABEL(irq_rcvreg))], %g2
	st	%g7, [%g2]
#endif
2:	mov	-1, %g2
	st	%g2, [%g6 + AOFF_task_lock_depth]
	wr	%g3, 0x0, %psr
	nop; nop; nop
9:	jmpl	%o7 + 0x8, %g0
	 mov	%g4, %o7

#undef NO_PROC_ID
#define NO_PROC_ID	0xff

	.globl	___unlock_kernel
___unlock_kernel:
	addcc	%g2, 1, %g2
	bne,a	1f
	 st	%g2, [%g6 + AOFF_task_lock_depth]
	rd	%psr, %g3
	or	%g3, PSR_PIL, %g2
	wr	%g2, 0x0, %psr
	nop; nop; nop
	mov	NO_PROC_ID, %g2
	stb	%g2, [%g1 + 1]
	stb	%g0, [%g1 + 0]
	st	%g0, [%g6 + AOFF_task_lock_depth]
	wr	%g3, 0x0, %psr
	nop; nop; nop;
1:	jmpl	%o7 + 0x8, %g0
	 mov	%g4, %o7

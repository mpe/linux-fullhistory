/* $Id: etrap.S,v 1.5 1997/03/13 08:24:01 jj Exp $
 * etrap.S: Preparing for entry into the kernel on Sparc V9.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <asm/pstate.h>
#include <asm/ptrace.h>

	.text
	.align	4
	.globl	etrap
etrap:
	rdpr	%wstate, %g2
	sethi	%uhi(current_set), %g6
	andcc	%g2, 7, %g2
	rdpr	%tstate, %g1
	be,pn	%xcc, 1f		/* What happens more often? etrap when already in priv or from userland? */
	 sllx	%g6, 32, %g6
	/* Just when going from userland to privileged mode, we have to change this stuff. */
	sll	%g2, 3, %g2
	wrpr	%g2, %wstate
	rdpr	%canrestore, %g5
	wrpr	%g0, 0, %canrestore
	wrpr	%g5, 0, %otherwin
1:
	sethi	%hi(current_set), %g4
	or	%g4, %lo(current_set), %g4
	rdpr	%tpc, %g2
	rdpr	%tnpc, %g3
	ldx	[%g6 + %g4], %g6
#ifdef __SMP__
/* FIXME: Fix the above insn for SMP */
#endif
	rd	%y, %g4
	ldx	[%g6 + AOFF_task_tss + AOFF_thread_ksp], %g5
	stx	%g1, [%g5 + REGWIN_SZ + PT_TSTATE]
	stx	%g2, [%g5 + REGWIN_SZ + PT_TPC]
	stx	%g3, [%g5 + REGWIN_SZ + PT_TNPC]
	stx	%g4, [%g5 + REGWIN_SZ + PT_Y]
	rdpr	%pstate, %g1
	save	%g5, -STACK_BIAS, %sp
	andn	%g1, (PSTATE_IG | PSTATE_MG | PSTATE_AG), %l1
	wrpr	%g0, 0x0, %tl
	mov	%g7, %l2
	wrpr	%l1, 0x0, %pstate
	stx	%g1, [%sp + STACK_BIAS + REGWIN_SZ + PT_G1]
	stx	%g2, [%sp + STACK_BIAS + REGWIN_SZ + PT_G2]
	stx	%g3, [%sp + STACK_BIAS + REGWIN_SZ + PT_G3]
	stx	%g4, [%sp + STACK_BIAS + REGWIN_SZ + PT_G4]
	stx	%g5, [%sp + STACK_BIAS + REGWIN_SZ + PT_G5]
	stx	%g6, [%sp + STACK_BIAS + REGWIN_SZ + PT_G6]
	stx	%g7, [%sp + STACK_BIAS + REGWIN_SZ + PT_G7]
	stx	%i0, [%sp + STACK_BIAS + REGWIN_SZ + PT_I0]
	stx	%i1, [%sp + STACK_BIAS + REGWIN_SZ + PT_I1]
	stx	%i2, [%sp + STACK_BIAS + REGWIN_SZ + PT_I2]
	stx	%i3, [%sp + STACK_BIAS + REGWIN_SZ + PT_I3]
	stx	%i4, [%sp + STACK_BIAS + REGWIN_SZ + PT_I4]
	stx	%i5, [%sp + STACK_BIAS + REGWIN_SZ + PT_I5]
	stx	%i6, [%sp + STACK_BIAS + REGWIN_SZ + PT_I6]
	stx	%i7, [%sp + STACK_BIAS + REGWIN_SZ + PT_I7]
	jmpl	%l2 + 0x4, %g0
	 wrpr	%l1, PSTATE_IE, %pstate

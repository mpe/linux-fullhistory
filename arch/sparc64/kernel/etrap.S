/* $Id: etrap.S,v 1.10 1997/04/03 13:03:49 davem Exp $
 * etrap.S: Preparing for entry into the kernel on Sparc V9.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <asm/asi.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/spitfire.h>
#include <asm/head.h>

/* We assume that pstate, when entering this, has AG and IE bits set, MG and IG clear */

	.text
	.align	32
	.globl	etrap, etrap_irq
etrap:
	rdpr		%pil, %g4
etrap_irq:
	rdpr		%wstate, %g5
	sllx		%g4, 20, %g4
	rdpr		%tstate, %g1
	rdpr		%tpc, %g2
	or		%g1, %g4, %g1
	rdpr		%tnpc, %g3

	/* What happens more often? etrap when already in priv or from userland? */
	andcc		%g1, TSTATE_PRIV, %g0
	bne,a,pn	%xcc, 1f
	 sub		%sp, REGWIN_SZ + TRACEREG_SZ - STACK_BIAS, %g5

	/* Just when going from userland to privileged mode,
	 * we have to change this stuff.
	 *
	 * Setup to run in NUCLEUS context, stash user context in
	 * secondary for later trap return.  Note we must not change
	 * trap level until PRIMARY_CONTEXT is set to zero, else
	 * we fall out of NUCLEUS too soon and crash hard.
	 */
	mov		PRIMARY_CONTEXT, %g7
	ldxa		[%g7] ASI_DMMU, %g4
	mov		SECONDARY_CONTEXT, %g6
	stxa		%g0, [%g7] ASI_DMMU
	stxa		%g4, [%g6] ASI_DMMU
	wrpr		%g0, 0x0, %tl

	sll		%g5, 3, %g5
	sethi		%uhi(KERNBASE), %g4
	or		%g4, %ulo(KERNBASE), %g4
	sethi		%hi(current_set), %g6
	or		%g6, %lo(current_set), %g6
	sllx		%g4, 32, %g4
	wrpr		%g5, %wstate
	rdpr		%canrestore, %g5
	ldx		[%g6 + %g4], %g6
#ifdef __SMP__
/* FIXME: Fix the above insn for SMP */
#endif
	wrpr		%g0, 0, %canrestore
	wrpr		%g5, 0, %otherwin
	ba,pt		%xcc, 2f
	 ldx		[%g6 + AOFF_task_saved_kernel_stack], %g5
1:
	wrpr		%g0, 0x0, %tl
2:
	rd		%y, %g4
	stx		%g1, [%g5 + REGWIN_SZ + PT_V9_TSTATE]
	stx		%g2, [%g5 + REGWIN_SZ + PT_V9_TPC]
	stx		%g3, [%g5 + REGWIN_SZ + PT_V9_TNPC]
	stx		%g4, [%g5 + REGWIN_SZ + PT_V9_Y]
	rdpr		%pstate, %g1
	save		%g5, -STACK_BIAS, %sp
	mov		%g1, %l1
	mov		%g7, %l2
	wrpr		%l1, PSTATE_AG, %pstate
	stx		%g1, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G1]
	stx		%g2, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G2]
	stx		%g3, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G3]
	stx		%g4, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G4]
	stx		%g5, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G5]
	stx		%g6, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G6]
	stx		%g7, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G7]
	stx		%i0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0]
	stx		%i1, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I1]
	stx		%i2, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I2]
	stx		%i3, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I3]
	stx		%i4, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I4]
	stx		%i5, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I5]
	stx		%i6, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I6]
	stx		%i7, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I7]
	wrpr		%l1, (PSTATE_IE | PSTATE_AG), %pstate
	sethi		%uhi(KERNBASE), %g4
	or		%g4, %ulo(KERNBASE), %g4
	sethi		%hi(current_set), %g6
	or		%g6, %lo(current_set), %g6
	sllx		%g4, 32, %g4
	jmpl		%l2 + 0x4, %g0
	 ldx		[%g6 + %g4], %g6
#ifdef __SMP__
/* FIXME: Fix the above insn for SMP */
#endif

	.globl	etraptl1
etraptl1:
	rdpr	%tl, %g4
	rdpr	%tstate, %g1
	sub	%g4, 1, %g4
	rdpr	%tpc, %g2
	rdpr	%tnpc, %g3
	wrpr	%g4, 0x0, %tl
	ba,pt	%xcc, 1b
	 sub	%sp, REGWIN_SZ + TRACEREG_SZ - STACK_BIAS, %g5

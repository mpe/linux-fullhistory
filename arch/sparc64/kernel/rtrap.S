/* $Id: rtrap.S,v 1.4 1997/03/13 16:24:55 jj Exp $
 * rtrap.S: Preparing for return from trap on Sparc V9.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <asm/pstate.h>
#include <asm/ptrace.h>

	.text
	.align	4
	.globl	rtrap
rtrap:
	sethi	%hi(intr_count), %l0
	or	%l0, %lo(intr_count), %l0
	ld	[%l0 + %g4], %l1
	sethi	%hi(bh_active), %l2
	brz,pt	%l1, 2f
	 or	%l2, %lo(bh_active), %l2
	sethi	%hi(bh_mask), %l1
	or	%l1, %lo(bh_mask), %l1
1:
	ldx	[%l2 + %g4], %l3
	ldx	[%l1 + %g4], %l4
	andcc	%l3, %l4, %g0
	be,pt	%xcc, 2f
	 mov	1, %l7
	call	do_bottom_half
	 st	%l7, [%l0 + %g4]
	ba,pt	%xcc, 1b
	 st	%g0, [%l0 + %g4]	
2:
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_TSTATE], %l1
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_TPC], %l2
	andcc	%l1, TSTATE_PRIV, %l3
	rdpr	%pstate, %l7
	be,pt	%icc, to_user
	 andn	%l7, PSTATE_IE, %l7
3:
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_G1], %g1
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_G2], %g2
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_G3], %g3
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_G4], %g4
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_G5], %g5
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_G6], %g6
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_G7], %g7
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_I0], %i0
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_I1], %i1
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_I2], %i2
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_I3], %i3
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_I4], %i4
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_I5], %i5
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_I6], %i6
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_I7], %i7
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_Y], %o3
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_TNPC], %o2
	wr	%o3, %g0, %y
	wrpr	%l7, %g0, %pstate
	wrpr	%g0, 1, %tl
	wrpr	%l1, %g0, %tstate
	wrpr	%l2, %g0, %tpc
	brnz,pn	%l3, 1f
	 wrpr	%o2, %g0, %tnpc
	/* we came here from to_user, ie. we have now AG */
	restore
	rdpr	%wstate, %g1
	rdpr	%otherwin, %g2
	srl	%g1, 3, %g1
	wrpr	%g2, %g0, %canrestore
	wrpr	%g1, %g0, %wstate
	wrpr	%g0, %g0, %otherwin
	retry
1:
	restore
	retry
to_user:
	sethi	%hi(need_resched), %l0
	or	%l0, %lo(need_resched), %l0
	ld	[%l0 + %g4], %l0
	wrpr	%o4, PSTATE_IE, %pstate
	brz,pt	%l0, 1f
	 ldx	[%g6 + AOFF_task_signal], %l0
	call	schedule
	 nop
1:
	ldx	[%g6 + AOFF_task_blocked], %o0
	or	%l7, PSTATE_AG, %l7		! Will need this for setting back wstate
	andcc	%l0, %o0, %g0
	be,pt	%xcc, 3b
	 mov	%l5, %o2  
	mov	%l6, %o3
	add	%sp, STACK_BIAS + REGWIN_SZ, %o1
	call	do_signal
	 add	%o7, 3b-.-4, %o7

/* $Id: rtrap.S,v 1.14 1997/05/18 08:42:14 davem Exp $
 * rtrap.S: Preparing for return from trap on Sparc V9.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/asi.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/spitfire.h>

/* We assume here this is entered with AG, MG and IG bits in pstate clear */

	.text
	.align	4
	.globl	rtrap
rtrap:
	sethi	%hi(bh_active), %l2
	or	%l2, %lo(bh_active), %l2
	sethi	%hi(bh_mask), %l1
	or	%l1, %lo(bh_mask), %l1
	ldx	[%l2 + %g4], %l3
	ldx	[%l1 + %g4], %l4
	andcc	%l3, %l4, %g0
	be,pt	%xcc, 2f
	 nop
	call	do_bottom_half
	 nop
2:
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TSTATE], %l1
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TPC], %l2
	sethi	%hi(0xf << 20), %l4
	andcc	%l1, TSTATE_PRIV, %l3
	and	%l1, %l4, %l4
	rdpr	%pstate, %l7
	andn	%l1, %l4, %l1			/* XXX May not be needed -DaveM */
	be,pt	%icc, to_user
	 andn	%l7, PSTATE_IE, %l7
3:
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G1], %g1
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G2], %g2
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G3], %g3
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G4], %g4
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G5], %g5
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G6], %g6
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G7], %g7
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0], %i0
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I1], %i1
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I2], %i2
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I3], %i3
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I4], %i4
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I5], %i5
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I6], %i6
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I7], %i7
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_Y], %o3
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TNPC], %o2
	rdpr	%tl, %o4
	wr	%o3, %g0, %y
	add	%o4, 1, %o4

	srl	%l4, 20, %l4
	wrpr	%l7, %g0, %pstate
	wrpr	%l4, 0x0, %pil
	wrpr	%o4, %g0, %tl
	wrpr	%l1, %g0, %tstate
	wrpr	%l2, %g0, %tpc
	brnz,pn	%l3, 1f
	 wrpr	%o2, %g0, %tnpc

	/* We came here from to_user, ie. we have now AG.
	 * Also have to push user context back into primary.
	 */
	mov	SECONDARY_CONTEXT, %g6
	mov	PRIMARY_CONTEXT, %g7
	ldxa	[%g6] ASI_DMMU, %g4
	stxa	%g4, [%g7] ASI_DMMU
	membar	#Sync			/* XXX flushi would be better -DaveM */

	rdpr	%wstate, %g1
	rdpr	%otherwin, %g2
	srl	%g1, 3, %g1
	wrpr	%g2, %g0, %canrestore
	wrpr	%g1, %g0, %wstate
	wrpr	%g0, %g0, %otherwin
1:
	restore
	retry
to_user:
	sethi	%hi(need_resched), %l0
	or	%l0, %lo(need_resched), %l0
	ld	[%l0 + %g4], %l0
	wrpr	%l7, PSTATE_IE, %pstate
	brz,pt	%l0, 1f
	 ldx	[%g6 + AOFF_task_signal], %l0
	call	schedule
	 nop
1:
	ldx	[%g6 + AOFF_task_blocked], %o0
	or	%l7, PSTATE_AG, %l7		! Will need this for setting back wstate
	andncc	%l0, %o0, %g0
	be,pt	%xcc, check_user_wins
	 mov	%l5, %o2  
	mov	%l6, %o3
	call	do_signal
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o1
check_user_wins:
	ldx	[%g6 + AOFF_task_tss + AOFF_thread_w_saved], %o2
	brz,pt	%o2, 3b
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o1
	call	fault_in_user_windows
	 add	%o7, 3b-.-4, %o7

/* $Id: rtrap.S,v 1.23 1997/06/16 07:38:41 davem Exp $
 * rtrap.S: Preparing for return from trap on Sparc V9.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/asi.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/spitfire.h>
#include <asm/head.h>

	/* We assume here that this is entered with AG, MG and IG bits
	 * in pstate clear.
	 */

	.text
	.align	32
	.globl	rtrap_clr_l6, rtrap
rtrap_clr_l6:
	ba,pt		%xcc, rtrap
	 clr		%l6
rtrap:	sethi		%hi(bh_active), %l2
	or		%l2, %lo(bh_active), %l2
	sethi		%hi(bh_mask), %l1
	or		%l1, %lo(bh_mask), %l1
	ldx		[%l2 + %g4], %l4
	ldx		[%l1 + %g4], %l7

	andcc		%l4, %l7, %g0
	be,pt		%xcc, 2f
	 nop
	call		do_bottom_half
	 nop
2:	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TSTATE], %l1
	sethi		%hi(0xf << 20), %l4
	andcc		%l1, TSTATE_PRIV, %l3

	and		%l1, %l4, %l4
	rdpr		%pstate, %l7
	andn		%l1, %l4, %l1
	be,pt		%icc, to_user
	 andn		%l7, PSTATE_IE, %l7
3:	ldx		[%g6 + AOFF_task_tss + AOFF_thread_ctx], %l0
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G1], %g1
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G2], %g2

	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G3], %g3
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G4], %g4
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G5], %g5
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G6], %g6
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G7], %g7
	wrpr		%l7, PSTATE_AG, %pstate
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0], %i0
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I1], %i1

	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I2], %i2
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I3], %i3
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I4], %i4
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I5], %i5
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I6], %i6
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I7], %i7
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_Y], %o3
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TPC], %l2

	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TNPC], %o2
	wr		%o3, %g0, %y
	srl		%l4, 20, %l4
	wrpr		%l4, 0x0, %pil
	wrpr		%g0, 0x1, %tl
	wrpr		%l1, %g0, %tstate
	wrpr		%l2, %g0, %tpc
	mov		PRIMARY_CONTEXT, %l7

	wrpr		%o2, %g0, %tnpc
	brnz,a,pn	%l3, 1f
	 restore
	sethi		%uhi(KERNBASE), %l5
	sllx		%l5, 32, %l5
	stxa		%l0, [%l7] ASI_DMMU
	flush		%l5
	rdpr		%wstate, %l1

	rdpr		%otherwin, %l2
	srl		%l1, 3, %l1
	wrpr		%l2, %g0, %canrestore
	wrpr		%l1, %g0, %wstate
	wrpr		%g0, %g0, %otherwin
	restore
	rdpr		%canrestore, %g1
	wrpr		%g1, 0x0, %cleanwin

1:	retry
to_user:
	sethi	%hi(need_resched), %l0
	or	%l0, %lo(need_resched), %l0
	ld	[%l0 + %g4], %l0
	wrpr	%l7, PSTATE_IE, %pstate
	brz,pt	%l0, check_signal
	 ldx	[%g6 + AOFF_task_signal], %l0
	nop

	call	schedule
	 nop
	ba,pt	%xcc, check_signal
	 ldx	[%g6 + AOFF_task_signal], %l0
check_signal:
	ldx	[%g6 + AOFF_task_blocked], %o0
	andncc	%l0, %o0, %g0
	be,a,pt	%xcc, check_user_wins
	 ldx	[%g6 + AOFF_task_tss + AOFF_thread_w_saved], %o2

	mov	%l5, %o2  
	mov	%l6, %o3
	call	do_signal
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o1
	ldx	[%g6 + AOFF_task_tss + AOFF_thread_w_saved], %o2
	clr	%l6
check_user_wins:
	brz,pt	%o2, 3b
	 nop

	call	fault_in_user_windows
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,a,pt	%xcc, 3b
	nop
	nop
	nop
	nop
	nop

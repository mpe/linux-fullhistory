/* $Id: rtrap.S,v 1.30 1997/08/10 04:49:33 davem Exp $
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

		.text
		.align			32
		.globl			rtrap_clr_l6, rtrap
#define		PTREGS_OFF		(STACK_BIAS + REGWIN_SZ)
rtrap_clr_l6:	ba,pt			%xcc, rtrap
		 clr			%l6
rtrap:		sethi			%hi(bh_active), %l2
		sethi			%hi(bh_mask), %l1
		ldx			[%l2 + %lo(bh_active)], %l4
		ldx			[%l1 + %lo(bh_mask)], %l7
		andcc			%l4, %l7, %g0
		be,pt			%xcc, 2f

		 nop
		call			do_bottom_half
		 nop
2:		ldx			[%sp + PTREGS_OFF + PT_V9_TSTATE], %l1
		sethi			%hi(0xf << 20), %l4
		andcc			%l1, TSTATE_PRIV, %l3
		and			%l1, %l4, %l4
		rdpr			%pstate, %l7

		andn			%l1, %l4, %l1
		be,pt			%icc, to_user
		 andn			%l7, PSTATE_IE, %l7
rt_continue:	ld			[%sp + PTREGS_OFF + PT_V9_FPRS], %l2
		ld			[%g6 + AOFF_task_tss + AOFF_thread_ctx], %l0
		ldx			[%sp + PTREGS_OFF + PT_V9_G1], %g1
		brnz,pn			%l2, rt_fpu_restore
		 ldx			[%sp + PTREGS_OFF + PT_V9_G2], %g2

rt_after_fpu:	ldx			[%sp + PTREGS_OFF + PT_V9_G3], %g3
		mov			%g6, %l6
		ldx			[%sp + PTREGS_OFF + PT_V9_G4], %g4
		ldx			[%sp + PTREGS_OFF + PT_V9_G5], %g5
		ldx			[%sp + PTREGS_OFF + PT_V9_G6], %g6
		ldx			[%sp + PTREGS_OFF + PT_V9_G7], %g7
		wrpr			%l7, PSTATE_AG, %pstate
		ldx			[%sp + PTREGS_OFF + PT_V9_I0], %i0

		ldx			[%sp + PTREGS_OFF + PT_V9_I1], %i1
		ldx			[%sp + PTREGS_OFF + PT_V9_I2], %i2
		ldx			[%sp + PTREGS_OFF + PT_V9_I3], %i3
		ldx			[%sp + PTREGS_OFF + PT_V9_I4], %i4
		ldx			[%sp + PTREGS_OFF + PT_V9_I5], %i5
		ldx			[%sp + PTREGS_OFF + PT_V9_I6], %i6
		ldx			[%sp + PTREGS_OFF + PT_V9_I7], %i7
		ld			[%sp + PTREGS_OFF + PT_V9_Y], %o3

		ldx			[%sp + PTREGS_OFF + PT_V9_TPC], %l2
		ldx			[%sp + PTREGS_OFF + PT_V9_TNPC], %o2
		wr			%o3, %g0, %y
		srl			%l4, 20, %l4
		wrpr			%l4, 0x0, %pil
		wrpr			%g0, 0x1, %tl
		wrpr			%l1, %g0, %tstate
		wrpr			%l2, %g0, %tpc

		wr			%g0, ASI_DMMU, %asi
		brnz,pn			%l3, kern_rtt
		 wrpr			%o2, %g0, %tnpc
		stxa			%l0, [%g0 + SECONDARY_CONTEXT] %asi
		flush			%l6
		stxa			%l0, [%g0 + PRIMARY_CONTEXT] %asi
		flush			%l6
		rdpr			%wstate, %l1

		rdpr			%otherwin, %l2
		srl			%l1, 3, %l1
		wrpr			%l2, %g0, %canrestore
		wrpr			%l1, %g0, %wstate
		wrpr			%g0, %g0, %otherwin
		restore
		rdpr			%canrestore, %g1
		wrpr			%g1, 0x0, %cleanwin

		retry
kern_rtt:	restore
		retry
to_user:	lduw			[%g6 + AOFF_task_processor], %o0
		mov			1, %o1
		sethi			%hi(need_resched), %l0
		ldx			[%l0 + %lo(need_resched)], %l0
		sllx			%o1, %o0, %o1

		wrpr			%l7, PSTATE_IE, %pstate
		andcc			%o1, %l0, %g0
		be,pt			%xcc, check_signal
		 ldx			[%g6 + AOFF_task_signal], %l0
		call			schedule
		 nop
		ldx			[%g6 + AOFF_task_signal], %l0
		nop

check_signal:	ldx			[%g6 + AOFF_task_blocked], %o0
		andncc			%l0, %o0, %g0
		be,pt			%xcc, check_user_wins
		 ldx			[%g6 + AOFF_task_tss + AOFF_thread_w_saved], %o2
		mov			%l5, %o2  
		mov			%l6, %o3
		call			do_signal
		 add			%sp, STACK_BIAS + REGWIN_SZ, %o1

		ldx			[%g6 + AOFF_task_tss + AOFF_thread_w_saved], %o2
		clr			%l6
check_user_wins:brz,pt			%o2, rt_continue
		 nop
		call			fault_in_user_windows
		 add			%sp, STACK_BIAS + REGWIN_SZ, %o0
		ba,a,pt			%xcc, rt_continue
rt_fpu_restore:	wr			%g0, FPRS_FEF, %fprs

		add			%sp, PTREGS_OFF + TRACEREG_SZ, %g4
		wr			%g0, ASI_BLK_P, %asi
		membar			#StoreLoad | #LoadLoad
		ldda			[%g4 + 0x000] %asi, %f0
		ldda			[%g4 + 0x040] %asi, %f16
		ldda			[%g4 + 0x080] %asi, %f32
		ldda			[%g4 + 0x0c0] %asi, %f48
		ldx			[%g4 + 0x100], %fsr

		ldx			[%g4 + 0x108], %g3
		membar			#Sync
		b,pt			%xcc, rt_after_fpu
		 wr			%g3, 0, %gsr
		nop
		nop
		nop
		nop
#undef PTREGS_OFF

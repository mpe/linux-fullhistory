/* $Id: rtrap.S,v 1.37 1997/12/11 15:14:54 jj Exp $
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

		ld			[%sp + PTREGS_OFF + PT_V9_FPRS], %l2
		andcc			%l2, FPRS_FEF, %g0
		be,pt			%icc, rt_continue
		 and			%l2, FPRS_DL, %l6
		wr			%g0, FPRS_FEF, %fprs
		ldx			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x108], %g5
		membar			#StoreLoad | #LoadLoad
		brz,pn			%l6, 1f
		 wr			%g0, ASI_BLK_P, %asi
		ldda			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x000] %asi, %f0
		ldda			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x040] %asi, %f16
1:		andcc			%l2, FPRS_DU, %g0
		be,pn			%icc, 1f
		 wr			%g5, 0, %gsr
		ldda			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x080] %asi, %f32
		ldda			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x0c0] %asi, %f48
1:		membar			#Sync
		ldx			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x100], %fsr
rt_continue:	lduh			[%g6 + AOFF_task_tss + AOFF_thread_ctx], %l0
		ldx			[%sp + PTREGS_OFF + PT_V9_G1], %g1
		ldx			[%sp + PTREGS_OFF + PT_V9_G2], %g2
		ldx			[%sp + PTREGS_OFF + PT_V9_G3], %g3
		mov			%g6, %o5
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
		wrpr			%o2, %g0, %tnpc
		mov			PRIMARY_CONTEXT, %l7
		brnz,pn			%l3, kern_rtt
		 mov			SECONDARY_CONTEXT, %o4
		stxa			%l0, [%l7] ASI_DMMU
		stxa			%l0, [%o4] ASI_DMMU
		flush			%o5
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
to_user:	sethi			%hi(need_resched), %l0
		ldx			[%l0 + %lo(need_resched)], %l0
		wrpr			%l7, PSTATE_IE, %pstate
		orcc			%g0, %l0, %g0
		be,a,pt			%xcc, check_signal
		 lduw			[%g6 + AOFF_task_sigpending], %l0

		call			schedule
		 nop
		lduw			[%g6 + AOFF_task_sigpending], %l0
check_signal:	ld			[%sp + PTREGS_OFF + PT_V9_FPRS], %l2
		brz,a,pt		%l0, check_user_wins
		 lduh			[%g6 + AOFF_task_tss + AOFF_thread_w_saved], %o2
		clr			%o0
		mov			%l5, %o2  
		mov			%l6, %o3
		call			do_signal
		 add			%sp, STACK_BIAS + REGWIN_SZ, %o1
		lduh			[%g6 + AOFF_task_tss + AOFF_thread_w_saved], %o2
		clr			%l6
check_user_wins:brz,pt			%o2, 1f
		 sethi			%hi(TSTATE_PEF), %o3

		call			fault_in_user_windows
		 add			%sp, STACK_BIAS + REGWIN_SZ, %o0

		sethi			%hi(TSTATE_PEF), %o3
1:		andcc			%l2, FPRS_FEF, %g0
		be,a,pt			%icc, rt_continue
		 andn			%l1, %o3, %l1		! If fprs.FEF is not set, disable tstate.PEF
		ldx			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x108], %o3
		lduh			[%g6 + AOFF_task_tss + AOFF_thread_flags], %l2
		wr			%g0, FPRS_FEF, %fprs
		wr			%o3, 0, %gsr
		andcc			%l2, SPARC_FLAG_USEDFPUL, %g0
		bne,pn			%icc, 2f
		 andcc			%l2, SPARC_FLAG_USEDFPUU, %g0
		fzero			%f0
		bne,pn			%icc, 1f
		 fzero			%f2
		faddd			%f0, %f2, %f4
		fmuld			%f0, %f2, %f6
		faddd			%f0, %f2, %f8
		fmuld			%f0, %f2, %f10
		faddd			%f0, %f2, %f12
		fmuld			%f0, %f2, %f14
		faddd			%f0, %f2, %f16
		fmuld			%f0, %f2, %f18
		faddd			%f0, %f2, %f20
		fmuld			%f0, %f2, %f22
		faddd			%f0, %f2, %f24
		fmuld			%f0, %f2, %f26
		faddd			%f0, %f2, %f28
		fmuld			%f0, %f2, %f30
		faddd			%f0, %f2, %f32
		fmuld			%f0, %f2, %f34
		faddd			%f0, %f2, %f36
		fmuld			%f0, %f2, %f38
		faddd			%f0, %f2, %f40
		fmuld			%f0, %f2, %f42
		faddd			%f0, %f2, %f44
		fmuld			%f0, %f2, %f46
		faddd			%f0, %f2, %f48
		fmuld			%f0, %f2, %f50
		faddd			%f0, %f2, %f52
		fmuld			%f0, %f2, %f54
		faddd			%f0, %f2, %f56
		fmuld			%f0, %f2, %f58
		faddd			%f0, %f2, %f60
		ldx			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x100], %fsr
		ba,pt			%xcc, rt_continue
		 wr			%g0, FPRS_FEF, %fprs
1:		wr			%g0, ASI_BLK_P, %asi
		membar			#StoreLoad | #LoadLoad
		ldda			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x080] %asi, %f32
		ldda			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x0c0] %asi, %f48
		faddd			%f0, %f2, %f4
		fmuld			%f0, %f2, %f6
		faddd			%f0, %f2, %f8
		fmuld			%f0, %f2, %f10
		faddd			%f0, %f2, %f12
		fmuld			%f0, %f2, %f14
		faddd			%f0, %f2, %f16
		fmuld			%f0, %f2, %f18
		faddd			%f0, %f2, %f20
		fmuld			%f0, %f2, %f22
		faddd			%f0, %f2, %f24
		fmuld			%f0, %f2, %f26
		faddd			%f0, %f2, %f28
		fmuld			%f0, %f2, %f30
		membar			#Sync
		ldx			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x100], %fsr
		ba,pt			%xcc, rt_continue
		 wr			%g0, FPRS_FEF, %fprs
2:		membar			#StoreLoad | #LoadLoad
		andcc			%l2, SPARC_FLAG_USEDFPUU, %g0
		bne,pt			%icc, 3f
		 wr			%g0, ASI_BLK_P, %asi
		ldda			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x000] %asi, %f0
		ldda			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x040] %asi, %f16
		fzero			%f32
		fzero			%f34
		faddd			%f32, %f34, %f36
		fmuld			%f32, %f34, %f38
		faddd			%f32, %f34, %f40
		fmuld			%f32, %f34, %f42
		faddd			%f32, %f34, %f44
		fmuld			%f32, %f34, %f46
		faddd			%f32, %f34, %f48
		fmuld			%f32, %f34, %f50
		faddd			%f32, %f34, %f52
		fmuld			%f32, %f34, %f54
		faddd			%f32, %f34, %f56
		fmuld			%f32, %f34, %f58
		faddd			%f32, %f34, %f60
		fmuld			%f32, %f34, %f62
		membar			#Sync
		ldx			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x100], %fsr
		ba,pt			%xcc, rt_continue
		 wr			%g0, FPRS_FEF, %fprs
3:		ldda			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x000] %asi, %f0
		ldda			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x040] %asi, %f16
		ldda			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x080] %asi, %f32
		ldda			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x0c0] %asi, %f48
		membar			#Sync
		ldx			[%sp + PTREGS_OFF + TRACEREG_SZ + 0x100], %fsr
		ba,pt			%xcc, rt_continue
		 wr			%g0, FPRS_FEF, %fprs

#undef PTREGS_OFF

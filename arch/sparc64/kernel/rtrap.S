/* $Id: rtrap.S,v 1.45 1998/11/09 15:33:29 davem Exp $
 * rtrap.S: Preparing for return from trap on Sparc V9.
 *
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/asi.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/spitfire.h>
#include <asm/head.h>
#include <asm/visasm.h>

#define		PTREGS_OFF		(STACK_BIAS + REGWIN_SZ)

		.text
		.align			32
		.globl			rtrap_clr_l6, rtrap
rtrap_clr_l6:	clr			%l6
		/* Fall through */
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

		ldub			[%g6 + AOFF_task_tss + AOFF_thread_fpdepth], %l5
		brz,pt			%l5, rt_continue
		 srl			%l5, 1, %o0
		add			%g6, AOFF_task_tss + AOFF_thread_fpsaved, %l6
		ldub			[%l6 + %o0], %l2
		sub			%l5, 2, %l5
		add			%g6, AOFF_task_tss + AOFF_thread_gsr, %o1
		andcc			%l2, (FPRS_FEF|FPRS_DU), %g0
		be,pt			%icc, 2f
		 and			%l2, FPRS_DL, %l6
		andcc			%l2, FPRS_FEF, %g0
		be,pn			%icc, 5f
		 sll			%o0, 3, %o5
		rd			%fprs, %g5
		wr			%g5, FPRS_FEF, %fprs
		ldub			[%o1 + %o0], %g5
		add			%g6, AOFF_task_tss + AOFF_thread_xfsr, %o1
		membar			#StoreLoad | #LoadLoad
		sll			%o0, 8, %o2
		add			%g6, AOFF_task_fpregs, %o3
		brz,pn			%l6, 1f
		 add			%g6, AOFF_task_fpregs+0x40, %o4
		ldda			[%o3 + %o2] ASI_BLK_P, %f0
		ldda			[%o4 + %o2] ASI_BLK_P, %f16
1:		andcc			%l2, FPRS_DU, %g0
		be,pn			%icc, 1f
		 wr			%g5, 0, %gsr
		add			%o2, 0x80, %o2
		ldda			[%o3 + %o2] ASI_BLK_P, %f32
		ldda			[%o4 + %o2] ASI_BLK_P, %f48
1:		membar			#Sync
		ldx			[%o1 + %o5], %fsr
2:		stb			%l5, [%g6 + AOFF_task_tss + AOFF_thread_fpdepth]
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
		brnz,pn			%l3, kern_rtt
		 mov			PRIMARY_CONTEXT, %l7
		stxa			%l0, [%l7] ASI_DMMU
		stxa			%l0, [%l7 + %l7] ASI_DMMU
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
to_user:	ldx			[%g6 + AOFF_task_need_resched], %l0
		wrpr			%l7, PSTATE_IE, %pstate
		orcc			%g0, %l0, %g0
		be,a,pt			%xcc, check_signal
		 lduw			[%g6 + AOFF_task_sigpending], %l0

		call			schedule
		 nop
		lduw			[%g6 + AOFF_task_sigpending], %l0
check_signal:	brz,a,pt		%l0, check_user_wins
		 nop
		clr			%o0
		mov			%l5, %o2  
		mov			%l6, %o3
		call			do_signal
		 add			%sp, STACK_BIAS + REGWIN_SZ, %o1
		clr			%l6

		/* We must not take any traps between here and the actual
		 * return to user-space.  If we do we risk having windows
		 * saved to the thread struct between the test and the
		 * actual return from trap.  --DaveM
		 */
check_user_wins:
		wrpr			%l7, 0x0, %pstate
		lduh			[%g6 + AOFF_task_tss + AOFF_thread_w_saved], %o2
		brz,pt			%o2, 1f
		 sethi			%hi(TSTATE_PEF), %l6

		wrpr			%l7, PSTATE_IE, %pstate
		call			fault_in_user_windows
		 add			%sp, STACK_BIAS + REGWIN_SZ, %o0
		/* It is OK to leave interrupts on now because if
		 * fault_in_user_windows has returned it has left us
		 * with a clean user stack state.
		 */
1:
#if 0
		call			rtrap_check
		 add			%sp, STACK_BIAS + REGWIN_SZ, %o0
#endif
		lduh			[%g6 + AOFF_task_tss + AOFF_thread_flags], %l5
		andcc			%l5, 0x200, %g0
		be,pt			%xcc, 1f
		 nop

		/* Don't forget to preserve user window invariants. */
		wrpr			%l7, PSTATE_IE, %pstate
		call			update_perfctrs
		 nop
		wrpr			%l7, 0x0, %pstate
		lduh			[%g6 + AOFF_task_tss + AOFF_thread_w_saved], %o2
		brz,pt			%o2, 1f
		 sethi			%hi(TSTATE_PEF), %l6
		wrpr			%l7, PSTATE_IE, %pstate
		call			fault_in_user_windows
		 add			%sp, STACK_BIAS + REGWIN_SZ, %o0

1:
		andcc			%l1, %l6, %g0
		be,pt			%xcc, rt_continue
		 stb			%g0, [%g6 + AOFF_task_tss + AOFF_thread_fpdepth] ! This is neccessary for non-syscall rtraps only

		rd			%fprs, %l5
		andcc			%l5, FPRS_FEF, %g0
		be,a,pn			%icc, rt_continue
		 andn			%l1, %l6, %l1
		ba,pt			%xcc, rt_continue+4
		 lduh			[%g6 + AOFF_task_tss + AOFF_thread_ctx], %l0

5:		wr			%g0, FPRS_FEF, %fprs
		membar			#StoreLoad | #LoadLoad
		sll			%o0, 8, %o2
		add			%g6, AOFF_task_fpregs+0x80, %o3
		add			%g6, AOFF_task_fpregs+0xc0, %o4
		ldda			[%o3 + %o2] ASI_BLK_P, %f32
		ldda			[%o4 + %o2] ASI_BLK_P, %f48
1:		membar			#Sync
		wr			%g0, FPRS_DU, %fprs
		ba,pt			%xcc, rt_continue
		 stb			%l5, [%g6 + AOFF_task_tss + AOFF_thread_fpdepth]

#undef PTREGS_OFF

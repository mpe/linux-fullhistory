/* $Id: etrap.S,v 1.34 1997/08/08 08:33:40 jj Exp $
 * etrap.S: Preparing for entry into the kernel on Sparc V9.
 *
 * Copyright (C) 1996, 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <asm/asi.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/spitfire.h>
#include <asm/head.h>

#define		FPUREG_SZ		((64 * 4) + (2 * 8))
#define		TASK_REGOFF		((((PAGE_SIZE<<1)-FPUREG_SZ)&~(64-1)) - \
					 TRACEREG_SZ-REGWIN_SZ)

/*
 * On entry, %g7 is return address - 0x4.
 * %g4 and %g5 will be preserved %l4 and %l5 respectively.
 */

		.text
		.align			32
		.globl			etrap, etrap_irq, etraptl1

etrap:		rdpr			%pil, %g2
etrap_irq:	rdpr			%tstate, %g1
		sllx			%g2, 20, %g2
		or			%g1, %g2, %g1
		andcc			%g1, TSTATE_PRIV, %g0
		bne,pn			%xcc, etrap_maybe_fpu
		 sub			%sp, REGWIN_SZ + TRACEREG_SZ - STACK_BIAS, %g2
		sethi			%hi(TASK_REGOFF), %g2

		or			%g2, %lo(TASK_REGOFF), %g2
		add			%g6, %g2, %g2
etrap_maybe_fpu:rd			%fprs, %g3
		brnz,pn			%g3, etrap_save_fpu
		 st			%g0, [%g2 + REGWIN_SZ + PT_V9_FPRS]
etrap_after_fpu:rdpr			%tpc, %g3
		stx			%g1, [%g2 + REGWIN_SZ + PT_V9_TSTATE]
		rdpr			%tnpc, %g1

		stx			%g3, [%g2 + REGWIN_SZ + PT_V9_TPC]
		rd			%y, %g3
		stx			%g1, [%g2 + REGWIN_SZ + PT_V9_TNPC]
		st			%g3, [%g2 + REGWIN_SZ + PT_V9_Y]
		save			%g2, -STACK_BIAS, %sp	! The ordering here is
		rdpr			%pstate, %g1		! critical, see winfixup
		bne,pn			%xcc, 2f
		 rdpr			%canrestore, %g3

		rdpr			%wstate, %g2
		wrpr			%g0, 7, %cleanwin
		wrpr			%g0, 0, %canrestore
		sll			%g2, 3, %g2
		wrpr			%g3, 0, %otherwin
		wrpr			%g2, 0, %wstate
		wr			%g0, ASI_DMMU, %asi
		ldxa			[%g0 + PRIMARY_CONTEXT] %asi, %g2

		stxa			%g0, [%g0 + PRIMARY_CONTEXT] %asi
		stxa			%g2, [%g0 + SECONDARY_CONTEXT] %asi
		flush			%g6
2:		wrpr			%g0, 0x0, %tl
		or			%g1, 0, %l1
		add			%g4, 0, %l4
		or			%g5, 0, %l5
		add			%g7, 0, %l2

		or			%g6, 0, %l6
		wrpr			%l1, (PSTATE_AG|PSTATE_RMO), %pstate
		stx			%g1, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G1]
		stx			%g2, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G2]
		stx			%g3, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G3]
		stx			%g4, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G4]
		stx			%g5, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G5]
		stx			%g6, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G6]

		stx			%g7, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G7]
		stx			%i0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0]
		stx			%i1, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I1]
		stx			%i2, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I2]
		stx			%i3, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I3]
		stx			%i4, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I4]
		sethi			%uhi(PAGE_OFFSET), %g4
		stx			%i5, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I5]

		stx			%i6, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I6]
		sllx			%g4, 32, %g4
		stx			%i7, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I7]
		wrpr			%l1, (PSTATE_IE|PSTATE_AG|PSTATE_RMO), %pstate
		jmpl			%l2 + 0x4, %g0
		 mov			%l6, %g6
etrap_save_fpu:	and			%g3, FPRS_FEF, %g3
		brz,pn			%g3, 2f

		 nop
		be,a,pt			%xcc, 3f
		 add			%g2, (TRACEREG_SZ + REGWIN_SZ), %g2
		wr			%g0, ASI_BLK_P, %asi
		add			%g2, ((TRACEREG_SZ+REGWIN_SZ)-FPUREG_SZ), %g2
		andn			%g2, (64 - 1), %g2
1:		st			%g3, [%g2 - 0x4 /*REGWIN_SZ + PT_V9_FPRS*/]
		rd			%gsr, %g3

		stx			%fsr, [%g2 + 0x100]
		stx			%g3,  [%g2 + 0x108]
		membar			#StoreStore | #LoadStore
		stda			%f0,  [%g2 + 0x000] %asi
		stda			%f16, [%g2 + 0x040] %asi
		stda			%f32, [%g2 + 0x080] %asi
		stda			%f48, [%g2 + 0x0c0] %asi
		membar			#Sync

		sub			%g2, (TRACEREG_SZ + REGWIN_SZ), %g2
2:		bne,pn			%xcc, etrap_after_fpu
		 wr			%g0, 0, %fprs
		ld			[%g6 + AOFF_task_tss + AOFF_thread_ctx], %g3
		wr			%g0, ASI_DMMU, %asi
		nop
		stxa			%g3, [%g0 + SECONDARY_CONTEXT] %asi
		flush			%g2

		b,pt			%xcc, etrap_after_fpu
		 nop
3:		mov			SECONDARY_CONTEXT, %g3
		stxa			%g0, [%g3] ASI_DMMU
		flush			%g2
		wr			%g0, ASI_BLK_S, %asi
		b,pt			%xcc, 1b
		 mov			FPRS_FEF, %g3

etraptl1:	rdpr			%tstate, %g1
		sub			%sp, REGWIN_SZ + TRACEREG_SZ - STACK_BIAS, %g2
		ba,pt			%xcc, etrap_maybe_fpu
		 andcc			%g1, TSTATE_PRIV, %g0
		nop
#undef TASK_REGOFF
#undef FPUREG_SZ

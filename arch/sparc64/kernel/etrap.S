/* $Id: etrap.S,v 1.37 1997/08/21 09:13:18 davem Exp $
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
#define		FPU_OFF			(STACK_BIAS + REGWIN_SZ + TRACEREG_SZ)

/*
 * On entry, %g7 is return address - 0x4.
 * %g4 and %g5 will be preserved %l4 and %l5 respectively.
 */

		.text
		.align			32
etrap_priv:	or			%g1, %g3, %g1						! IEU0		Group
		rd			%fprs, %g3						! Single	Group+4bubbles
		sub			%sp, REGWIN_SZ + TRACEREG_SZ - STACK_BIAS, %g2		! IEU0		Group
		andcc			%g3, FPRS_FEF, %g0					! IEU1
		add			%g2, REGWIN_SZ + TRACEREG_SZ - FPUREG_SZ, %g3		! IEU0		Group
		be,pt			%icc, 1f						! CTI
		 andcc			%g1, TSTATE_PRIV, %g0					! IEU1
		andn			%g3, (64 - 1), %g3					! IEU0		Group
		ba,pt			%xcc, 1f						! CTI
		 sub			%g3, REGWIN_SZ + TRACEREG_SZ, %g2			! IEU0		Group
		
		.align			32
		.globl			etrap, etrap_irq, etraptl1

etrap:		rdpr			%pil, %g2						! Single 	Group
etrap_irq:	rdpr			%tstate, %g1						! Single 	Group
		sllx			%g2, 20, %g3						! IEU0		Group
		andcc			%g1, TSTATE_PRIV, %g0					! IEU1
		bne,pn			%xcc, etrap_priv					! CTI
		 sethi			%hi(TASK_REGOFF), %g2					! IEU0		Group
		or			%g1, %g3, %g1						! IEU1
		or			%g2, %lo(TASK_REGOFF), %g2				! IEU0		Group
		add			%g6, %g2, %g2						! IEU0		Group
1:		rdpr			%tpc, %g3						! Single	Group
		stx			%g1, [%g2 + REGWIN_SZ + PT_V9_TSTATE]			! Store		Group
		rdpr			%tnpc, %g1						! Single	Group
		stx			%g3, [%g2 + REGWIN_SZ + PT_V9_TPC]			! Store		Group
		rd			%y, %g3							! Single	Group+4bubbles
		stx			%g1, [%g2 + REGWIN_SZ + PT_V9_TNPC]			! Store		Group
		st			%g3, [%g2 + REGWIN_SZ + PT_V9_Y]			! Store		Group
		save			%g2, -STACK_BIAS, %sp	! The ordering here is		! Single	Group
		rdpr			%pstate, %g1		! critical, see winfixup	! Single	Group+9bubbles
		bne,pn			%xcc, 2f						! CTI		Group
		 sethi			%hi(TSTATE_PEF), %l2					! IEU0
		mov			PRIMARY_CONTEXT, %l4					! IEU1
		rdpr			%canrestore, %g3					! Single	Group+4bubbles
		rdpr			%wstate, %g2						! Single	Group+4bubbles
		wrpr			%g0, 7, %cleanwin					! Single	Group+4bubbles
		wrpr			%g0, 0, %canrestore					! Single	Group+4bubbles
		sll			%g2, 3, %g2						! IEU0		Group
		mov			SECONDARY_CONTEXT, %l5					! IEU1
		wrpr			%g3, 0, %otherwin					! Single	Group+4bubbles
		wrpr			%g2, 0, %wstate						! Single	Group+4bubbles
		rdpr			%tstate, %l3						! Single	Group
		ldxa			[%l4] ASI_DMMU, %g2					! Load		Group
		stxa			%g0, [%l4] ASI_DMMU					! Store		Group
		stxa			%g2, [%l5] ASI_DMMU					! Store		Group
		flush			%g6							! Single	Group+9bubbles
		andcc			%l3, %l2, %g0						! IEU1		Group
		be,a,pt			%icc, 6f						! CTI
		 st			%g0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_FPRS]	! Store
		rd			%fprs, %l0						! Single	Group+4bubbles
		andcc			%l0, FPRS_FEF, %g0					! IEU1		Group
		be,pn			%icc, 6f						! CTI
		 st			%l0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_FPRS]	! Store
		ld			[%g6 + AOFF_task_tss + AOFF_thread_flags], %l4		! Load		Group
		stx			%fsr, [%sp + FPU_OFF + 0x100] 				! Single	Group
		or			%l4, %l0, %l4						! IEU0		Group
		ba,pt			%xcc, 3f						! CTI
		 st			%l4, [%g6 + AOFF_task_tss + AOFF_thread_flags]		! Store
2:		rd			%fprs, %l0						! Single	Group+4bubbles
		andcc			%l0, FPRS_FEF, %g0					! IEU1		Group
		be,pn			%icc, 6f						! CTI
		 st			%l0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_FPRS]	! Store
		stx			%fsr, [%sp + FPU_OFF + 0x100] 				! Single	Group
3:		rd			%gsr, %l7						! Single	Group+4bubbles
		cmp			%l0, FPRS_FEF						! IEU1		Group
		be,pn			%icc, 6f						! CTI
		 stx			%l7, [%sp + FPU_OFF + 0x108]				! Store		
		wr			%g0, ASI_BLK_P, %asi					! Singe		Group+4bubbles
		andcc			%l0, FPRS_DL, %g0					! IEU1		Group
		be,pn			%icc, 4f						! CTI
		 membar			#StoreStore | #LoadStore				! Memory	
		stda			%f0, [%sp + FPU_OFF + 0x000] %asi			! Store		Group
		stda			%f16, [%sp + FPU_OFF + 0x040] %asi			! Store		Group
		andcc			%l0, FPRS_DU, %g0					! IEU1
		be,pn			%icc, 5f						! CTI
		 nop										! IEU0		Group
4:		stda			%f32, [%sp + FPU_OFF + 0x080] %asi			! Store		Group
		stda			%f48, [%sp + FPU_OFF + 0x0c0] %asi			! Store		Group
5:		membar			#Sync							! Memory	
6:		wr			%g0, 0x0, %fprs						! Single	Group+4bubbles
		wrpr			%g0, 0x0, %tl						! Single	Group+4bubbles
		mov			%g1, %l1						! IEU0		Group
		mov			%g4, %l4						! IEU1
		mov			%g5, %l5						! IEU0		Group
		mov			%g7, %l2						! IEU1
		mov			%g6, %l6						! IEU0		Group
		wrpr			%l1, (PSTATE_AG|PSTATE_RMO), %pstate			! Single	Group+4bubbles
		stx			%g1, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G1]		! Store		Group
		stx			%g2, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G2]		! Store		Group
		stx			%g3, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G3]		! Store		Group
		stx			%g4, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G4]		! Store		Group
		stx			%g5, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G5]		! Store		Group
		stx			%g6, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G6]		! Store		Group
		stx			%g7, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_G7]		! Store		Group
		stx			%i0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0]		! Store		Group
		stx			%i1, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I1]		! Store		Group
		stx			%i2, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I2]		! Store		Group
		stx			%i3, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I3]		! Store		Group
		stx			%i4, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I4]		! Store		Group
		sethi			%uhi(PAGE_OFFSET), %g4					! IEU0
		stx			%i5, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I5]		! Store		Group
		stx			%i6, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I6]		! Store		Group
		sllx			%g4, 32, %g4						! IEU0
		stx			%i7, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I7]		! Store		Group
		wrpr			%l1, (PSTATE_IE|PSTATE_AG|PSTATE_RMO), %pstate		! Single	Group+4bubbles
		jmpl			%l2 + 0x4, %g0						! CTI		Group
		 mov			%l6, %g6						! IEU0

etraptl1:	rdpr			%tstate, %g1						! Single	Group+4bubbles
		ba,pt			%xcc, etrap_priv					! CTI		Group
		 clr			%g3							! IEU0
#undef TASK_REGOFF
#undef FPUREG_SZ

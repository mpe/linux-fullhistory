/* $Id: etrap.S,v 1.40 1998/06/12 14:54:03 jj Exp $
 * etrap.S: Preparing for entry into the kernel on Sparc V9.
 *
 * Copyright (C) 1996, 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997, 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <asm/asi.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/spitfire.h>
#include <asm/head.h>

#define		TASK_REGOFF		((PAGE_SIZE<<1)-TRACEREG_SZ-REGWIN_SZ)

/*
 * On entry, %g7 is return address - 0x4.
 * %g4 and %g5 will be preserved %l4 and %l5 respectively.
 */

		.text		
		.align			32
		.globl			etrap, etrap_irq, etraptl1
		.globl			scetrap

etrap:		rdpr			%pil, %g2						! Single 	Group
etrap_irq:	rdpr			%tstate, %g1						! Single 	Group
		sllx			%g2, 20, %g3						! IEU0		Group
		andcc			%g1, TSTATE_PRIV, %g0					! IEU1
		or			%g1, %g3, %g1						! IEU0		Group
		bne,a,pn		%xcc, 1f						! CTI
		 sub			%sp, REGWIN_SZ + TRACEREG_SZ - STACK_BIAS, %g2		! IEU1
		sethi			%hi(TASK_REGOFF), %g2					! IEU0		Group		
		sethi			%hi(TSTATE_PEF), %g3					! IEU1
		or			%g2, %lo(TASK_REGOFF), %g2				! IEU0		Group
		and			%g1, %g3, %g3						! IEU1
		brnz,pn			%g3, 1f							! CTI+IEU1	Group
		 add			%g6, %g2, %g2						! IEU0
		wr			%g0, 0, %fprs						! Single	Group+4bubbles
1:		rdpr			%tpc, %g3						! Single	Group
		stx			%g1, [%g2 + REGWIN_SZ + PT_V9_TSTATE]			! Store		Group
		rdpr			%tnpc, %g1						! Single	Group
		stx			%g3, [%g2 + REGWIN_SZ + PT_V9_TPC]			! Store		Group
		rd			%y, %g3							! Single	Group+4bubbles
		stx			%g1, [%g2 + REGWIN_SZ + PT_V9_TNPC]			! Store		Group
		st			%g3, [%g2 + REGWIN_SZ + PT_V9_Y]			! Store		Group
		save			%g2, -STACK_BIAS, %sp	! The ordering here is		! Single	Group
		rdpr			%pstate, %g1		! critical, see winfixup	! Single	Group+9bubbles
		andn			%g6, 0x1f, %l6						! IEU0		Group
		bne,pn			%xcc, 3f						! CTI		
		 mov			PRIMARY_CONTEXT, %l4					! IEU1
		rdpr			%canrestore, %g3					! Single	Group+4bubbles
		rdpr			%wstate, %g2						! Single	Group+4bubbles
		wrpr			%g0, 7, %cleanwin					! Single	Group+4bubbles
		wrpr			%g0, 0, %canrestore					! Single	Group+4bubbles
		sll			%g2, 3, %g2						! IEU0		Group
		mov			1, %l5							! IEU1
		stb			%l5, [%l6 + AOFF_task_tss + AOFF_thread_fpdepth]	! Store
		wrpr			%g3, 0, %otherwin					! Single	Group+4bubbles
		wrpr			%g2, 0, %wstate						! Single	Group+4bubbles
		ldxa			[%l4] ASI_DMMU, %g2					! Load		Group
		stxa			%g0, [%l4] ASI_DMMU					! Store		Group
		stxa			%g2, [%l4 + %l4] ASI_DMMU				! Store		Group
		flush			%l6							! Single	Group+9bubbles
2:		wrpr			%g0, 0x0, %tl						! Single	Group+4bubbles
		andn			%g1, PSTATE_MM, %l1					! IEU0		Group
		mov			%g4, %l4						! IEU1
		mov			%g5, %l5						! IEU0		Group
		mov			%g7, %l2						! IEU1
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

3:		ldub			[%l6 + AOFF_task_tss + AOFF_thread_fpdepth], %l5	! Load		Group
		add			%l6, AOFF_task_tss + AOFF_thread_fpsaved + 1, %l4	! IEU0
		srl			%l5, 1, %l3						! IEU0		Group
		add			%l5, 2, %l5						! IEU1
		stb			%l5, [%l6 + AOFF_task_tss + AOFF_thread_fpdepth]	! Store
		ba,pt			%xcc, 2b						! CTI
		 stb			%g0, [%l4 + %l3]					! Store		Group

etraptl1:	rdpr			%tstate, %g1						! Single	Group+4bubbles
		sub			%sp, REGWIN_SZ + TRACEREG_SZ - STACK_BIAS, %g2		! IEU1
		ba,pt			%xcc, 1b						! CTI		Group
		 andcc			%g1, TSTATE_PRIV, %g0					! IEU0

scetrap:	rdpr			%pil, %g2						! Single 	Group
		rdpr			%tstate, %g1						! Single 	Group
		sllx			%g2, 20, %g3						! IEU0		Group
		andcc			%g1, TSTATE_PRIV, %g0					! IEU1
		or			%g1, %g3, %g1						! IEU0		Group
		bne,a,pn		%xcc, 1f						! CTI
		 sub			%sp, REGWIN_SZ + TRACEREG_SZ - STACK_BIAS, %g2		! IEU1
		sethi			%hi(TASK_REGOFF), %g2					! IEU0		Group
		sethi			%hi(TSTATE_PEF), %g3					! IEU1
		or			%g2, %lo(TASK_REGOFF), %g2				! IEU0		Group
		and			%g1, %g3, %g3						! IEU1
		brnz,pn			%g3, 1f							! CTI+IEU1	Group
		 add			%g6, %g2, %g2						! IEU0
		wr			%g0, 0, %fprs						! Single	Group+4bubbles
1:		rdpr			%tpc, %g3						! Single	Group
		stx			%g1, [%g2 + REGWIN_SZ + PT_V9_TSTATE]			! Store		Group
		rdpr			%tnpc, %g1						! Single	Group
		stx			%g3, [%g2 + REGWIN_SZ + PT_V9_TPC]			! Store		Group
		stx			%g1, [%g2 + REGWIN_SZ + PT_V9_TNPC]			! Store		Group
		st			%g0, [%g2 + REGWIN_SZ + PT_V9_Y]			! Store		Group
		save			%g2, -STACK_BIAS, %sp	! The ordering here is		! Single	Group
		rdpr			%pstate, %g1		! critical, see winfixup	! Single	Group+9bubbles
		andn			%g6, 0x1f, %l6						! IEU0		Group
		bne,pn			%xcc, 2f						! CTI		
		 mov			PRIMARY_CONTEXT, %l4					! IEU1
		rdpr			%canrestore, %g3					! Single	Group+4bubbles
		rdpr			%wstate, %g2						! Single	Group+4bubbles
		wrpr			%g0, 7, %cleanwin					! Single	Group+4bubbles
		wrpr			%g0, 0, %canrestore					! Single	Group+4bubbles
		sll			%g2, 3, %g2						! IEU0		Group
		wrpr			%g3, 0, %otherwin					! Single	Group+4bubbles
		wrpr			%g2, 0, %wstate						! Single	Group+4bubbles
		ldxa			[%l4] ASI_DMMU, %g2					! Load		Group
		stxa			%g0, [%l4] ASI_DMMU					! Store		Group
		stxa			%g2, [%l4 + %l4] ASI_DMMU				! Store		Group
		flush			%l6							! Single	Group+9bubbles
2:		wrpr			%g0, 0x0, %tl						! Single	Group+4bubbles
		andn			%g1, PSTATE_MM, %l1					! IEU0		Group
		mov			%g4, %l4						! IEU1
		mov			%g5, %l5						! IEU0		Group
		mov			%g7, %l2						! IEU1
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

#undef TASK_REGOFF

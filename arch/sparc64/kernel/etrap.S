/* $Id: etrap.S,v 1.18 1997/05/19 05:58:51 davem Exp $
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

	/* We assume that pstate, when entering this, has AG and
	 * IE bits set, MG and IG clear.
	 *
	 * We also guarentee for caller that AG %g4 and %g5 will have
	 * their values preserved and left in %l4 and %l5 respectively
	 * for him (fault handling needs this).
	 */

	.text
	.align	32
	.globl	etrap, etrap_irq
etrap:
	rdpr		%pil, %g2
etrap_irq:
	rdpr		%tstate, %g1
	sllx		%g2, 20, %g2
	or		%g1, %g2, %g1
	andcc		%g1, TSTATE_PRIV, %g0
	bne,a,pn	%xcc, 1f
	 sub		%sp, REGWIN_SZ + TRACEREG_SZ - STACK_BIAS, %g2
	rd		%pic, %g3

	sethi		%hi((PAGE_SIZE<<1)-TRACEREG_SZ-REGWIN_SZ), %g2
	or		%g2, %lo((PAGE_SIZE<<1)-TRACEREG_SZ-REGWIN_SZ), %g2
	add		%g3, %g2, %g2
1:	stx		%g1, [%g2 + REGWIN_SZ + PT_V9_TSTATE]
	rdpr		%tpc, %g1
	rdpr		%tnpc, %g3
	stx		%g1, [%g2 + REGWIN_SZ + PT_V9_TPC]
	rd		%y, %g1

	stx		%g3, [%g2 + REGWIN_SZ + PT_V9_TNPC]
	stx		%g1, [%g2 + REGWIN_SZ + PT_V9_Y]
	rdpr		%pstate, %g1
	save		%g2, -STACK_BIAS, %sp
	bne,pn		%xcc, 1f
	 rdpr		%canrestore, %g3
	rdpr		%wstate, %g6
	wrpr		%g0, 0, %canrestore

	sll		%g6, 3, %g6
	wrpr		%g3, 0, %otherwin
	wrpr		%g6, %wstate
	sethi		%uhi(KERNBASE), %g3
	sllx		%g3, 32, %g3
	mov		PRIMARY_CONTEXT, %g2
	stxa		%g0, [%g2] ASI_DMMU
	flush		%g3
1:
	wrpr		%g0, 0x0, %tl
	mov		%g1, %l1
	mov		%g4, %l4
	mov		%g5, %l5
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
	rd		%pic, %g6

	jmpl		%l2 + 0x4, %g0
	 sllx		%g4, 32, %g4

	.globl	etraptl1
etraptl1:
	rdpr	%tstate, %g1
	ba,pt	%xcc, 1b
	 sub	%sp, REGWIN_SZ + TRACEREG_SZ - STACK_BIAS, %g2
	nop
	nop
	nop

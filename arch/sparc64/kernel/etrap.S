/* $Id: etrap.S,v 1.1 1996/12/26 10:16:42 davem Exp $
 * etrap.S: Preparing for entry into the kernel on Sparc V9.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/pstate.h>
#include <asm/ptrace.h>

	.text
	.align	4
	.globl	etrap
etrap:
	sethi	%uhi(current_set), %g6
	rd	%tstate, %g1
	or	%g6, %ulo(current_set), %g6
	rd	%tpc, %g2
	sllx	%g6, 32, %g6
	rd	%tnpc, %g3
	sethi	%hi(current_set), %g4
	or	%g4, %lo(current_set), %g4
	or	%g6, %g4, %g6
	rd	%y, %g4
	ldx	[%g6 + THREAD_KSTACK], %g5
	stx	%g1, [%g5 + REGWIN_SZ + PT_TSTATE]
	stx	%g2, [%g5 + REGWIN_SZ + PT_TPC]
	stx	%g3, [%g5 + REGWIN_SZ + PT_TNPC]
	stx	%g4, [%g5 + REGWIN_SZ + PT_Y]
	rd	%pstate, %g1
	andn	%g1, (PSTATE_IG | PSTATE_MG | PSTATE_AG), %g2
	save	%g5, 0x0, %sp
	mov	%g2, %l1
	wr	%g0, 0x0, %tl
	mov	%g7, %l2
	wr	%l1, 0x0, %pstate
	stx	%g1, [%sp + REGWIN_SZ + PT_G1]
	stx	%g2, [%sp + REGWIN_SZ + PT_G2]
	stx	%g3, [%sp + REGWIN_SZ + PT_G3]
	stx	%g4, [%sp + REGWIN_SZ + PT_G4]
	stx	%g5, [%sp + REGWIN_SZ + PT_G5]
	stx	%g6, [%sp + REGWIN_SZ + PT_G6]
	stx	%g7, [%sp + REGWIN_SZ + PT_G7]
	stx	%i0, [%sp + REGWIN_SZ + PT_I0]
	stx	%i1, [%sp + REGWIN_SZ + PT_I1]
	stx	%i2, [%sp + REGWIN_SZ + PT_I2]
	stx	%i3, [%sp + REGWIN_SZ + PT_I3]
	stx	%i4, [%sp + REGWIN_SZ + PT_I4]
	stx	%i5, [%sp + REGWIN_SZ + PT_I5]
	stx	%i6, [%sp + REGWIN_SZ + PT_I6]
	stx	%i7, [%sp + REGWIN_SZ + PT_I7]
	jmpl	%l2 + 0x4, %g0
	 wr	%l1, PSTATE_IE, %pstate

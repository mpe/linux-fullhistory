/* $Id: winmacro.h,v 1.1 1996/12/26 13:25:20 davem Exp $
 * winmacro.h: Window loading-unloading macros for Sparc/V9.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_WINMACRO_H
#define _SPARC64_WINMACRO_H

#define STORE_V9_WINDOW_GENERIC_ASI(store_insn, base, offset, the_asi)	\
	store_insn	%l0, [base + offset + RW_V9_L0] the_asi;	\
	store_insn	%l1, [base + offset + RW_V9_L1] the_asi;	\
	store_insn	%l2, [base + offset + RW_V9_L2] the_asi;	\
	store_insn	%l3, [base + offset + RW_V9_L3] the_asi;	\
	store_insn	%l4, [base + offset + RW_V9_L4] the_asi;	\
	store_insn	%l5, [base + offset + RW_V9_L5] the_asi;	\
	store_insn	%l6, [base + offset + RW_V9_L6] the_asi;	\
	store_insn	%l7, [base + offset + RW_V9_L7] the_asi;	\
	store_insn	%i0, [base + offset + RW_V9_I0] the_asi;	\
	store_insn	%i1, [base + offset + RW_V9_I1] the_asi;	\
	store_insn	%i2, [base + offset + RW_V9_I2] the_asi;	\
	store_insn	%i3, [base + offset + RW_V9_I3] the_asi;	\
	store_insn	%i4, [base + offset + RW_V9_I4] the_asi;	\
	store_insn	%i5, [base + offset + RW_V9_I5] the_asi;	\
	store_insn	%i6, [base + offset + RW_V9_I6] the_asi;	\
	store_insn	%i7, [base + offset + RW_V9_I7] the_asi;

#define STORE_V9_WINDOW_KERNEL(base)		\
	stx	%l0, [base + RW_V9_L0];		\
	stx	%l1, [base + RW_V9_L1];		\
	stx	%l2, [base + RW_V9_L2];		\
	stx	%l3, [base + RW_V9_L3];		\
	stx	%l4, [base + RW_V9_L4];		\
	stx	%l5, [base + RW_V9_L5];		\
	stx	%l6, [base + RW_V9_L6];		\
	stx	%l7, [base + RW_V9_L7];		\
	stx	%i0, [base + RW_V9_I0];		\
	stx	%i1, [base + RW_V9_I1];		\
	stx	%i2, [base + RW_V9_I2];		\
	stx	%i3, [base + RW_V9_I3];		\
	stx	%i4, [base + RW_V9_I4];		\
	stx	%i5, [base + RW_V9_I5];		\
	stx	%i6, [base + RW_V9_I6];		\
	stx	%i7, [base + RW_V9_I7];

#define LOAD_V9_WINDOW_KERNEL(base)		\
	ldx	[base + RW_V9_L0], %l0;		\
	ldx	[base + RW_V9_L1], %l1;		\
	ldx	[base + RW_V9_L2], %l2;		\
	ldx	[base + RW_V9_L3], %l3;		\
	ldx	[base + RW_V9_L4], %l4;		\
	ldx	[base + RW_V9_L5], %l5;		\
	ldx	[base + RW_V9_L6], %l6;		\
	ldx	[base + RW_V9_L7], %l7;		\
	ldx	[base + RW_V9_I0], %i0;		\
	ldx	[base + RW_V9_I1], %i1;		\
	ldx	[base + RW_V9_I2], %i2;		\
	ldx	[base + RW_V9_I3], %i3;		\
	ldx	[base + RW_V9_I4], %i4;		\
	ldx	[base + RW_V9_I5], %i5;		\
	ldx	[base + RW_V9_I6], %i6;		\
	ldx	[base + RW_V9_I7], %i7;

#define STORE_V9_WINDOW_ASI_REG(base)		\

#endif /* !(_SPARC64_WINMACRO_H) */

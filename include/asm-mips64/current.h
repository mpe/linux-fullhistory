/* $Id: current.h,v 1.4 2000/01/17 23:32:47 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 1999 Ralf Baechle
 */
#ifndef _ASM_CURRENT_H
#define _ASM_CURRENT_H

#ifdef _LANGUAGE_C

/* MIPS rules... */
register struct task_struct *current asm("$28");

#endif /* _LANGUAGE_C */
#ifdef _LANGUAGE_ASSEMBLY

/*
 * Special variant for use by exception handlers when the stack pointer
 * is not loaded.
 */
#define _GET_CURRENT(reg)			\
	lui	reg, %hi(kernelsp);		\
	.set	push;				\
	.set	noreorder;			\
	ld	reg, %lo(kernelsp)(reg);	\
	.set	pop;				\
	ori	reg, 0x3fff;			\
	xori	reg, 0x3fff

#endif

#endif /* _ASM_CURRENT_H */

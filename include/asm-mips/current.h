/* $Id: current.h,v 1.4 1998/07/20 17:52:19 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Ralf Baechle
 */
#ifndef __ASM_MIPS_CURRENT_H
#define __ASM_MIPS_CURRENT_H

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
	lw	reg, %lo(kernelsp)(reg);	\
	.set	pop;				\
	ori	reg, 8191;			\
	xori	reg, 8191

#endif

#endif /* __ASM_MIPS_CURRENT_H */

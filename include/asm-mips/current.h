#ifndef __ASM_MIPS_CURRENT_H
#define __ASM_MIPS_CURRENT_H

#ifdef __LANGUAGE_C__

/* MIPS rules... */
register struct task_struct *current asm("$28");

#endif /* __LANGUAGE_C__ */
#ifdef __LANGUAGE_ASSEMBLY__

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

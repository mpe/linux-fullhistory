#ifndef __ASM_MIPS_CURRENT_H
#define __ASM_MIPS_CURRENT_H

#ifdef __LANGUAGE_C__

static inline struct task_struct *__get_current(void)
{
	struct task_struct *__current;

	__asm__("ori\t%0,$29,%1\n\t"
	        "xori\t%0,%1"
		 :"=r" (__current)
	         :"ir" (8191UL));

	return __current;
}

#define current __get_current()

#endif /* __LANGUAGE_C__ */
#ifdef __LANGUAGE_ASSEMBLY__

/*
 * Get current task pointer
 */
#define GET_CURRENT(reg)			\
	lui	reg, %hi(kernelsp);		\
	lw	reg, %lo(kernelsp)(reg);	\
	ori	reg, 8191;			\
	xori	reg, 8191

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

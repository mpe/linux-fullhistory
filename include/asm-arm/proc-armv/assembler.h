/*
 * linux/asm-arm/proc-armv/assembler.h
 *
 * Copyright (C) 1996-2000 Russell King
 *
 * This file contains ARM processor specifics for
 * the ARM6 and better processors.
 */
#define MODE_USR	USR_MODE
#define MODE_FIQ	FIQ_MODE
#define MODE_IRQ	IRQ_MODE
#define MODE_SVC	SVC_MODE

#define DEFAULT_FIQ	MODE_FIQ

/*
 * LOADREGS - ldm with PC in register list (eg, ldmfd sp!, {pc})
 */
#ifdef __STDC__
#define LOADREGS(cond, base, reglist...)\
	ldm##cond	base,reglist
#else
#define LOADREGS(cond, base, reglist...)\
	ldm/**/cond	base,reglist
#endif

/*
 * Build a return instruction for this processor type.
 */
#define RETINSTR(instr, regs...)\
	instr	regs

/*
 * Save the current IRQ state and disable IRQs.  Note that this macro
 * assumes FIQs are enabled, and that the processor is in SVC mode.
 */
	.macro	save_and_disable_irqs, oldcpsr, temp
	mrs	\oldcpsr, cpsr
	mov	\temp, #I_BIT | MODE_SVC
	msr	cpsr_c, \temp
	.endm

/*
 * Restore interrupt state previously stored in a register.  We don't
 * guarantee that this will preserve the flags.
 */
	.macro	restore_irqs, oldcpsr
	msr	cpsr_c, \oldcpsr
	.endm

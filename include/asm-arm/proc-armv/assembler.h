/*
 * linux/asm-arm/proc-armv/assembler.h
 *
 * Copyright (C) 1996 Russell King
 *
 * This file contains arm architecture specific defines
 * for the different processors
 */
#ifndef __ASSEMBLY__
#error "Only include this from assembly code"
#endif

#define MODE_USR	USR_MODE
#define MODE_FIQ	FIQ_MODE
#define MODE_IRQ	IRQ_MODE
#define MODE_SVC	SVC_MODE

#define DEFAULT_FIQ	MODE_FIQ

/*
 * LOADREGS - ldm with PC in register list (eg, ldmfd sp!, {pc})
 * RETINSTR - return instruction (eg, mov pc, lr)
 */
#ifdef __STDC__
#define LOADREGS(cond, base, reglist...)\
	ldm##cond	base,reglist

#define RETINSTR(instr, regs...)\
	instr	regs
#else
#define LOADREGS(cond, base, reglist...)\
	ldm/**/cond	base,reglist

#define RETINSTR(instr, regs...)\
	instr		regs
#endif

/*
 * No nop required after mode change
 */
#define MODENOP

/*
 * Change to `mode'
 */
#define MODE(savereg,tmpreg,mode) \
	mrs	savereg, cpsr; \
	bic	tmpreg, savereg, $0x1f; \
	orr	tmpreg, tmpreg, $mode; \
	msr	cpsr, tmpreg

/*
 * Restore mode
 */
#define RESTOREMODE(savereg) \
	msr	cpsr, savereg

/*
 * save interrupt state (uses stack)
 */	
#define SAVEIRQS(tmpreg)\
	mrs	tmpreg, cpsr; \
	str	tmpreg, [sp, $-4]!

/*
 * restore interrupt state (uses stack)
 */
#define RESTOREIRQS(tmpreg)\
	ldr	tmpreg, [sp], $4; \
	msr	cpsr, tmpreg

/*
 * disable IRQs
 */
#define DISABLEIRQS(tmpreg)\
	mrs	tmpreg , cpsr; \
	orr	tmpreg , tmpreg , $I_BIT; \
	msr	cpsr, tmpreg

/*
 * enable IRQs
 */
#define ENABLEIRQS(tmpreg)\
	mrs	tmpreg , cpsr; \
	bic	tmpreg , tmpreg , $I_BIT; \
	msr	cpsr, tmpreg

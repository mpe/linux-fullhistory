/*
 * linux/asm-arm/proc-armv/assembler.h
 *
 * Copyright (C) 1996 Russell King
 *
 * This file contains arm architecture specific defines
 * for the different processors
 */

/*
 * LOADREGS: multiple register load (ldm) with pc in register list
 *		(takes account of ARM6 not using ^)
 *
 * RETINSTR: return instruction: adds the 's' in at the end of the
 *		instruction if this is not an ARM6
 *
 * SAVEIRQS: save IRQ state (not required on ARM2/ARM3 - done
 *		implicitly
 *
 * RESTOREIRQS: restore IRQ state (not required on ARM2/ARM3 - done
 *		implicitly with ldm ... ^ or movs.
 *
 * These next two need thinking about - can't easily use stack... (see system.S)
 * DISABLEIRQS: disable IRQS in SVC mode
 *
 * ENABLEIRQS: enable IRQS in SVC mode
 *
 * USERMODE: switch to USER mode
 *
 * SVCMODE: switch to SVC mode
 */

#define N_BIT	(1 << 31)
#define Z_BIT	(1 << 30)
#define C_BIT	(1 << 29)
#define V_BIT	(1 << 28)

#define PCMASK	0

#ifdef __ASSEMBLER__

#define I_BIT	(1 << 7)
#define F_BIT	(1 << 6)

#define MODE_FIQ26	0x01
#define MODE_FIQ32	0x11

#define DEFAULT_FIQ	MODE_FIQ32

#define LOADREGS(cond, base, reglist...)\
	ldm##cond	base,reglist

#define RETINSTR(instr, regs...)\
	instr	regs

#define MODENOP

#define MODE(savereg,tmpreg,mode) \
	mrs	savereg, cpsr; \
	bic	tmpreg, savereg, $0x1f; \
	orr	tmpreg, tmpreg, $mode; \
	msr	cpsr, tmpreg

#define RESTOREMODE(savereg) \
	msr	cpsr, savereg
	
#define SAVEIRQS(tmpreg)\
	mrs	tmpreg, cpsr; \
	str	tmpreg, [sp, $-4]!

#define RESTOREIRQS(tmpreg)\
	ldr	tmpreg, [sp], $4; \
	msr	cpsr, tmpreg

#define DISABLEIRQS(tmpreg)\
	mrs	tmpreg , cpsr; \
	orr	tmpreg , tmpreg , $I_BIT; \
	msr	cpsr, tmpreg

#define ENABLEIRQS(tmpreg)\
	mrs	tmpreg , cpsr; \
	bic	tmpreg , tmpreg , $I_BIT; \
	msr	cpsr, tmpreg
#endif

/*
 * linux/asm-arm/proc-armo/assembler.h
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

#define PCMASK	0xfc000003

#ifdef __ASSEMBLER__

#define I_BIT	(1 << 27)
#define F_BIT	(1 << 26)

#define MODE_USR	0
#define MODE_FIQ	1
#define MODE_IRQ	2
#define MODE_SVC	3

#define DEFAULT_FIQ	MODE_FIQ

#define LOADREGS(cond, base, reglist...)\
	ldm##cond	base,reglist^

#define RETINSTR(instr, regs...)\
	instr##s	regs

#define MODENOP\
	mov	r0, r0

#define MODE(savereg,tmpreg,mode) \
	mov	savereg, pc; \
	bic	tmpreg, savereg, $0x0c000003; \
	orr	tmpreg, tmpreg, $mode; \
	teqp	tmpreg, $0

#define RESTOREMODE(savereg) \
	teqp	savereg, $0

#define SAVEIRQS(tmpreg)

#define RESTOREIRQS(tmpreg)

#define DISABLEIRQS(tmpreg)\
	teqp	pc, $0x08000003

#define ENABLEIRQS(tmpreg)\
	teqp	pc, $0x00000003

#define USERMODE(tmpreg)\
	teqp	pc, $0x00000000;\
	mov	r0, r0

#define SVCMODE(tmpreg)\
	teqp	pc, $0x00000003;\
	mov	r0, r0

#endif

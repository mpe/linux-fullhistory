/*
 * linux/arch/arm/lib/floppydma.S
 *
 * Copyright (C) 1995, 1996 Russell King
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
		.text

		.global	SYMBOL_NAME(floppy_fiqin_end)
ENTRY(floppy_fiqin_start)
		subs	r9, r9, #1
		ldrgtb	r12, [r11, #-4]
		ldrleb	r12, [r11], #0
		strb	r12, [r10], #1
		subs	pc, lr, #4
SYMBOL_NAME(floppy_fiqin_end):

		.global	SYMBOL_NAME(floppy_fiqout_end)
ENTRY(floppy_fiqout_start)
		subs	r9, r9, #1
		ldrgeb	r12, [r10], #1
		movlt	r12, #0
		strleb	r12, [r11], #0
		subles	pc, lr, #4
		strb	r12, [r11, #-4]
		subs	pc, lr, #4
SYMBOL_NAME(floppy_fiqout_end):

@ Params:
@ r0 = length
@ r1 = address
@ r2 = floppy port
@ Puts these into R9_fiq, R10_fiq, R11_fiq
ENTRY(floppy_fiqsetup)
		mov	ip, sp
		stmfd	sp!, {fp, ip, lr, pc}
		sub	fp, ip, #4
		MODE(r3,ip,I_BIT|F_BIT|DEFAULT_FIQ)	@ disable FIQs, IRQs, FIQ mode
		mov	r0, r0
		mov	r9, r0
		mov	r10, r1
		mov	r11, r2
		RESTOREMODE(r3)				@ back to normal
		mov	r0, r0
		LOADREGS(ea,fp,{fp, sp, pc})

ENTRY(floppy_fiqresidual)
		mov	ip, sp
		stmfd	sp!, {fp, ip, lr, pc}
		sub	fp, ip, #4
		MODE(r3,ip,I_BIT|F_BIT|DEFAULT_FIQ)	@ disable FIQs, IRQs, FIQ mode
		mov	r0, r0
		mov	r0, r9
		RESTOREMODE(r3)
		mov	r0, r0
		LOADREGS(ea,fp,{fp, sp, pc})

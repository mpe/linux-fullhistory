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

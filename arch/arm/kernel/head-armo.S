/*
 * linux/arch/arm/kernel/head.S
 *
 * Copyright (C) 1994, 1995, 1996, 1997 Russell King
 *
 * 26-bit kernel startup code
 */
#include <linux/linkage.h>

		.text
		.align
/*
 * Entry point.
 */
ENTRY(stext)
ENTRY(_stext)
__entry:	cmp	pc, #0x02000000
		ldrlt	pc, LC1			@ if 0x01800000, call at 0x02080000
		teq	r0, #0			@ Check for old calling method
		blne	Loldparams		@ Move page if old
		adr	r5, LC0
		ldmia	r5, {r5, r6, sl, sp}	@ Setup stack
		mov	r4, #0
1:		cmp	r5, sl			@ Clear BSS
		strcc	r4, [r5], #4
		bcc	1b
		mov	r0, #0xea000000		@ Point undef instr to continuation
		adr	r5, Lcontinue - 12
		orr	r5, r0, r5, lsr #2
		str	r5, [r4, #4]
		mov	r2, r4
		ldr	r5, Larm2_id
		swp	r0, r0, [r2]		@ check for swp (ARM2 can't)
		ldr	r5, Larm250_id
		mrc	15, 0, r0, c0, c0	@ check for CP#15 (ARM250 can't)
		mov	r5, r0			@ Use processor ID if we do have CP#15
Lcontinue:	str	r5, [r6]
		mov	r5, #0xeb000000		@ Point undef instr vector to itself
		sub	r5, r5, #2
		str	r5, [r4, #4]
		mov	fp, #0
		b	SYMBOL_NAME(start_kernel)

LC1:		.word	SYMBOL_NAME(_stext)
LC0:		.word	SYMBOL_NAME(__bss_start)
		.word	SYMBOL_NAME(processor_id)
		.word	SYMBOL_NAME(_end)
		.word	SYMBOL_NAME(init_task_union)+8192
Larm2_id:	.long	0x41560200
Larm250_id:	.long	0x41560250
		.align

Loldparams:	mov	r4, #0x02000000
		add	r3, r4, #0x00080000
		add	r4, r4, #0x0007c000
1:		ldmia	r0!, {r5 - r12}
		stmia	r4!, {r5 - r12}
		cmp	r4, r3
		blt	1b
		movs	pc, lr

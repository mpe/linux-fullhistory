/*
 * linux/arch/arm/lib/memzero.S
 *
 * Copyright (C) 1995-1999 Russell King
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include "constants.h"

		.text

/*
 * Prototype: void memzero(void *d, size_t n)
 */
1:		@ 4 <= r1
		cmp	ip, #2				@	1
		strltb	r2, [r0], #1			@	1
		strleb	r2, [r0], #1			@	1
		strb	r2, [r0], #1			@	1
		rsb	ip, ip, #4			@	1
		sub	r1, r1, ip			@	1
		cmp	r1, #3				@	1
		bgt	2f				@	1	@ +8
		b	4f				@	1	@ +9

		.align	5

ENTRY(__memzero)
		mov	r2, #0				@	1
		cmp	r1, #4				@	1
		blt	4f				@	1	@ = 3

		@ r1 >= 4

		ands	ip, r0, #3			@	1
		bne	1b				@	1	@ = 5

2:		@ r1 >= 4 && (r0 & 3) = 0				@ = 5 or 11

		str	lr, [sp, #-4]!			@	1
		mov	r3, #0				@	1
		mov	ip, #0				@	1
		mov	lr, #0				@	1

		@ 4 <= r1 <= 32						@ = 9 or 15

3:		subs	r1, r1, #32			@	1
		stmgeia	r0!, {r2, r3, ip, lr}		@	4
		stmgeia	r0!, {r2, r3, ip, lr}		@	4
		bgt	3b				@	1
		LOADREGS(eqfd, sp!, {pc})		@	1/2

		@ -28 <= r1 <= -1

		cmp	r1, #-16			@	1
		stmgeia	r0!, {r2, r3, ip, lr}		@	4
		ldr	lr, [sp], #4			@	1
		addlts	r1, r1, #16			@	1
		RETINSTR(moveq,pc,lr)			@	1

		@ -12 <= r1 <= -1

		cmp	r1, #-8				@	1
		stmgeia	r0!, {r2, r3}			@	2
		addlts	r1, r1, #8			@	1
		RETINSTR(moveq,pc,lr)			@	1

		@ -4 <= r1 <= -1

		cmp	r1, #-4				@	1
		strge	r2, [r0], #4			@	1
		adds	r1, r1, #4			@	1
		RETINSTR(moveq,pc,lr)			@	1

4:		@ 1 <= r1 <= 3
		cmp	r1, #2				@	1
		strgtb	r2, [r0], #1			@	1
		strgeb	r2, [r0], #1			@	1
		strb	r2, [r0], #1			@	1
		RETINSTR(mov,pc,lr)			@	1

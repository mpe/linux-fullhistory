/*
 * linux/arch/arm/lib/memset.S
 *
 * Copyright (C) 1995-1999 Russell King
 *
 * ASM optimised string functions
 *
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include "constants.h"

		.text
		.align	5
ENTRY(memset)
		mov	r3, r0
		cmp	r2, #16
		blt	6f
		ands	ip, r3, #3
		beq	1f
		cmp	ip, #2
		strltb	r1, [r3], #1			@ Align destination
		strleb	r1, [r3], #1
		strb	r1, [r3], #1
		rsb	ip, ip, #4
		sub	r2, r2, ip
1:		orr	r1, r1, r1, lsl #8
		orr	r1, r1, r1, lsl #16
		cmp	r2, #256
		blt	4f
		stmfd	sp!, {r4, r5, lr}
		mov	r4, r1
		mov	r5, r1
		mov	lr, r1
		mov	ip, r2, lsr #6
		sub	r2, r2, ip, lsl #6
2:		stmia	r3!, {r1, r4, r5, lr}		@ 64 bytes at a time.
		stmia	r3!, {r1, r4, r5, lr}
		stmia	r3!, {r1, r4, r5, lr}
		stmia	r3!, {r1, r4, r5, lr}
		subs	ip, ip, #1
		bne	2b
		teq	r2, #0
		LOADREGS(eqfd, sp!, {r4, r5, pc})	@ Now <64 bytes to go.
		tst	r2, #32
		stmneia	r3!, {r1, r4, r5, lr}
		stmneia	r3!, {r1, r4, r5, lr}
		tst	r2, #16
		stmneia	r3!, {r1, r4, r5, lr}
		ldmia	sp!, {r4, r5}
3:		tst	r2, #8
		stmneia	r3!, {r1, lr}
		tst	r2, #4
		strne	r1, [r3], #4
		tst	r2, #2
		strneb	r1, [r3], #1
		strneb	r1, [r3], #1
		tst	r2, #1
		strneb	r1, [r3], #1
		LOADREGS(fd, sp!, {pc})

4:		movs	ip, r2, lsr #3
		beq	3b
		sub	r2, r2, ip, lsl #3
		str	lr, [sp, #-4]!		
		mov	lr, r1
		subs	ip, ip, #4
5:		stmgeia	r3!, {r1, lr}
		stmgeia	r3!, {r1, lr}
		stmgeia	r3!, {r1, lr}
		stmgeia	r3!, {r1, lr}
		subges	ip, ip, #4
		bge	5b
		tst	ip, #2
		stmneia	r3!, {r1, lr}
		stmneia	r3!, {r1, lr}
		tst	ip, #1
		stmneia	r3!, {r1, lr}
		teq	r2, #0
		LOADREGS(eqfd, sp!, {pc})
		b	3b

6:		subs	r2, r2, #1
		strgeb	r1, [r3], #1
		bgt	6b
		RETINSTR(mov, pc, lr)



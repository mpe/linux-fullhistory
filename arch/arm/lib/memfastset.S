/*
 * linux/arch/arm/lib/memfastset.S
 *
 * Copyright (C) 1995, 1996 Russell King
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
		.text
@ Prototype: void memsetl (unsigned long *d, unsigned long c, size_t n);

ENTRY(memsetl)
		stmfd	sp!, {lr}
		cmp	r2, #16
		blt	5f
		mov	r3, r1
		mov	ip, r1
		mov	lr, r1
		subs	r2, r2, #32
		bmi	2f
1:		stmia	r0!, {r1, r3, ip, lr}
		stmia	r0!, {r1, r3, ip, lr}
		LOADREGS(eqfd, sp!, {pc})
		subs	r2, r2, #32
		bpl	1b
2:		adds	r2, r2, #16
		bmi	4f
3:		stmia	r0!, {r1, r3, ip, lr}
		LOADREGS(eqfd, sp!, {pc})
		subs	r2, r2, #16
		bpl	3b
4:		add	r2, r2, #16
5:		subs	r2, r2, #4
		strge	r1, [r0], #4
		bgt	5b 
		LOADREGS(fd, sp!, {pc})

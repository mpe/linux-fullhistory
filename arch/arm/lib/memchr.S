/*
 * linux/arch/arm/lib/memchr.S
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
ENTRY(memchr)
		str	lr, [sp, #-4]!
1:		ldrb	r3, [r0], #1
		teq	r3, r1
		beq	2f
		subs	r2, r2, #1
		bpl	1b
2:		movne	r0, #0
		subeq	r0, r0, #1
		LOADREGS(fd, sp!, {pc})


/*
 * linux/arch/arm/lib/strrchr.S
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
ENTRY(strrchr)
		str	lr, [sp, #-4]!
		mov	r3, #0
1:		ldrb	r2, [r0], #1
		teq	r2, r1
		moveq	r3, r0
		teq	r2, #0
		bne	1b
		mov	r0, r3
		LOADREGS(fd, sp!, {pc})



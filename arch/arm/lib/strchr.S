/*
 * linux/arch/arm/lib/strchr.S
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
ENTRY(strchr)
		str	lr, [sp, #-4]!
		mov	r3, #0
1:		ldrb	r2, [r0], #1
		teq	r2, r1
		teqne	r2, #0
		bne	1b
		teq	r2, #0
		moveq	r0, #0
		subne	r0, r0, #1
		LOADREGS(fd, sp!, {pc})



/*
 * linux/arch/arm/lib/testchangebit.S
 *
 * Copyright (C) 1995-1996 Russell King
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
                .text

ENTRY(test_and_change_bit)
		add	r1, r1, r0, lsr #3
		and	r3, r0, #7
		mov	r0, #1
		SAVEIRQS(ip)
		DISABLEIRQS(ip)
		ldrb	r2, [r1]
		tst	r2, r0, lsl r3
		eor	r2, r2, r0, lsl r3
		moveq	r0, #0
		strb	r2, [r1]
		RESTOREIRQS(ip)
		RETINSTR(mov,pc,lr)



/*
 * linux/arch/arm/lib/changebit.S
 *
 * Copyright (C) 1995-1996 Russell King
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
                .text

/* Purpose  : Function to change a bit
 * Prototype: int change_bit(int bit,int *addr)
 */
ENTRY(change_bit)
                and     r2, r0, #7
                mov     r3, #1
                mov     r3, r3, lsl r2
		SAVEIRQS(ip)
		DISABLEIRQS(ip)
		ldrb	r2, [r1, r0, lsr #3]
		eor	r2, r2, r3
		strb	r2, [r1, r0, lsr #3]
		RESTOREIRQS(ip)
		RETINSTR(mov,pc,lr)



/*
 * linux/arch/arm/lib/bitops.S
 *
 * Copyright (C) 1995-1996 Russell King
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
                .text

@ Purpose  : Find a 'zero' bit
@ Prototype: int find_first_zero_bit(char *addr,int maxbit);

ENTRY(find_first_zero_bit)
		mov	r2, #0			@ Initialise bit position
Lfindzbit1lp:	ldrb	r3, [r0, r2, lsr #3]	@ Check byte, if 0xFF, then all bits set
		teq	r3, #0xFF
		bne	Lfoundzbit
		add	r2, r2, #8
		cmp	r2, r1			@ Check to see if we have come to the end
		bcc	Lfindzbit1lp
		add	r0, r1, #1		@ Make sure that we flag an error
		RETINSTR(mov,pc,lr)
Lfoundzbit:	tst	r3, #1			@ Check individual bits
		moveq	r0, r2
		RETINSTR(moveq,pc,lr)
		tst	r3, #2
		addeq	r0, r2, #1
		RETINSTR(moveq,pc,lr)
		tst	r3, #4
		addeq	r0, r2, #2
		RETINSTR(moveq,pc,lr)
		tst	r3, #8
		addeq	r0, r2, #3
		RETINSTR(moveq,pc,lr)
		tst	r3, #16
		addeq	r0, r2, #4
		RETINSTR(moveq,pc,lr)
		tst	r3, #32
		addeq	r0, r2, #5
		RETINSTR(moveq,pc,lr)
		tst	r3, #64
		addeq	r0, r2, #6
		RETINSTR(moveq,pc,lr)
		add	r0, r2, #7
		RETINSTR(mov,pc,lr)

@ Purpose  : Find next 'zero' bit
@ Prototype: int find_next_zero_bit(char *addr,int maxbit,int offset)

ENTRY(find_next_zero_bit)
		tst	r2, #7
		beq	Lfindzbit1lp		@ If new byte, goto old routine
		ldrb	r3, [r0, r2, lsr#3]
		orr	r3, r3, #0xFF00		@ Set top bits so we wont get confused
		str	r4, [sp, #-4]!
		and	r4, r2, #7
		mov	r3, r3, lsr r4		@ Shift right by no. of bits
		ldr	r4, [sp], #4
		and	r3, r3, #0xFF
		teq	r3, #0xFF
		orreq	r2, r2, #7
		addeq	r2, r2, #1
		beq	Lfindzbit1lp		@ If all bits are set, goto old routine
		b	Lfoundzbit

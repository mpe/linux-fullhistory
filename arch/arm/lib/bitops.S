/*
 * linux/arch/arm/lib/bitops.S
 *
 * Copyright (C) 1995, 1996 Russell King
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
                .text

@ Purpose  : Function to set a bit
@ Prototype: int set_bit(int bit,int *addr)

ENTRY(set_bit)
                and     r2, r0, #7
                mov     r3, #1
                mov     r3, r3, lsl r2
		SAVEIRQS(ip)
		DISABLEIRQS(ip)
		ldrb	r2, [r1, r0, lsr #3]
		orr	r2, r2, r3
		strb	r2, [r1, r0, lsr #3]
		RESTOREIRQS(ip)
		RETINSTR(mov,pc,lr)

ENTRY(test_and_set_bit)
		add	r1, r1, r0, lsr #3	@ Get byte offset
		and	r3, r0, #7		@ Get bit offset
		mov	r0, #1
		SAVEIRQS(ip)
		DISABLEIRQS(ip)
		ldrb	r2, [r1]
		tst	r2, r0, lsl r3
		orr	r2, r2, r0, lsl r3
		moveq	r0, #0
		strb	r2, [r1]
		RESTOREIRQS(ip)
		RETINSTR(mov,pc,lr)

@ Purpose  : Function to clear a bit
@ Prototype: int clear_bit(int bit,int *addr)

ENTRY(clear_bit)
		and	r2, r0, #7
		mov	r3, #1
		mov	r3, r3, lsl r2
		SAVEIRQS(ip)
		DISABLEIRQS(ip)
		ldrb	r2, [r1, r0, lsr #3]
		bic	r2, r2, r3
		strb	r2, [r1, r0, lsr #3]
		RESTOREIRQS(ip)
		RETINSTR(mov,pc,lr)

ENTRY(test_and_clear_bit)
		add	r1, r1, r0, lsr #3	@ Get byte offset
		and	r3, r0, #7	@ Get bit offset
		mov	r0, #1
		SAVEIRQS(ip)
		DISABLEIRQS(ip)
		ldrb	r2, [r1]
		tst	r2, r0, lsl r3
		bic	r2, r2, r0, lsl r3
		moveq	r0, #0
		strb	r2, [r1]
		RESTOREIRQS(ip)
		RETINSTR(mov,pc,lr)

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
		stmfd	sp!, {r4}
		and	r4, r2, #7
		mov	r3, r3, lsr r4		@ Shift right by no. of bits
		ldmfd	sp!, {r4}
		and	r3, r3, #0xFF
		teq	r3, #0xFF
		orreq	r2, r2, #7
		addeq	r2, r2, #1
		beq	Lfindzbit1lp		@ If all bits are set, goto old routine
		b	Lfoundzbit

/*
 * linux/arch/arm/lib/io-ebsa.S
 *
 * Copyright (C) 1995, 1996 Russell King
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
		.text
		.align

#define OUT(reg)					\
		mov	r8, reg, lsl $16		;\
		orr	r8, r8, r8, lsr $16		;\
		str	r8, [r3, r0, lsl $2]		;\
		mov	r8, reg, lsr $16		;\
		orr	r8, r8, r8, lsl $16		;\
		str	r8, [r3, r0, lsl $2]

#define IN(reg)						\
		ldr	reg, [r0]			;\
		and	reg, reg, ip			;\
		ldr	lr, [r0]			;\
		orr	reg, reg, lr, lsl $16

@ Purpose: read a block of data from a hardware register to memory.
@ Proto  : insw(int from_port, void *to, int len_in_words);
@ Proto  : inswb(int from_port, void *to, int len_in_bytes);
@ Notes  : increment to

ENTRY(insw)
		mov	r2, r2, lsl#1
ENTRY(inswb)
		mov	ip, sp
		stmfd	sp!, {r4 - r10 ,fp ,ip ,lr ,pc}
		sub	fp, ip, #4
		cmp	r0, #0x00c00000
		movge	r3, #0
		movlt	r3, #0xf0000000
		add	r0, r3, r0, lsl #2
		tst	r1, #3
		beq	Linswok
		tst	r1, #1
		bne	Linsw_notaligned
		cmp	r2, #1
		ldrge	r4, [r0]
		strgeb	r4, [r1], #1
		movgt	r4, r4, LSR#8
		strgtb	r4, [r1], #1
		ldmleea	fp, {r4 - r10, fp, sp, pc}^
		sub	r2, r2, #2
Linswok:	mov	ip, #0xFF
		orr	ip, ip, ip, lsl #8
Linswlp:	subs	r2, r2, #64
		bmi	Linsw_toosmall
		IN(r3)
		IN(r4)
		IN(r5)
		IN(r6)
		IN(r7)
		IN(r8)
		IN(r9)
		IN(r10)
		stmia	r1!, {r3 - r10}
		IN(r3)
		IN(r4)
		IN(r5)
		IN(r6)
		IN(r7)
		IN(r8)
		IN(r9)
		IN(r10)
		stmia	r1!, {r3 - r10}
		bne	Linswlp
		LOADREGS(ea, fp, {r4 - r10, fp, sp, pc})
Linsw_toosmall:
		add	r2, r2, #32
		bmi	Linsw_toosmall2
Linsw2lp:	IN(r3)
		IN(r4)
		IN(r5)
		IN(r6)
		IN(r7)
		IN(r8)
		IN(r9)
		IN(r10)
		stmia	r1!, {r3 - r10}
		LOADREGS(eqea, fp, {r4 - r10, fp, sp, pc})
		b	Linsw_notaligned
Linsw_toosmall2:
		add	r2, r2, #32
Linsw_notaligned:
		cmp	r2, #1
		LOADREGS(ltea, fp, {r4 - r10, fp, sp, pc})
		ldr	r4, [r0]
		strb	r4, [r1], #1
		movgt	r4, r4, LSR#8
		strgtb	r4, [r1], #1
		subs	r2, r2, #2
		bgt	Linsw_notaligned
		LOADREGS(ea, fp, {r4 - r10, fp, sp, pc})

@ Purpose: write a block of data from memory to a hardware register.
@ Proto  : outsw(int to_reg, void *from, int len_in_words);
@ Proto  : outswb(int to_reg, void *from, int len_in_bytes);
@ Notes  : increments from

ENTRY(outsw)
		mov	r2, r2, LSL#1
ENTRY(outswb)
		mov	ip, sp
		stmfd	sp!, {r4 - r8, fp, ip, lr, pc}
		sub	fp, ip, #4
		cmp	r0, #0x00c00000
		movge	r3, #0
		movlt	r3, #0xf0000000
		tst	r1, #2
		beq	Loutsw32lp
		ldr	r4, [r1], #2
		mov	r4, r4, lsl #16
		orr	r4, r4, r4, lsr #16
		str	r4, [r3, r0, lsl #2]
		sub	r2, r2, #2
		teq	r2, #0
		LOADREGS(eqea, fp, {r4 - r8, fp, sp, pc})
Loutsw32lp:	subs	r2,r2,#32
		blt	Loutsw_toosmall
		ldmia	r1!,{r4,r5,r6,r7}
		OUT(r4)
		OUT(r5)
		OUT(r6)
		OUT(r7)
		ldmia	r1!,{r4,r5,r6,r7}
		OUT(r4)
		OUT(r5)
		OUT(r6)
		OUT(r7)
		LOADREGS(eqea, fp, {r4 - r8, fp, sp, pc})
		b	Loutsw32lp
Loutsw_toosmall:
		adds	r2,r2,#32
		LOADREGS(eqea, fp, {r4 - r8, fp, sp, pc})
Llpx:		ldr	r4,[r1],#2
		mov	r4,r4,LSL#16
		orr	r4,r4,r4,LSR#16
		str	r4,[r3,r0,LSL#2]
		subs	r2,r2,#2
		bgt	Llpx
		LOADREGS(ea, fp, {r4 - r8, fp, sp, pc})


/*
 * linux/arch/arm/lib/string.S
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

/*
 * Prototype: void memzero(void *d, size_t n)
 */
1:		@ 4 <= r1
		cmp	ip, #2				@	1
		strltb	r2, [r0], #1			@	1
		strleb	r2, [r0], #1			@	1
		strb	r2, [r0], #1			@	1
		rsb	ip, ip, #4			@	1
		sub	r1, r1, ip			@	1
		cmp	r1, #3				@	1
		bgt	2f				@	1	@ +8
		b	4f				@	1	@ +9

		.align	5

ENTRY(__memzero)
		mov	r2, #0				@	1
		cmp	r1, #4				@	1
		blt	4f				@	1	@ = 3

		@ r1 >= 4

		ands	ip, r0, #3			@	1
		bne	1b				@	1	@ = 5

2:		@ r1 >= 4 && (r0 & 3) = 0				@ = 5 or 11

		str	lr, [sp, #-4]!			@	1
		mov	r3, #0				@	1
		mov	ip, #0				@	1
		mov	lr, #0				@	1

		@ 4 <= r1 <= 32						@ = 9 or 15

3:		subs	r1, r1, #32			@	1
		stmgeia	r0!, {r2, r3, ip, lr}		@	4
		stmgeia	r0!, {r2, r3, ip, lr}		@	4
		bgt	3b				@	1
		LOADREGS(eqfd, sp!, {pc})		@	1/2

		@ -28 <= r1 <= -1

		cmp	r1, #-16			@	1
		stmgeia	r0!, {r2, r3, ip, lr}		@	4
		ldr	lr, [sp], #4			@	1
		addlts	r1, r1, #16			@	1
		RETINSTR(moveq,pc,lr)			@	1

		@ -12 <= r1 <= -1

		cmp	r1, #-8				@	1
		stmgeia	r0!, {r2, r3}			@	2
		addlts	r1, r1, #8			@	1
		RETINSTR(moveq,pc,lr)			@	1

		@ -4 <= r1 <= -1

		cmp	r1, #-4				@	1
		strge	r2, [r0], #4			@	1
		adds	r1, r1, #4			@	1
		RETINSTR(moveq,pc,lr)			@	1

4:		@ 1 <= r1 <= 3
		cmp	r1, #2				@	1
		strgtb	r2, [r0], #1			@	1
		strgeb	r2, [r0], #1			@	1
		strb	r2, [r0], #1			@	1
		RETINSTR(mov,pc,lr)			@	1

/*
 * StrongARM optimised copy_page routine
 * now 1.72bytes/cycle, was 1.60 bytes/cycle
 * (50MHz bus -> 86MB/s)
 */

ENTRY(copy_page)
		stmfd	sp!, {r4, lr}			@	2
		mov	r2, #PAGE_SZ/64			@	1
1:		ldmia	r1!, {r3, r4, ip, lr}		@	4
		subs	r2, r2, #1			@	1
		stmia	r0!, {r3, r4, ip, lr}		@	4
		ldmia	r1!, {r3, r4, ip, lr}		@	4+1
		stmia	r0!, {r3, r4, ip, lr}		@	4
		ldmia	r1!, {r3, r4, ip, lr}		@	4+1
		stmia	r0!, {r3, r4, ip, lr}		@	4
		ldmia	r1!, {r3, r4, ip, lr}		@	4+1
		stmia	r0!, {r3, r4, ip, lr}		@	4
		bne	1b				@	1
		LOADREGS(fd, sp!, {r4, pc})		@	3

		.align	5
ENTRY(memset)		/* needed for some versions of gcc */
ENTRY(__memset)
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
		stmfd	sp!, {lr}
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

ENTRY(strrchr)
		stmfd	sp!, {lr}
		mov	r3, #0
1:		ldrb	r2, [r0], #1
		teq	r2, r1
		moveq	r3, r0
		teq	r2, #0
		bne	1b
		mov	r0, r3
		LOADREGS(fd, sp!, {pc})

ENTRY(strchr)
		stmfd	sp!,{lr}
		mov	r3, #0
1:		ldrb	r2, [r0], #1
		teq	r2, r1
		teqne	r2, #0
		bne	1b
		teq	r2, #0
		moveq	r0, #0
		subne	r0, r0, #1
		LOADREGS(fd, sp!, {pc})

ENTRY(memchr)
		stmfd	sp!, {lr}
1:		ldrb	r3, [r0], #1
		teq	r3, r1
		beq	2f
		subs	r2, r2, #1
		bpl	1b
2:		movne	r0, #0
		subeq	r0, r0, #1
		LOADREGS(fd, sp!, {pc})


#define ENTER	\
		mov	ip,sp	;\
		stmfd	sp!,{r4-r9,fp,ip,lr,pc}	;\
		sub	fp,ip,#4

#define EXIT	\
		LOADREGS(ea, fp, {r4 - r9, fp, sp, pc})

#define EXITEQ	\
		LOADREGS(eqea, fp, {r4 - r9, fp, sp, pc})

/*
 * Prototype: void memcpy(void *to,const void *from,unsigned long n);
 * ARM3: cant use memcopy here!!!
 */
ENTRY(memcpy)
ENTRY(memmove)
		ENTER
		cmp	r1, r0
		bcc	19f
		subs	r2, r2, #4
		blt	6f
		ands	ip, r0, #3
		bne	7f
		ands	ip, r1, #3
		bne	8f

1:		subs	r2, r2, #8
		blt	5f
		subs	r2, r2, #0x14
		blt	3f
2:		ldmia	r1!,{r3 - r9, ip}
		stmia	r0!,{r3 - r9, ip}
		subs	r2, r2, #32
		bge	2b
		cmn	r2, #16
		ldmgeia	r1!, {r3 - r6}
		stmgeia	r0!, {r3 - r6}
		subge	r2, r2, #0x10
3:		adds	r2, r2, #0x14
4:		ldmgeia	r1!, {r3 - r5}
		stmgeia	r0!, {r3 - r5}
		subges	r2, r2, #12
		bge	4b
5:		adds	r2, r2, #8
		blt	6f
		subs	r2, r2, #4
		ldrlt	r3, [r1], #4
		strlt	r3, [r0], #4
		ldmgeia	r1!, {r3, r4}
		stmgeia	r0!, {r3, r4}
		subge	r2, r2, #4

6:		adds	r2, r2, #4
		EXITEQ
		cmp	r2, #2
		ldrb	r3, [r1], #1
		strb	r3, [r0], #1
		ldrgeb	r3, [r1], #1
		strgeb	r3, [r0], #1
		ldrgtb	r3, [r1], #1
		strgtb	r3, [r0], #1
		EXIT

7:		rsb	ip, ip, #4
		cmp	ip, #2
		ldrb	r3, [r1], #1
		strb	r3, [r0], #1
		ldrgeb	r3, [r1], #1
		strgeb	r3, [r0], #1
		ldrgtb	r3, [r1], #1
		strgtb	r3, [r0], #1
		subs	r2, r2, ip
		blt	6b
		ands	ip, r1, #3
		beq	1b

8:		bic	r1, r1, #3
		ldr	r7, [r1], #4
		cmp	ip, #2
		bgt	15f
		beq	11f
		cmp	r2, #12
		blt	10f
		sub	r2, r2, #12
9:		mov	r3, r7, lsr #8
		ldmia	r1!, {r4 - r7}
		orr	r3, r3, r4, lsl #24
		mov	r4, r4, lsr #8
		orr	r4, r4, r5, lsl #24
		mov	r5, r5, lsr #8
		orr	r5, r5, r6, lsl #24
		mov	r6, r6, lsr #8
		orr	r6, r6, r7, lsl #24
		stmia	r0!, {r3 - r6}
		subs	r2, r2, #16
		bge	9b
		adds	r2, r2, #12
		blt	100f
10:		mov	r3, r7, lsr #8
		ldr	r7, [r1], #4
		orr	r3, r3, r7, lsl #24
		str	r3, [r0], #4
		subs	r2, r2, #4
		bge	10b
100:		sub	r1, r1, #3
		b	6b

11:		cmp	r2, #12
		blt	13f		/* */
		sub	r2, r2, #12
12:		mov	r3, r7, lsr #16
		ldmia	r1!, {r4 - r7}
		orr	r3, r3, r4, lsl #16
		mov	r4, r4, lsr #16
		orr	r4, r4, r5, lsl #16
		mov	r5, r5, lsr #16
		orr	r5, r5, r6, lsl #16
		mov	r6, r6, lsr #16
		orr	r6, r6, r7,LSL#16
		stmia	r0!, {r3 - r6}
		subs	r2, r2, #16
		bge	12b
		adds	r2, r2, #12
		blt	14f
13:		mov	r3, r7, lsr #16
		ldr	r7, [r1], #4
		orr	r3, r3, r7, lsl #16
		str	r3, [r0], #4
		subs	r2, r2, #4
		bge	13b
14:		sub	r1, r1, #2
		b	6b

15:		cmp	r2, #12
		blt	17f
		sub	r2, r2, #12
16:		mov	r3, r7, lsr #24
		ldmia	r1!,{r4 - r7}
		orr	r3, r3, r4, lsl #8
		mov	r4, r4, lsr #24
		orr	r4, r4, r5, lsl #8
		mov	r5, r5, lsr #24
		orr	r5, r5, r6, lsl #8
		mov	r6, r6, lsr #24
		orr	r6, r6, r7, lsl #8
		stmia	r0!, {r3 - r6}
		subs	r2, r2, #16
		bge	16b
		adds	r2, r2, #12
		blt	18f
17:		mov	r3, r7, lsr #24
		ldr	r7, [r1], #4
		orr	r3, r3, r7, lsl#8
		str	r3, [r0], #4
		subs	r2, r2, #4
		bge	17b
18:		sub	r1, r1, #1
		b	6b


19:		add	r1, r1, r2
		add	r0, r0, r2
		subs	r2, r2, #4
		blt	24f
		ands	ip, r0, #3
		bne	25f
		ands	ip, r1, #3
		bne	26f

20:		subs	r2, r2, #8
		blt	23f
		subs	r2, r2, #0x14
		blt	22f
21:		ldmdb	r1!, {r3 - r9, ip}
		stmdb	r0!, {r3 - r9, ip}
		subs	r2, r2, #32
		bge	21b
22:		cmn	r2, #16
		ldmgedb	r1!, {r3 - r6}
		stmgedb	r0!, {r3 - r6}
		subge	r2, r2, #16
		adds	r2, r2, #20
		ldmgedb	r1!, {r3 - r5}
		stmgedb	r0!, {r3 - r5}
		subge	r2, r2, #12
23:		adds	r2, r2, #8
		blt	24f
		subs	r2, r2, #4
		ldrlt	r3, [r1, #-4]!
		strlt	r3, [r0, #-4]!
		ldmgedb	r1!, {r3, r4}
		stmgedb	r0!, {r3, r4}
		subge	r2, r2, #4

24:		adds	r2, r2, #4
		EXITEQ
		cmp	r2, #2
		ldrb	r3, [r1, #-1]!
		strb	r3, [r0, #-1]!
		ldrgeb	r3, [r1, #-1]!
		strgeb	r3, [r0, #-1]!
		ldrgtb	r3, [r1, #-1]!
		strgtb	r3, [r0, #-1]!
		EXIT

25:		cmp	ip, #2
		ldrb	r3, [r1, #-1]!
		strb	r3, [r0, #-1]!
		ldrgeb	r3, [r1, #-1]!
		strgeb	r3, [r0, #-1]!
		ldrgtb	r3, [r1, #-1]!
		strgtb	r3, [r0, #-1]!
		subs	r2, r2, ip
		blt	24b
		ands	ip, r1, #3
		beq	20b

26:		bic	r1, r1, #3
		ldr	r3, [r1], #0
		cmp	ip, #2
		blt	34f
		beq	30f
		cmp	r2, #12
		blt	28f
		sub	r2, r2, #12
27:		mov	r7, r3, lsl #8
		ldmdb	r1!, {r3, r4, r5, r6}
		orr	r7, r7, r6, lsr #24
		mov	r6, r6, lsl #8
		orr	r6, r6, r5, lsr #24
		mov	r5, r5, lsl #8
		orr	r5, r5, r4, lsr #24
		mov	r4, r4, lsl #8
		orr	r4, r4, r3, lsr #24
		stmdb	r0!, {r4, r5, r6, r7}
		subs	r2, r2, #16
		bge	27b
		adds	r2, r2, #12
		blt	29f
28:		mov	ip, r3, lsl #8
		ldr	r3, [r1, #-4]!
		orr	ip, ip, r3, lsr #24
		str	ip, [r0, #-4]!
		subs	r2, r2, #4
		bge	28b
29:		add	r1, r1, #3
		b	24b

30:		cmp	r2, #12
		blt	32f
		sub	r2, r2, #12
31:		mov	r7, r3, lsl #16
		ldmdb	r1!, {r3, r4, r5, r6}
		orr	r7, r7, r6, lsr #16
		mov	r6, r6, lsl #16
		orr	r6, r6, r5, lsr #16
		mov	r5, r5, lsl #16
		orr	r5, r5, r4, lsr #16
		mov	r4, r4, lsl #16
		orr	r4, r4, r3, lsr #16
		stmdb	r0!, {r4, r5, r6, r7}
		subs	r2, r2, #16
		bge	31b
		adds	r2, r2, #12
		blt	33f
32:		mov	ip, r3, lsl #16
		ldr	r3, [r1, #-4]!
		orr	ip, ip, r3, lsr #16
		str	ip, [r0, #-4]!
		subs	r2, r2, #4
		bge	32b
33:		add	r1, r1, #2
		b	24b

34:		cmp	r2, #12
		blt	36f
		sub	r2, r2, #12
35:		mov	r7, r3, lsl #24
		ldmdb	r1!, {r3, r4, r5, r6}
		orr	r7, r7, r6, lsr #8
		mov	r6, r6, lsl #24
		orr	r6, r6, r5, lsr #8
		mov	r5, r5, lsl #24
		orr	r5, r5, r4, lsr #8
		mov	r4, r4, lsl #24
		orr	r4, r4, r3, lsr #8
		stmdb	r0!, {r4, r5, r6, r7}
		subs	r2, r2, #16
		bge	35b
		adds	r2, r2, #12
		blt	37f
36:		mov	ip, r3, lsl #24
		ldr	r3, [r1, #-4]!
		orr	ip, ip, r3, lsr #8
		str	ip, [r0, #-4]!
		subs	r2, r2, #4
		bge	36b
37:		add	r1, r1, #1
		b	24b

		.align



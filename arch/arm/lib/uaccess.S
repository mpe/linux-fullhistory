/*
 * linux/arch/arm/lib/uaccess.S
 *
 * Copyright (C) 1995, 1996,1997,1998 Russell King
 *
 * Routines to block copy data to/from user memory
 *  These are highly optimised both for the 4k page size
 *  and for various alignments.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/errno.h>

		.text
#ifdef ENTRY
#define USER(x...)					\
9999:		x;					\
		.section __ex_table,"a";		\
		.align	3;				\
		.long	9999b,9001f;			\
		.previous
#else
#define USER(x...)					\
		x
#define ENTRY(x...)					\
		.globl	_##x;				\
_##x:
#define TESTING
#endif

#define PAGE_SHIFT 12

/* Prototype: int __arch_copy_to_user(void *to, const char *from, size_t n)
 * Purpose  : copy a block to user memory from kernel memory
 * Params   : to   - user memory
 *          : from - kernel memory
 *          : n    - number of bytes to copy
 * Returns  : Number of bytes NOT copied.
 */

.c2u_dest_not_aligned:
		rsb	ip, ip, #4
		cmp	ip, #2
		ldrb	r3, [r1], #1
USER(		strbt	r3, [r0], #1)			// May fault
		ldrgeb	r3, [r1], #1
USER(		strgebt	r3, [r0], #1)			// May fault
		ldrgtb	r3, [r1], #1
USER(		strgtbt	r3, [r0], #1)			// May fault
		sub	r2, r2, ip
		b	.c2u_dest_aligned

ENTRY(__arch_copy_to_user)
		stmfd	sp!, {r2, r4 - r7, lr}
		cmp	r2, #4
		blt	.c2u_not_enough
		ands	ip, r0, #3
		bne	.c2u_dest_not_aligned
.c2u_dest_aligned:

		ands	ip, r1, #3
		bne	.c2u_src_not_aligned
/*
 * Seeing as there has to be at least 8 bytes to copy, we can
 * copy one word, and force a user-mode page fault...
 */

.c2u_0fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.c2u_0nowords
		ldr	r3, [r1], #4
USER(		strt	r3, [r0], #4)			// May fault
		mov	ip, r0, lsl #32 - PAGE_SHIFT	// On each page, use a ld/st??t instruction
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.c2u_0fupi
/*
 * ip = max no. of bytes to copy before needing another "strt" insn
 */
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #32
		blt	.c2u_0rem8lp

.c2u_0cpy8lp:	ldmia	r1!, {r3 - r6}
		stmia	r0!, {r3 - r6}			// Shouldn't fault
		ldmia	r1!, {r3 - r6}
		stmia	r0!, {r3 - r6}			// Shouldn't fault
		subs	ip, ip, #32
		bpl	.c2u_0cpy8lp
.c2u_0rem8lp:	cmn	ip, #16
		ldmgeia	r1!, {r3 - r6}
		stmgeia	r0!, {r3 - r6}			// Shouldn't fault
		tst	ip, #8
		ldmneia	r1!, {r3 - r4}
		stmneia	r0!, {r3 - r4}			// Shouldn't fault
		tst	ip, #4
		ldrne	r3, [r1], #4
		strnet	r3, [r0], #4			// Shouldn't fault
		ands	ip, ip, #3
		beq	.c2u_0fupi
.c2u_0nowords:	teq	ip, #0
		beq	.c2u_finished
.c2u_nowords:	cmp	ip, #2
		ldrb	r3, [r1], #1
USER(		strbt	r3, [r0], #1)			// May fault
		ldrgeb	r3, [r1], #1
USER(		strgebt	r3, [r0], #1)			// May fault
		ldrgtb	r3, [r1], #1
USER(		strgtbt	r3, [r0], #1)			// May fault
		b	.c2u_finished

.c2u_not_enough:
		movs	ip, r2
		bne	.c2u_nowords
.c2u_finished:	mov	r0, #0
		LOADREGS(fd,sp!,{r2, r4 - r7, pc})

.c2u_src_not_aligned:
		bic	r1, r1, #3
		ldr	r7, [r1], #4
		cmp	ip, #2
		bgt	.c2u_3fupi
		beq	.c2u_2fupi
.c2u_1fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.c2u_1nowords
		mov	r3, r7, lsr #8
		ldr	r7, [r1], #4
		orr	r3, r3, r7, lsl #24
USER(		strt	r3, [r0], #4)			// May fault
		mov	ip, r0, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.c2u_1fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.c2u_1rem8lp

.c2u_1cpy8lp:	mov	r3, r7, lsr #8
		ldmia	r1!, {r4 - r7}
		orr	r3, r3, r4, lsl #24
		mov	r4, r4, lsr #8
		orr	r4, r4, r5, lsl #24
		mov	r5, r5, lsr #8
		orr	r5, r5, r6, lsl #24
		mov	r6, r6, lsr #8
		orr	r6, r6, r7, lsl #24
		stmia	r0!, {r3 - r6}			// Shouldn't fault
		subs	ip, ip, #16
		bpl	.c2u_1cpy8lp
.c2u_1rem8lp:	tst	ip, #8
		movne	r3, r7, lsr #8
		ldmneia	r1!, {r4, r7}
		orrne	r3, r3, r4, lsl #24
		movne	r4, r4, lsr #8
		orrne	r4, r4, r7, lsl #24
		stmneia	r0!, {r3 - r4}			// Shouldn't fault
		tst	ip, #4
		movne	r3, r7, lsr #8
		ldrne	r7, [r1], #4
		orrne	r3, r3, r7, lsl #24
		strnet	r3, [r0], #4			// Shouldn't fault
		ands	ip, ip, #3
		beq	.c2u_1fupi
.c2u_1nowords:	mov	r3, r7, lsr #8
		teq	ip, #0
		beq	.c2u_finished
		cmp	ip, #2
USER(		strbt	r3, [r0], #1)			// May fault
		movge	r3, r3, lsr #8
USER(		strgebt	r3, [r0], #1)			// May fault
		movgt	r3, r3, lsr #8
USER(		strgtbt	r3, [r0], #1)			// May fault
		b	.c2u_finished

.c2u_2fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.c2u_2nowords
		mov	r3, r7, lsr #16
		ldr	r7, [r1], #4
		orr	r3, r3, r7, lsl #16
USER(		strt	r3, [r0], #4)			// May fault
		mov	ip, r0, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.c2u_2fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.c2u_2rem8lp

.c2u_2cpy8lp:	mov	r3, r7, lsr #16
		ldmia	r1!, {r4 - r7}
		orr	r3, r3, r4, lsl #16
		mov	r4, r4, lsr #16
		orr	r4, r4, r5, lsl #16
		mov	r5, r5, lsr #16
		orr	r5, r5, r6, lsl #16
		mov	r6, r6, lsr #16
		orr	r6, r6, r7, lsl #16
		stmia	r0!, {r3 - r6}			// Shouldn't fault
		subs	ip, ip, #16
		bpl	.c2u_2cpy8lp
.c2u_2rem8lp:	tst	ip, #8
		movne	r3, r7, lsr #16
		ldmneia	r1!, {r4, r7}
		orrne	r3, r3, r4, lsl #16
		movne	r4, r4, lsr #16
		orrne	r4, r4, r7, lsl #16
		stmneia	r0!, {r3 - r4}			// Shouldn't fault
		tst	ip, #4
		movne	r3, r7, lsr #16
		ldrne	r7, [r1], #4
		orrne	r3, r3, r7, lsl #16
		strnet	r3, [r0], #4			// Shouldn't fault
		ands	ip, ip, #3
		beq	.c2u_2fupi
.c2u_2nowords:	mov	r3, r7, lsr #16
		teq	ip, #0
		beq	.c2u_finished
		cmp	ip, #2
USER(		strbt	r3, [r0], #1)			// May fault
		movge	r3, r3, lsr #8
USER(		strgebt	r3, [r0], #1)			// May fault
		ldrgtb	r3, [r1], #0
USER(		strgtbt	r3, [r0], #1)			// May fault
		b	.c2u_finished

.c2u_3fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.c2u_3nowords
		mov	r3, r7, lsr #24
		ldr	r7, [r1], #4
		orr	r3, r3, r7, lsl #8
USER(		strt	r3, [r0], #4)			// May fault
		mov	ip, r0, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.c2u_3fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.c2u_3rem8lp

.c2u_3cpy8lp:	mov	r3, r7, lsr #24
		ldmia	r1!, {r4 - r7}
		orr	r3, r3, r4, lsl #8
		mov	r4, r4, lsr #24
		orr	r4, r4, r5, lsl #8
		mov	r5, r5, lsr #24
		orr	r5, r5, r6, lsl #8
		mov	r6, r6, lsr #24
		orr	r6, r6, r7, lsl #8
		stmia	r0!, {r3 - r6}			// Shouldn't fault
		subs	ip, ip, #16
		bpl	.c2u_3cpy8lp
.c2u_3rem8lp:	tst	ip, #8
		movne	r3, r7, lsr #24
		ldmneia	r1!, {r4, r7}
		orrne	r3, r3, r4, lsl #8
		movne	r4, r4, lsr #24
		orrne	r4, r4, r7, lsl #8
		stmneia	r0!, {r3 - r4}			// Shouldn't fault
		tst	ip, #4
		movne	r3, r7, lsr #24
		ldrne	r7, [r1], #4
		orrne	r3, r3, r7, lsl #8
		strnet	r3, [r0], #4			// Shouldn't fault
		ands	ip, ip, #3
		beq	.c2u_3fupi
.c2u_3nowords:	mov	r3, r7, lsr #24
		teq	ip, #0
		beq	.c2u_finished
		cmp	ip, #2
USER(		strbt	r3, [r0], #1)			// May fault
		ldrge	r3, [r1], #0
USER(		strgebt	r3, [r0], #1)			// May fault
		movgt	r3, r3, lsr #8
USER(		strgtbt	r3, [r0], #1)			// May fault
		b	.c2u_finished

#ifndef TESTING
		.section .fixup,"ax"
		.align	0
9001:		LOADREGS(fd,sp!, {r0, r4 - r7, pc})
		.previous
#endif

/* Prototype: unsigned long __arch_copy_from_user(void *to,const void *from,unsigned long n);
 * Purpose  : copy a block from user memory to kernel memory
 * Params   : to   - kernel memory
 *          : from - user memory
 *          : n    - number of bytes to copy
 * Returns  : Number of bytes NOT copied.
 */
.cfu_dest_not_aligned:
		rsb	ip, ip, #4
		cmp	ip, #2
USER(		ldrbt	r3, [r1], #1)			// May fault
		strb	r3, [r0], #1
USER(		ldrgebt	r3, [r1], #1)			// May fault
		strgeb	r3, [r0], #1
USER(		ldrgtbt	r3, [r1], #1)			// May fault
		strgtb	r3, [r0], #1
		sub	r2, r2, ip
		b	.cfu_dest_aligned

ENTRY(__arch_copy_from_user)
		stmfd	sp!, {r0, r2, r4 - r7, lr}
		cmp	r2, #4
		blt	.cfu_not_enough
		ands	ip, r0, #3
		bne	.cfu_dest_not_aligned
.cfu_dest_aligned:
		ands	ip, r1, #3
		bne	.cfu_src_not_aligned
/*
 * Seeing as there has to be at least 8 bytes to copy, we can
 * copy one word, and force a user-mode page fault...
 */

.cfu_0fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.cfu_0nowords
USER(		ldrt	r3, [r1], #4)
		str	r3, [r0], #4
		mov	ip, r1, lsl #32 - PAGE_SHIFT	// On each page, use a ld/st??t instruction
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.cfu_0fupi
/*
 * ip = max no. of bytes to copy before needing another "strt" insn
 */
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #32
		blt	.cfu_0rem8lp

.cfu_0cpy8lp:	ldmia	r1!, {r3 - r6}			// Shouldn't fault
		stmia	r0!, {r3 - r6}
		ldmia	r1!, {r3 - r6}			// Shouldn't fault
		stmia	r0!, {r3 - r6}
		subs	ip, ip, #32
		bpl	.cfu_0cpy8lp
.cfu_0rem8lp:	cmn	ip, #16
		ldmgeia	r1!, {r3 - r6}			// Shouldn't fault
		stmgeia	r0!, {r3 - r6}
		tst	ip, #8
		ldmneia	r1!, {r3 - r4}			// Shouldn't fault
		stmneia	r0!, {r3 - r4}
		tst	ip, #4
		ldrnet	r3, [r1], #4			// Shouldn't fault
		strne	r3, [r0], #4
		ands	ip, ip, #3
		beq	.cfu_0fupi
.cfu_0nowords:	teq	ip, #0
		beq	.cfu_finished
.cfu_nowords:	cmp	ip, #2
USER(		ldrbt	r3, [r1], #1)			// May fault
		strb	r3, [r0], #1
USER(		ldrgebt	r3, [r1], #1)			// May fault
		strgeb	r3, [r0], #1
USER(		ldrgtbt	r3, [r1], #1)			// May fault
		strgtb	r3, [r0], #1
		b	.cfu_finished

.cfu_not_enough:
		movs	ip, r2
		bne	.cfu_nowords
.cfu_finished:	mov	r0, #0
		add	sp, sp, #8
		LOADREGS(fd,sp!,{r4 - r7, pc})

.cfu_src_not_aligned:
		bic	r1, r1, #3
USER(		ldrt	r7, [r1], #4)			// May fault
		cmp	ip, #2
		bgt	.cfu_3fupi
		beq	.cfu_2fupi
.cfu_1fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.cfu_1nowords
		mov	r3, r7, lsr #8
USER(		ldrt	r7, [r1], #4)			// May fault
		orr	r3, r3, r7, lsl #24
		str	r3, [r0], #4
		mov	ip, r1, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.cfu_1fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.cfu_1rem8lp

.cfu_1cpy8lp:	mov	r3, r7, lsr #8
		ldmia	r1!, {r4 - r7}			// Shouldn't fault
		orr	r3, r3, r4, lsl #24
		mov	r4, r4, lsr #8
		orr	r4, r4, r5, lsl #24
		mov	r5, r5, lsr #8
		orr	r5, r5, r6, lsl #24
		mov	r6, r6, lsr #8
		orr	r6, r6, r7, lsl #24
		stmia	r0!, {r3 - r6}
		subs	ip, ip, #16
		bpl	.cfu_1cpy8lp
.cfu_1rem8lp:	tst	ip, #8
		movne	r3, r7, lsr #8
		ldmneia	r1!, {r4, r7}			// Shouldn't fault
		orrne	r3, r3, r4, lsl #24
		movne	r4, r4, lsr #8
		orrne	r4, r4, r7, lsl #24
		stmneia	r0!, {r3 - r4}
		tst	ip, #4
		movne	r3, r7, lsr #8
USER(		ldrnet	r7, [r1], #4)			// May fault
		orrne	r3, r3, r7, lsl #24
		strne	r3, [r0], #4
		ands	ip, ip, #3
		beq	.cfu_1fupi
.cfu_1nowords:	mov	r3, r7, lsr #8
		teq	ip, #0
		beq	.cfu_finished
		cmp	ip, #2
		strb	r3, [r0], #1
		movge	r3, r3, lsr #8
		strgeb	r3, [r0], #1
		movgt	r3, r3, lsr #8
		strgtb	r3, [r0], #1
		b	.cfu_finished

.cfu_2fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.cfu_2nowords
		mov	r3, r7, lsr #16
USER(		ldrt	r7, [r1], #4)			// May fault
		orr	r3, r3, r7, lsl #16
		str	r3, [r0], #4
		mov	ip, r1, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.cfu_2fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.cfu_2rem8lp

.cfu_2cpy8lp:	mov	r3, r7, lsr #16
		ldmia	r1!, {r4 - r7}			// Shouldn't fault
		orr	r3, r3, r4, lsl #16
		mov	r4, r4, lsr #16
		orr	r4, r4, r5, lsl #16
		mov	r5, r5, lsr #16
		orr	r5, r5, r6, lsl #16
		mov	r6, r6, lsr #16
		orr	r6, r6, r7, lsl #16
		stmia	r0!, {r3 - r6}
		subs	ip, ip, #16
		bpl	.cfu_2cpy8lp
.cfu_2rem8lp:	tst	ip, #8
		movne	r3, r7, lsr #16
		ldmneia	r1!, {r4, r7}			// Shouldn't fault
		orrne	r3, r3, r4, lsl #16
		movne	r4, r4, lsr #16
		orrne	r4, r4, r7, lsl #16
		stmneia	r0!, {r3 - r4}
		tst	ip, #4
		movne	r3, r7, lsr #16
USER(		ldrnet	r7, [r1], #4)			// May fault
		orrne	r3, r3, r7, lsl #16
		strne	r3, [r0], #4
		ands	ip, ip, #3
		beq	.cfu_2fupi
.cfu_2nowords:	mov	r3, r7, lsr #16
		teq	ip, #0
		beq	.cfu_finished
		cmp	ip, #2
		strb	r3, [r0], #1
		movge	r3, r3, lsr #8
		strgeb	r3, [r0], #1
USER(		ldrgtbt	r3, [r1], #0)			// May fault
		strgtb	r3, [r0], #1
		b	.cfu_finished

.cfu_3fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.cfu_3nowords
		mov	r3, r7, lsr #24
USER(		ldrt	r7, [r1], #4)			// May fault
		orr	r3, r3, r7, lsl #8
		str	r3, [r0], #4
		mov	ip, r1, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.cfu_3fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.cfu_3rem8lp

.cfu_3cpy8lp:	mov	r3, r7, lsr #24
		ldmia	r1!, {r4 - r7}			// Shouldn't fault
		orr	r3, r3, r4, lsl #8
		mov	r4, r4, lsr #24
		orr	r4, r4, r5, lsl #8
		mov	r5, r5, lsr #24
		orr	r5, r5, r6, lsl #8
		mov	r6, r6, lsr #24
		orr	r6, r6, r7, lsl #8
		stmia	r0!, {r3 - r6}
		subs	ip, ip, #16
		bpl	.cfu_3cpy8lp
.cfu_3rem8lp:	tst	ip, #8
		movne	r3, r7, lsr #24
		ldmneia	r1!, {r4, r7}			// Shouldn't fault
		orrne	r3, r3, r4, lsl #8
		movne	r4, r4, lsr #24
		orrne	r4, r4, r7, lsl #8
		stmneia	r0!, {r3 - r4}
		tst	ip, #4
		movne	r3, r7, lsr #24
USER(		ldrnet	r7, [r1], #4)			// May fault
		orrne	r3, r3, r7, lsl #8
		strne	r3, [r0], #4
		ands	ip, ip, #3
		beq	.cfu_3fupi
.cfu_3nowords:	mov	r3, r7, lsr #24
		teq	ip, #0
		beq	.cfu_finished
		cmp	ip, #2
		strb	r3, [r0], #1
USER(		ldrget	r3, [r1], #0)			// May fault
		strgeb	r3, [r0], #1
		movgt	r3, r3, lsr #8
		strgtb	r3, [r0], #1
		b	.cfu_finished

#ifndef TESTING
		.section .fixup,"ax"
		.align	0
		/* We took an exception.  Zero out the buffer and pretend no
		   data was copied.  */
9001:		ldr	r0, [sp], #4
		ldr	r1, [sp]
		bl	SYMBOL_NAME(memzero)
		LOADREGS(fd,sp!, {r0, r4 - r7, pc})
		.previous
#endif

/* Prototype: int __arch_clear_user(void *addr, size_t sz)
 * Purpose  : clear some user memory
 * Params   : addr - user memory address to clear
 *          : sz   - number of bytes to clear
 * Returns  : number of bytes NOT cleared
 */
ENTRY(__arch_clear_user)
		stmfd	sp!, {r1, lr}
		mov	r2, #0
		cmp	r1, #4
		blt	2f
		ands	ip, r0, #3
		beq	1f
		cmp	ip, #2
USER(		strbt	r2, [r0], #1)
USER(		strlebt	r2, [r0], #1)
USER(		strltbt	r2, [r0], #1)
		rsb	ip, ip, #4
		sub	r1, r1, ip		@  7  6  5  4  3  2  1
1:		subs	r1, r1, #8		@ -1 -2 -3 -4 -5 -6 -7
USER(		strplt	r2, [r0], #4)
USER(		strplt	r2, [r0], #4)
		bpl	1b
		adds	r1, r1, #4		@  3  2  1  0 -1 -2 -3
USER(		strplt	r2, [r0], #4)
2:		tst	r1, #2			@ 1x 1x 0x 0x 1x 1x 0x
USER(		strnebt	r2, [r0], #1)
USER(		strnebt	r2, [r0], #1)
		tst	r1, #1			@ x1 x0 x1 x0 x1 x0 x1
USER(		strnebt	r2, [r0], #1)
		mov	r0, #0
		LOADREGS(fd,sp!, {r1, pc})

#ifndef TESTING
		.section .fixup,"ax"
		.align	0
9001:		LOADREGS(fd,sp!, {r0, pc})
		.previous

/* Prototype: int __arch_strlen_user(char *str)
 * Purpose  : get length of a string in user memory
 * Params   : str - address of string in user memory
 * Returns  : length of string *including terminator*, or zero on error
 */
ENTRY(__arch_strlen_user)
		stmfd	sp!, {lr}
		mov	r2, r0
1:
USER(		ldrbt	r1, [r0], #1)
		teq	r1, #0
		bne	1b
		sub	r0, r0, r2
		LOADREGS(fd,sp!, {pc})

		.section .fixup,"ax"
		.align	0
9001:		mov	r0, #0
		LOADREGS(fd,sp!,{pc})
		.previous

/* Prototype: size_t __arch_strncpy_from_user(char *dst, char *src, size_t len)
 * Purpose  : copy a string from user memory to kernel memory
 * Params   : dst - kernel memory destination
 *          : src - user memory source
 *          : len - maximum length of string
 * Returns  : number of characters copied
 */
ENTRY(__arch_strncpy_from_user)
		stmfd	sp!, {lr}
		add	ip, r1, #1
1:		subs	r2, r2, #1
		bmi	2f
USER(		ldrbt	r3, [r1], #1)
		strb	r3, [r0], #1
		teq	r3, #0
		bne	1b
		sub	r0, r1, ip
		LOADREGS(fd, sp!, {pc})
2:		sub	ip, ip, #1
		sub	r0, r1, ip
		LOADREGS(fd, sp!, {pc})

		.section .fixup,"ax"
		.align	0
9001:		mov	ip, #0
1:		strb	ip, [r0], #1
		subs	r2, r2, #1
		bpl	1b
		mov	r0, #-EFAULT
		LOADREGS(fd, sp!, {pc})
		.previous

		.align
#endif


/*
 * linux/arch/arm/lib/string.S
 *
 * Copyright (C) 1995-1998 Russell King
 *
 * This is commonly used to clear the frame buffer and the frame
 * backing buffer.  As such, it will be rarely called with r2 < 32.
 *
 * Optimisations by Matthew Wilcox
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
		.text
# Prototype: char *strrchr(const char *s,char c);

/*
 * Prototype: void memzero(void *d, size_t n)
 */
ENTRY(memzero)
		mov	r2, r1
		mov	r1, #0
/*
 * Prototype: void memsetl(unsigned long *d, unsigned long c, size_t n)
 */
ENTRY(memsetl)
		teq	r2, #0
		RETINSTR(moveq,pc,lr)
		stmfd	sp!, {lr}
		mov	lr, r1
		mov	r3, r1
		mov	ip, r1

		@ r2 = {32 ... 4}

1:		subs	r2, r2, #32
		stmgeia	r0!, {r1, r3, ip, lr}
		stmgeia	r0!, {r1, r3, ip, lr}
		bgt	1b
		LOADREGS(eqfd, sp!, {pc})

		@ r2 can be {-4 ... -28}

		cmp	r2, #-16
		stmgeia	r0!, {r1, r3, ip, lr}
		addlts	r2, r2, #16
		LOADREGS(eqfd, sp!, {pc})

		@ r2 can be {-4 ... -12}

		cmp	r2, #-8
		stmgeia	r0!, {r1, r3}
		strne	r1, [r0]
		LOADREGS(fd, sp!, {pc})

		.global	__page_memcpy
__page_memcpy:	stmfd	sp!, {r4, r5, lr}
1:		subs	r2, r2, #4*8
		ldmgeia	r1!, {r3, r4, r5, ip}
		stmgeia	r0!, {r3, r4, r5, ip}
		ldmgeia	r1!, {r3, r4, r5, ip}
		stmgeia	r0!, {r3, r4, r5, ip}
		bgt	1b
		LOADREGS(fd, sp!, {r4, r5, pc})

		.global	memset
memset:		mov	r3, r0
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

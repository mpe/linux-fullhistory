/*
 * linux/arch/arm/boot/compressed/head.S
 *
 * Copyright (C) 1996,1997,1998 Russell King
 */
#include <linux/linkage.h>

		.text
/*
 * sort out different calling conventions
 */
		.align
		.globl	_start
_start:
start:		mov	r0, r0
		mov	r0, r0
		mov	r0, r0
		mov	r0, r0
		mov	r0, r0
		mov	r0, r0
		mov	r0, r0
		mov	r0, r0
		b	1f
		.word	0x016f2818			@ Magic numbers to help the loader
		.word	_start	
1:		teq	r0, #0
		beq	2f
		mov	r4, #0x02000000
		add	r4, r4, #0x7C000
		mov	r3, #0x4000
		sub	r3, r3, #4
1:		ldmia	r0!, {r5 - r12}
		stmia	r4!, {r5 - r12}
		subs	r3, r3, #32
		bpl	1b
2:		adr	r2, LC0
		ldmia	r2, {r2, r3, r4, r5, r6, sp}
		add	r2, r2, #3
		add	r3, r3, #3
		add	sp, sp, #3
		bic	r2, r2, #3
		bic	r3, r3, #3
		bic	sp, sp, #3
		adr	r7, start
		sub	r6, r7, r6
/*
 * Relocate pointers
 */
		add	r2, r2, r6
		add	r3, r3, r6
		add	r5, r5, r6
		add	sp, sp, r6
/*
 * Clear zero-init
 */
		mov	r6, #0
1:		str	r6, [r2], #4
		cmp	r2, r3
		blt	1b
		str	r1, [r5]		@ save architecture
/*
 * Uncompress the kernel
 */
		mov	r1, #0x8000
		add	r3, r2, r1, lsl #1	@ Add 64k for malloc
		sub	r1, r1, #1
		add	r3, r3, r1
		bic	r5, r3, r1		@ decompress kernel to after end of the compressed
		mov	r0, r5
		mov	r1, r2
		mov	r2, r0
		bl	SYMBOL_NAME(decompress_kernel)
		add	r0, r0, #7
		bic	r2, r0, #7
/*
 * Now move the kernel to the correct location (r5 -> r4, len r0)
 */
		mov	r0, r4			@ r0 = start of real kernel
		mov	r1, r5			@ r1 = start of kernel image
		add	r3, r5, r2		@ r3 = end of kernel
		adr	r4, movecode
		adr	r5, movecodeend
1:		ldmia	r4!, {r6 - r12, lr}
		stmia	r3!, {r6 - r12, lr}
		cmp	r4, r5
		blt	1b
		mrc	p15, 0, r5, c0, c0
		eor	r5, r5, #0x44 << 24
		eor	r5, r5, #0x01 << 16
		eor	r5, r5, #0xa1 << 8
		movs	r5, r5, lsr #4
		mov	r5, #0
		mcreq	p15, 0, r5, c7, c5, 0	@ flush I cache
		ldr	r5, LC0 + 12		@ get architecture
		ldr	r5, [r5]
		add	pc, r1, r2		@ Call move code

/*
 * r0 = length, r1 = to, r2 = from
 */
movecode:	add	r3, r1, r2
		mov	r4, r0
1:		ldmia	r1!, {r6 - r12, lr}
		stmia	r0!, {r6 - r12, lr}
		cmp	r1, r3
		blt	1b
		mrc	p15, 0, r0, c0, c0
		eor	r0, r0, #0x44 << 24
		eor	r0, r0, #0x01 << 16
		eor	r0, r0, #0xa1 << 8
		movs	r0, r0, lsr #4
		mov	r0, #0
		mcreq	p15, 0, r0, c7, c5, 0	@ flush I cache
		mov	r1, r5			@ call kernel correctly
		mov	pc, r4			@ call via EXEC entry
movecodeend:

LC0:		.word	SYMBOL_NAME(_edata)
		.word	SYMBOL_NAME(_end)
		.word	LOADADDR
		.word	SYMBOL_NAME(architecture)
		.word	start
		.word	SYMBOL_NAME(user_stack)+4096
		.align

		.bss
SYMBOL_NAME(architecture):
		.space	4
		.align

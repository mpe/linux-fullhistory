/*
 * linux/arch/arm/boot/compressed/head.S
 *
 * Copyright (C) 1996-1999 Russell King
 */
#include <linux/linkage.h>

		.section ".start", #alloc, #execinstr
/*
 * sort out different calling conventions
 */
		.align
start:
		.type	start,#function
		.rept	8
		mov	r0, r0
		.endr

		b	1f
		.word	0x016f2818		@ Magic numbers to help the loader
		.word	start	
1:

		/*
		 * some architecture specific code can
		 * be inserted by the linker here
		 */

		.text
1:		teq	r0, #0
		bne	1b
		mov	r7, r1			@ save architecture ID
		mrc	p15, 0, r6, c0, c0	@ get processor ID
		adr	r2, LC0
		ldmia	r2, {r2, r3, r4, r5, sp}

		mov	r0, #0
1:		str	r0, [r2], #4		@ clear bss
		str	r0, [r2], #4
		str	r0, [r2], #4
		str	r0, [r2], #4
		cmp	r2, r3
		blt	1b

		mov	r1, sp			@ malloc space above stack
		add	r2, sp, #0x10000	@ 64k max

		teq	r4, r5			@ will we overwrite ourselves?
		moveq	r5, r2
		movne	r5, r4

		mov	r0, r5
		mov	r3, r7
		bl	SYMBOL_NAME(decompress_kernel)

		teq	r4, r5			@ do we need to relocate
		beq	call_kernel		@ the kernel?

		add	r0, r0, #127
		bic	r0, r0, #127		@ align the kernel length
/*
 * r0     = decompressed kernel length
 * r1-r3  = unused
 * r4     = kernel execution address
 * r5     = decompressed kernel start
 * r6     = processor ID
 * r7     = architecture ID
 * r8-r14 = unused
 */
		add	r1, r5, r0		@ end of decompressed kernel
		adr	r2, reloc_start
		adr	r3, reloc_end
1:		ldmia	r2!, {r8 - r13}		@ copy relocation code
		stmia	r1!, {r8 - r13}
		ldmia	r2!, {r8 - r13}
		stmia	r1!, {r8 - r13}
		cmp	r2, r3
		blt	1b

		eor	r1, r6, #0x44 << 24	@ SA-110?
		eor	r1, r1, #0x01 << 16
		eor	r1, r1, #0xa1 << 8
		movs	r1, r1, lsr #4
		mcreq	p15, 0, r1, c7, c7, 0	@ flush I & D-cache
		mcreq	p15, 0, r1, c7, c10, 4	@ drain WB
		add	pc, r5, r0		@ call relocation code

/*
 * r0     = decompressed kernel length
 * r1-r3  = unused
 * r4     = kernel execution address
 * r5     = decompressed kernel start
 * r6     = processor ID
 * r7     = architecture ID
 * r8-r14 = unused
 */
reloc_start:	add	r8, r5, r0
#if 0
	mov r0, r6
	mov r1, #8
	bl phex
	mov r0, #':'
	bl putc
	mov r0, r5
	mov r1, #8
	bl phex
	mov r0, #'-'
	bl putc
	mov r0, r8
	mov r1, #8
	bl phex
	mov r0, #'>'
	bl putc
	mov r0, r4
	mov r1, #8
	bl phex
	mov r0, #'\n'
	bl putc
#endif
		mov	r0, r8
		mov	r1, r4
1:
		.rept	4
		ldmia	r5!, {r2, r3, r8 - r13}	@ relocate kernel
		stmia	r1!, {r2, r3, r8 - r13}
		.endr

		cmp	r5, r0
		blt	1b
#if 0
	mov r8, r0
	mov r0, r5
	mov r1, #8
	bl phex
	mov r0, #'-'
	bl putc
	mov r0, r8
	mov r1, #8
	bl phex
	mov r0, #'\n'
	bl putc
	mov	r0, r4
	bl	memdump
#endif
		eor	r0, r6, #0x44 << 24	@ SA-110?
		eor	r0, r0, #0x01 << 16
		eor	r0, r0, #0xa1 << 8
		movs	r0, r0, lsr #4
		mcreq	p15, 0, r0, c7, c7, 0	@ flush I cache
		mcreq	p15, 0, r1, c7, c10, 4	@ drain WB

call_kernel:	mov	r0, #0
		mov	r1, r7			@ restore architecture number
		mov	pc, r4			@ call kernel

phexbuf:	.space	12

phex:		adr	r3, phexbuf
		mov	r2, #0
		strb	r2, [r3, r1]
1:		subs	r1, r1, #1
		movmi	r0, r3
		bmi	puts
		and	r2, r0, #15
		mov	r0, r0, lsr #4
		cmp	r2, #10
		addge	r2, r2, #7
		add	r2, r2, #'0'
		strb	r2, [r3, r1]
		b	1b

puts:		mov	r3, #0x7c000000
1:		ldrb	r2, [r0], #1
		teq	r2, #0
		moveq	pc, lr
2:		strb	r2, [r3, #0x3f8]
		mov	r1, #0x00020000
3:		subs	r1, r1, #1
		bne	3b
		teq	r2, #'\n'
		moveq	r2, #'\r'
		beq	2b
		teq	r0, #0
		bne	1b
		mov	pc, lr
putc:
		mov	r2, r0
		mov	r0, #0
		mov	r3, #0x7c000000
		b	2b

memdump:	mov	r12, r0
		mov	r10, lr
		mov	r1, #8
		bl	phex
		mov	r0, #'\n'
		bl	putc
		mov	r11, #0
2:		mov	r0, r11, lsl #2
		mov	r1, #4
		bl	phex
		mov	r0, #':'
		bl	putc
1:		mov	r0, #' '
		bl	putc
		ldr	r0, [r12, r11, lsl #2]
		mov	r1, #8
		bl	phex
		and	r0, r11, #7
		teq	r0, #3
		moveq	r0, #' '
		bleq	putc
		and	r0, r11, #7
		add	r11, r11, #1
		teq	r0, #7
		bne	1b
		mov	r0, #'\n'
		bl	putc
		cmp	r11, #64
		blt	2b
		mov	pc, r10
reloc_end:

LC0:		.word	__bss_start
		.word	_end
		.word	_load_addr
		.word	_start
		.word	user_stack+4096
		.align

		.section	".stack"
user_stack:	.space	4096

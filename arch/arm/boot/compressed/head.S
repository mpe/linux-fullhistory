/*
 * linux/arch/arm/boot/compressed/head.S
 *
 * Copyright (C) 1996-1999 Russell King
 */
#include <linux/linkage.h>

		.section ".start", #alloc, #execinstr

/*
 * Debugging stuff
 */
		.macro	kputc,val
		mov	r0, \val
		bl	putc
		.endm

		.macro	kphex,val,len
		mov	r0, \val
		mov	r1, #\len
		bl	phex
		.endm

		.macro	debug_reloc_start
#ifdef DEBUG
		kputc	#'\n'
		kphex	r6, 8
		kputc	#':'
		kphex	r5, 8
		kputc	#'-'
		kphex	r8, 8
		kputc	#'>'
		kphex	r4, 8
		kputc	#'\n'
#endif
		.endm

		.macro	debug_reloc_end
#ifdef DEBUG
		mov	r8, r0
		kphex	r5, 8
		kputc	#'-'
		kphex	r8, 8
		kputc	#'\n'
		mov	r0, r4
		bl	memdump
#endif
		.endm

#if 0
		.macro	loadsp,	rb
		mov	\rb, #0x7c000000
		.endm

		.macro	writeb,	rb
		strb	\rb, [r3, #0x3f8]
		.endm
#else
		.macro	loadsp,	rb
		mov	\rb, #0x03000000
		orr	\rb, \rb, #0x00010000
		.endm

		.macro	writeb,	rb
		strb	\rb, [r3, #0x3f8 << 2]
		.endm
#endif

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

		bl	cache_on

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

		bl	cache_clean_flush
		add	pc, r5, r0		@ call relocation code

/*
 * Page table physical address list
 */
		.align	5
		.type	pgtable,#object
pgtable:	.word	0x00004000		@ 0x00
		.word	0x10004000		@ 0x01
		.word	0x00000000		@ 0x02
		.word	0x40004000		@ 0x03
		.word	0x00004000		@ 0x04
		.word	0x00004000		@ 0x05
		.word	0x00004000		@ 0x06
		.word	0x80004000		@ 0x07
		.word	0x00004000		@ 0x08
		.word	0x00000000		@ 0x09
		.word	0x00000000		@ 0x0a
		.word	0x00000000		@ 0x0b
		.word	0x00000000		@ 0x0c
		.word	0x00000000		@ 0x0d
		.word	0x10004000		@ 0x0e
		.word	0x08004000		@ 0x0f
		.word	0xc0004000		@ 0x10
		.size	pgtable,. - pgtable
1:

		.type	LC0, #object
LC0:		.word	__bss_start
		.word	_end
		.word	_load_addr
		.word	_start
		.word	user_stack+4096
		.size	LC0, . - LC0

		.align	5
cache_on:	ldr	r1, proc_sa110_type
		eor	r1, r1, r6
		movs	r1, r1, lsr #5
		movne	pc, lr
		cmp	r7, #(1b - pgtable) >> 2
		movge	pc, lr
		adr	r3, pgtable
		ldr	r3, [r3, r7, lsl #2]
		teq	r3, #0
		moveq	pc, lr
/*
 * Initialise the page tables
 */
		mov	r0, r3
		mov	r8, r0, lsr #18
		mov	r8, r8, lsl #18		@ start of RAM
		add	r9, r8, #0x20000000	@ the maximum RAM size
		mov	r1, #0x12
		orr	r1, r1, #3 << 10
		add	r2, r3, #16384
1:		cmp	r1, r8			@ if virt > start of RAM
		orrge	r1, r1, #0x0c		@ set cacheable, bufferable
		cmp	r1, r9			@ if virt > end of RAM
		bicge	r1, r1, #0x0c		@ clear cacheable, bufferable
		str	r1, [r0], #4		@ 1:1 mapping
		add	r1, r1, #1048576
		teq	r0, r2
		bne	1b

		mov	r0, #0
		mcr	p15, 0, r0, c7, c10, 4	@ drain write buffer
		mcr	p15, 0, r0, c8, c7	@ flush I,D TLBs
		mcr	p15, 0, r3, c2, c0	@ load page table pointer
		mov	r0, #-1
		mcr	p15, 0, r0, c3, c0	@ load domain access register
		mrc	p15, 0, r0, c1, c0
		orr	r0, r0, #0x1000		@ I-cache enable
#ifndef DEBUG
		orr	r0, r0, #0x003d		@ Write buffer, mmu
#endif
		mcr	p15, 0, r0, c1, c0
		mov	pc, lr

/*
 * r0     = decompressed kernel length
 * r1-r3  = unused
 * r4     = kernel execution address
 * r5     = decompressed kernel start
 * r6     = processor ID
 * r7     = architecture ID
 * r8-r14 = unused
 */
		.align	5
reloc_start:	add	r8, r5, r0
		debug_reloc_start
		mov	r1, r4
1:
		.rept	4
		ldmia	r5!, {r0, r2, r3, r9 - r13}	@ relocate kernel
		stmia	r1!, {r0, r2, r3, r9 - r13}
		.endr

		cmp	r5, r8
		blt	1b
		debug_reloc_end

call_kernel:	bl	cache_clean_flush
		bl	cache_off
		mov	r0, #0
		mov	r1, r7			@ restore architecture number
		mov	pc, r4			@ call kernel

		.type	proc_sa110_type,#object
proc_sa110_type:
		.word	0x4401a100
		.size	proc_sa110_type, . - proc_sa110_type

/*
 * Turn off StrongARM cache and MMU
 */
		.align	5
cache_off:	ldr	r1, proc_sa110_type
		eor	r1, r1, r6
		movs	r1, r1, lsr #5
		movne	pc, lr
		mrc	p15, 0, r0, c1, c0
		bic	r0, r0, #0x000d
		mcr	p15, 0, r0, c1, c0
		mov	pc, lr		

/*
 * Clean and flush the cache to maintain consistency.
 */
		.align	5
cache_clean_flush:
		ldr	r1, proc_sa110_type	@ SA-110 or SA-1100?
		eor	r1, r1, r6
		movs	r1, r1, lsr #5
		movne	pc, lr

		bic	r1, pc, #31
		add	r2, r1, #32768
1:		ldr	r12, [r1], #32
		teq	r1, r2
		bne	1b

		mcr	p15, 0, r1, c7, c7, 0	@ flush I cache
		mcr	p15, 0, r1, c7, c10, 4	@ drain WB
		mov	pc, lr

#ifdef DEBUG
		.type	phexbuf,#object
phexbuf:	.space	12
		.size	phexbuf, . - phexbuf

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

puts:		loadsp	r3
1:		ldrb	r2, [r0], #1
		teq	r2, #0
		moveq	pc, lr
2:		writeb	r2
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
		loadsp	r3
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
#endif

reloc_end:

		.align
		.section	".stack"
user_stack:	.space	4096

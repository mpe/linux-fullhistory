/*
 * linux/arch/arm/boot/compressed/head-nexuspci.S
 *
 * Copyright (C) 1996 Philip Blundell
 */

#define ARM_CP			p15
#define ARM610_REG_CONTROL	cr1
#define ARM_REG_ZERO		cr0

		.text

start:		b	skip1
		b	go_uncompress
		b	go_uncompress
		b	go_uncompress
		b	go_uncompress
		b	go_uncompress
		b	go_uncompress
		b	go_uncompress
		b	go_uncompress
		b	go_uncompress
skip1:		mov	sp, #0x40000000
		add	sp, sp, #0x200000
		mov	r2, #0x20000000
		mov	r1, #0x1a
		str	r1, [r2]

		MOV	r0, #0x30
		MCR	ARM_CP, 0, r0, ARM610_REG_CONTROL, ARM_REG_ZERO
		
		mov	r2, #0x10000000

		mov	r1, #42
		strb	r1, [r2, #8]

		mov	r1, #48
		strb	r1, [r2, #8]

		mov	r1, #16
		strb	r1, [r2, #8]

		mov	r1, #0x93
		strb	r1, [r2, #0]
		mov	r1, #0x17
		strb	r1, [r2, #0]

		mov	r1, #0xbb
		strb	r1, [r2, #0x4]

		mov	r1, #0x78
		strb	r1, [r2, #0x10]

		mov	r1, #160
		strb	r1, [r2, #0x8]

		mov	r1, #5
		strb	r1, [r2, #0x8]

		mov	r0, #0x50
		bl	_ll_write_char

		mov	r4, #0x40000000
		mov	r1, #0x00200000
		add	r4, r4, r1
copylp:
		ldr	r3, [r1]
		str	r3, [r4, r1]
		subs	r1, r1, #4
		bne	copylp

		add	pc, r4, #0x28


/*
 * Uncompress the kernel
 */
go_uncompress:
		mov	r0,   #0x40000000
		add	r0, r0, #0x300000
		bl	_decompress_kernel

		mov	r0,   #0x40000000
		add	r1, r0, #0x300000
		mov	r2, #0x100000

clp2:		ldr	r3, [r1, r2]
		str	r3, [r0, r2]
		subs	r2, r2, #4
		bne	clp2

		mov	r2, #0x40000000
		mov	r0, #0
		mov	r1, #3
		add	pc, r2, #0x20		@ call via EXEC entry

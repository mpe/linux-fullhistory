/*
 * linux/arch/arm/boot/compressed/head-nexuspci.S
 *
 * Copyright (C) 1996, 1997, 1998 Philip Blundell
 *
 * NexusPCI is unusual because we don't have a bootloader -- the kernel is
 * run directly out of ROM at the moment.  Maybe this will change one day and
 * then this file can go away.
 *
 */

		.text

.globl	_start
_start:		b	reset
		b	undefined
		b	undefined
		b	undefined
		b	undefined
		b	undefined
		b	undefined
		b	undefined
		b	go_uncompress
	
reset:		mov	r2, #0x20000000			@ LED off
		mov	r1, #0x1a
		str	r1, [r2]

		mov	r2, #0x10000000			@ SCC init

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

		ldr	r2, =_start
		ldr	r3, =_edata
		mov	r8, r2
		mov	r0, #0
1:
		ldmia	r0!, {r4, r5, r6, r7}
		stmia	r2!, {r4, r5, r6, r7}
		cmp	r2, r3
		ble	1b

		ldr	r3, =_edata
		ldr	r1, =_end
		mov	r2, #0
1:
		strb	r2, [r3]
		cmp	r3, r1
		beq	2f
		add	r3, r3, #1
		b	1b
2:	
		add	pc, r8, #0x20

undefined:	ldr	r4, =undef_msg
1:		ldrb	r0, [r4], #1
		movs	r0, r0
2:		beq	2b
		bl	_ll_write_char
		b	1b

undef_msg:	.ascii	"Undefined instruction (or other problem)\000"
		.align	4

/*
 * Uncompress the kernel
 */
go_uncompress:
		mov	r0, #0x40000000
		ldr	sp, =user_stack
		add	sp, sp, #4096
		bl	decompress_kernel

		mov	r2, #0x40000000
		mov	r0, #0
		mov	r1, #3
		add	pc, r2, #0x20		@ call via EXEC entry

/*
 * linux/arch/arm/boot/compressed/head-victor.S
 *
 * Copyright (C) 1998 Nicolas Pitre <nico@visuaide.com>
 */

#include <linux/linkage.h>

		.text
		.globl	_start
_start:
		@ just in case we still use an a.out loader...
		nop
		nop
		nop
		nop
		nop
		nop
		nop
		nop

		@ load different addresses
		adr	r2, LC0
		ldmia	r2, {r4, r5, r6, sp}

		@ clear BSS
		mov	r2, #0
1:		str	r2, [r5], #4
		cmp	r5, r6
		blt	1b

		@ uncompress the kernel
		mov	r8, r0		@ save cmdline ptr
		mov	r0, r4		@ where to put uncompressed data
		add	r1, r6, #31
		bic	r1, r1, #31	@ free memory space
		add	r2, r1, #65536	@ end of free mem space
		bl	SYMBOL_NAME(decompress_kernel)
		mov	r0, r8		@ retrieve cmdline ptr
		mov	pc, r4		@ call via EXEC entry

LC0:		.word	_load_addr
		.word	__bss_start
		.word	SYMBOL_NAME(_end)
		.word	SYMBOL_NAME(user_stack)+4096

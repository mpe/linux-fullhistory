/* $Id: head.S,v 1.6 1999/10/05 12:34:16 gniibe Exp $
 *
 *  arch/sh/kernel/head.S
 *
 *  Copyright (C) 1999  Niibe Yutaka & Kaz Kojima
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Head.S contains the SH exception handlers and startup code.
 */
#include <linux/linkage.h>

	.section	.empty_zero_page, "aw"
ENTRY(empty_zero_page)
	.long	1		/* MOUNT_ROOT_RDONLY */
	.long	0		/* RAMDISK_FLAGS */
	.long	0x0200		/* ORIG_ROOT_DEV */
	.long	1		/* LOADER_TYPE */
	.long	0x88400000	/* INITRD_START */
	.long	0x00400000	/* INITRD_SIZE */
	.long	0x89000000	/* MEMORY_END */
	.long	0

	.text	
	.balign 4096,0,4096
/*
 * Condition at the entry of _stext:
 *
 *   BSC has already been initialized.
 *   INTC may or may not be initialized.
 *   VBR may or may not be initialized.
 *   MMU may or may not be initialized.
 *   Cache may or may not be initialized.
 *   Hardware (including on-chip modules) may or may not be initialized. 
 *
 *   The register R4&R5 holds the address of the parameter block, which has
 *   command-line data, etc.
 *
 */
ENTRY(_stext)
#if defined(__SH4__)
	!			Initialize FPSCR	
	/* GCC (as of 2.95.1) assumes FPU with double precision mode. */
	mov.l	7f,r0
	lds	r0,fpscr
#endif
	!			Initialize Status Register
	mov.l	1f,r0		! MD=1, RB=0, BL=1
	ldc	r0,sr
	!
	mov.l	2f,r0
	mov	r0,r15		! Set initial r15 (stack pointer)
	ldc	r0,r4_bank	!   and stack base
	!
	!			Enable cache
	mov.l	6f,r0
	jsr	@r0
	 nop
	!			Clear BSS area
	mov.l	3f,r1
	add	#4,r1
	mov.l	4f,r2
	mov	#0,r0
9:	cmp/hs	r2,r1
	bf/s	9b		! while (r1 < r2)
	 mov.l	r0,@-r2
	!			Start kernel
	mov.l	5f,r0
	jmp	@r0
	 nop

	.balign 4
1:	.long	0x50000000		! MD=1, RB=0, BL=1
2:	.long	SYMBOL_NAME(stack)
3:	.long	SYMBOL_NAME(__bss_start)
4:	.long	SYMBOL_NAME(_end)
5:	.long	SYMBOL_NAME(start_kernel)
6:	.long	SYMBOL_NAME(cache_init)
#if defined(__SH4__)
7:	.long   0x00080000
#endif

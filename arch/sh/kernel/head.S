/* $Id$
 *
 *  arch/sh/kernel/head.S
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Head.S contains the SH exception handlers and startup code.
 */
#include <linux/config.h>
#include <linux/threads.h>
#include <linux/linkage.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#ifdef CONFIG_CPU_SH3
/* Following values are assumed to be as small as immediate. */
#define CCR		0xffffffec	/* Address of Cache Control Register */
#define CACHE_INIT	0x00000009	/* 8k-byte cache, flush, enable */
#elif CONFIG_CPU_SH4
/* Should fill here. */
#endif

ENTRY(_stext)
	!			Switch to register bank 0
	stc	sr,r1		!
	mov.l	1f,r0		! RB=0, BL=1
	and	r1,r0
	ldc	r0,sr
	!			Enable cache
#ifdef CONFIG_CPU_SH3
	mov	#CCR,r1
	mov.l	@r1,r0
	cmp/eq	#1,r0		! If it's enabled already, don't flush it
	bt/s	8f
	 mov	#CACHE_INIT,r0
	mov.l	r0,@r1
#elif CONFIG_CPU_SH4
	! Should fill here.
#endif
8:
	!
	mov.l	2f,r0
	mov	r0,r15		! Set initial r15 (stack pointer)
	ldc	r0,r4_bank	!   and stack base
	!			Clear BSS area
	mov.l	3f,r1
	mov.l	4f,r2
	mov	#0,r0
9:	mov.l	r0,@r1
	cmp/hs	r2,r1
	bf/s	9b
	 add	#4,r1
	!			Start kernel
	mov.l	5f,r0
	jmp	@r0
	 nop

	.balign 4
1:	.long 0xdfffffff		! RB=0, BL=1
2:	.long SYMBOL_NAME(stack)
3:	.long SYMBOL_NAME(__bss_start)
4:	.long SYMBOL_NAME(_end)
5:	.long SYMBOL_NAME(start_kernel)

.data

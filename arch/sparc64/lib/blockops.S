/* $Id: blockops.S,v 1.1 1996/12/22 07:42:15 davem Exp $
 * arch/sparc64/lib/blockops.S: UltraSparc block zero optimized routines.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/asi.h>

	/* Zero out 64 bytes of memory at (buf + offset). */
#define BLAST_BLOCK(buf, offset)				\
	stx		%g0, [buf + offset + 0x38];		\
	stx		%g0, [buf + offset + 0x30];		\
	stx		%g0, [buf + offset + 0x28];		\
	stx		%g0, [buf + offset + 0x20];		\
	stx		%g0, [buf + offset + 0x18];		\
	stx		%g0, [buf + offset + 0x10];		\
	stx		%g0, [buf + offset + 0x08];		\
	stx		%g0, [buf + offset + 0x00];

	/* Copy 32 bytes of memory at (src + offset) to
	 * (dst + offset).
	 */
#define MIRROR_BLOCK(dst, src, offset, t0, t1, t2, t3)		\
	ldx		[src + offset + 0x18], t0;		\
	ldx		[src + offset + 0x10], t1;		\
	ldx		[src + offset + 0x08], t2;		\
	ldx		[src + offset + 0x00], t3;		\
	stx		t0, [dst + offset + 0x18];		\
	stx		t1, [dst + offset + 0x10];		\
	stx		t2, [dst + offset + 0x08];		\
	stx		t3, [dst + offset + 0x00];

	.text
	.align	4

	.globl	C_LABEL(bzero_2page), C_LABEL(bzero_1page)
C_LABEL(bzero_2page):
	/* %o0 = buf */
	or		%o0, %g0, %o1
	or		%g0, 0x40, %g2
1:
	BLAST_BLOCK(%o0, 0x00)
	BLAST_BLOCK(%o0, 0x40)
	BLAST_BLOCK(%o0, 0x80)
	BLAST_BLOCK(%o0, 0xc0)
	subcc		%g2, 1, %g2
	bne,pt		%icc, 1b
	 add		%o0, 0x100, %o0

	retl
	 mov		%o1, %o0

C_LABEL(bzero_1page):
	/* %o0 = buf */
	or		%o0, %g0, %o1
	or		%g0, 0x20, %g2
1:
	BLAST_BLOCK(%o0, 0x00)
	BLAST_BLOCK(%o0, 0x40)
	BLAST_BLOCK(%o0, 0x80)
	BLAST_BLOCK(%o0, 0xc0)
	subcc		%g2, 1, %g2
	bne,pt		%icc, 1b
	 add		%o0, 0x100, %o0

	retl
	 mov		%o1, %o0

	.globl	C_LABEL(__copy_1page)
C_LABEL(__copy_1page):
	/* %o0 = dst, %o1 = src */
	or		%g0, 0x10, %g1
1:
	MIRROR_BLOCK(%o0, %o1, 0x00, %o2, %o3, %o4, %o5)
	MIRROR_BLOCK(%o0, %o1, 0x20, %o2, %o3, %o4, %o5)
	MIRROR_BLOCK(%o0, %o1, 0x40, %o2, %o3, %o4, %o5)
	MIRROR_BLOCK(%o0, %o1, 0x60, %o2, %o3, %o4, %o5)
	MIRROR_BLOCK(%o0, %o1, 0x80, %o2, %o3, %o4, %o5)
	MIRROR_BLOCK(%o0, %o1, 0xa0, %o2, %o3, %o4, %o5)
	MIRROR_BLOCK(%o0, %o1, 0xc0, %o2, %o3, %o4, %o5)
	MIRROR_BLOCK(%o0, %o1, 0xe0, %o2, %o3, %o4, %o5)
	subcc		%g1, 1, %g1
	add		%o0, 0x100, %o0
	bne,pt		%icc, 1b
	 add		%o1, 0x100, %o1

	retl
	 nop


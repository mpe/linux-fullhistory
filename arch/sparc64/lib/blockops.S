/* $Id: blockops.S,v 1.5 1997/03/26 18:34:28 jj Exp $
 * arch/sparc64/lib/blockops.S: UltraSparc block zero optimized routines.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <asm/asi.h>

	/* Zero out 256 bytes of memory at (buf + offset). */
#define BLAST_BLOCK(buf, offset)				\
	stda		%f48, [buf + offset + 0x00] %asi;	\
	stda		%f48, [buf + offset + 0x40] %asi;	\
	stda		%f48, [buf + offset + 0x80] %asi;	\
	stda		%f48, [buf + offset + 0xc0] %asi;

	/* Copy 256 bytes of memory at (src + offset) to
	 * (dst + offset).
	 */
#define MIRROR_BLOCK(dst, src, offset, sync)			\
	ldda		[src + offset + 0x000] %asi, %f0;	\
	ldda		[src + offset + 0x040] %asi, %f16;	\
	ldda		[src + offset + 0x080] %asi, %f32;	\
	ldda		[src + offset + 0x0c0] %asi, %f48;	\
	membar		sync;					\
	stda		%f0, [dst + offset + 0x000] %asi;	\
	stda		%f16, [dst + offset + 0x040] %asi;	\
	stda		%f32, [dst + offset + 0x080] %asi;	\
	stda		%f48, [dst + offset + 0x0c0] %asi;

	.text
	.align	4

	.globl		bzero_2page, bzero_1page
bzero_2page:
	/* %o0 = buf */
	mov		%o0, %o1
	wr		%g0, ASI_BLK_P, %asi
	mov		0x10, %g2

	membar		#Sync|#StoreLoad

	fzero		%f48
	fzero		%f50
	fzero		%f52
	fzero		%f54
	fzero		%f56
	fzero		%f58
	fzero		%f60
	fzero		%f62
1:
	BLAST_BLOCK(%o0, 0x000)
	BLAST_BLOCK(%o0, 0x100)
	BLAST_BLOCK(%o0, 0x200)
	BLAST_BLOCK(%o0, 0x300)
	subcc		%g2, 1, %g2
	bne,pt		%icc, 1b
	 add		%o0, 0x400, %o0

	membar		#Sync|#LoadStore|#StoreStore

	retl
	 mov		%o1, %o0

bzero_1page:
	/* %o0 = buf */
	mov		%o0, %o1
	wr		%g0, ASI_BLK_P, %asi
	mov		0x08, %g2
	membar		#Sync|#StoreLoad
	fzero		%f48
	fzero		%f50
	fzero		%f52
	fzero		%f54
	fzero		%f56
	fzero		%f58
	fzero		%f60
	fzero		%f62
1:
	BLAST_BLOCK(%o0, 0x000)
	BLAST_BLOCK(%o0, 0x100)
	BLAST_BLOCK(%o0, 0x200)
	BLAST_BLOCK(%o0, 0x300)
	subcc		%g2, 1, %g2
	bne,pt		%icc, 1b
	 add		%o0, 0x400, %o0

	membar		#Sync|#LoadStore|#StoreStore

	retl
	 mov		%o1, %o0

	.globl		__bfill64
__bfill64:
	/* %o0 = buf */
	stx		%o1, [%sp + 0x7ff + 128]
	wr		%g0, ASI_BLK_P, %asi
	mov		0x08, %g2
	ldd		[%sp + 0x7ff + 128], %f48
	membar		#Sync|#StoreLoad
	fmovd		%f48, %f50
	fmovd		%f48, %f52
	fmovd		%f48, %f54
	fmovd		%f48, %f56
	fmovd		%f48, %f58
	fmovd		%f48, %f60
	fmovd		%f48, %f62
1:
	BLAST_BLOCK(%o0, 0x000)
	BLAST_BLOCK(%o0, 0x100)
	BLAST_BLOCK(%o0, 0x200)
	BLAST_BLOCK(%o0, 0x300)
	subcc		%g2, 1, %g2
	bne,pt		%icc, 1b
	 add		%o0, 0x400, %o0

	retl
	 membar		#Sync|#LoadStore|#StoreStore

	.globl		__copy_1page
__copy_1page:
	/* %o0 = dst, %o1 = src */
	or		%g0, 0x08, %g1
	wr		%g0, ASI_BLK_P, %asi
	membar		#Sync|#StoreLoad
1:
	MIRROR_BLOCK(%o0, %o1, 0x000, #Sync)
	MIRROR_BLOCK(%o0, %o1, 0x100, #Sync)
	MIRROR_BLOCK(%o0, %o1, 0x200, #Sync)
	MIRROR_BLOCK(%o0, %o1, 0x300, #Sync)
	subcc		%g1, 1, %g1
	add		%o0, 0x400, %o0
	bne,pt		%icc, 1b
	 add		%o1, 0x400, %o1

	retl
	 membar		#Sync|#LoadStore|#StoreStore


/* memcpy.S: Sparc optimized memcpy code.
 *
 *  Copyright(C) 1995 Linus Torvalds
 *  Copyright(C) 1996 David S. Miller
 *  Copyright(C) 1996 Eddie C. Dost
 *  Copyright(C) 1996 Jakub Jelinek
 *
 * derived from:
 *	e-mail between David and Eddie.
 */

#include <asm/cprefix.h>
#include <asm/ptrace.h>

/* Both these macros have to start with exactly the same insn */
#define MOVE_BIGCHUNK(src, dst, offset, t0, t1, t2, t3, t4, t5, t6, t7) \
	ldd	[%src + offset + 0x00], %t0; \
	ldd	[%src + offset + 0x08], %t2; \
	ldd	[%src + offset + 0x10], %t4; \
	ldd	[%src + offset + 0x18], %t6; \
	st	%t0, [%dst + offset + 0x00]; \
	st	%t1, [%dst + offset + 0x04]; \
	st	%t2, [%dst + offset + 0x08]; \
	st	%t3, [%dst + offset + 0x0c]; \
	st	%t4, [%dst + offset + 0x10]; \
	st	%t5, [%dst + offset + 0x14]; \
	st	%t6, [%dst + offset + 0x18]; \
	st	%t7, [%dst + offset + 0x1c];

#define MOVE_BIGALIGNCHUNK(src, dst, offset, t0, t1, t2, t3, t4, t5, t6, t7) \
	ldd	[%src + offset + 0x00], %t0; \
	ldd	[%src + offset + 0x08], %t2; \
	ldd	[%src + offset + 0x10], %t4; \
	ldd	[%src + offset + 0x18], %t6; \
	std	%t0, [%dst + offset + 0x00]; \
	std	%t2, [%dst + offset + 0x08]; \
	std	%t4, [%dst + offset + 0x10]; \
	std	%t6, [%dst + offset + 0x18];

#define MOVE_LASTCHUNK(src, dst, offset, t0, t1, t2, t3) \
	ldd	[%src - offset - 0x10], %t0; \
	ldd	[%src - offset - 0x08], %t2; \
	st	%t0, [%dst - offset - 0x10]; \
	st	%t1, [%dst - offset - 0x0c]; \
	st	%t2, [%dst - offset - 0x08]; \
	st	%t3, [%dst - offset - 0x04];

#define MOVE_HALFCHUNK(src, dst, offset, t0, t1, t2, t3) \
	lduh	[%src + offset + 0x00], %t0; \
	lduh	[%src + offset + 0x02], %t1; \
	lduh	[%src + offset + 0x04], %t2; \
	lduh	[%src + offset + 0x06], %t3; \
	sth	%t0, [%dst + offset + 0x00]; \
	sth	%t1, [%dst + offset + 0x02]; \
	sth	%t2, [%dst + offset + 0x04]; \
	sth	%t3, [%dst + offset + 0x06];

#define MOVE_SHORTCHUNK(src, dst, offset, t0, t1) \
	ldub	[%src - offset - 0x02], %t0; \
	ldub	[%src - offset - 0x01], %t1; \
	stb	%t0, [%dst - offset - 0x02]; \
	stb	%t1, [%dst - offset - 0x01];

	.text
	.align	4

	.globl	C_LABEL(__memcpy), C_LABEL(memcpy), C_LABEL(bcopy)
	.globl	C_LABEL(amemmove), C_LABEL(memmove)
C_LABEL(bcopy):
	mov	%o0, %o3
	mov	%o1, %o0
	mov	%o3, %o1
C_LABEL(amemmove):
C_LABEL(memmove):
/* This should be kept as optimized as possible */
	cmp	%o0, %o1
	bleu	1f
	 xor	%o0, %o1, %o4

	add	%o1, %o2, %o3
	cmp	%o3, %o0
	bleu	2f
	 andcc	%o4, 3, %g0

/* But I think from now on, we can hold on. Or tell me, is memmoving
 * overlapping regions such a nice game? */

	mov	%o0, %g1
	add	%o1, %o2, %o1
	add	%o0, %o2, %o0
	sub	%o1, 1, %o1
	sub	%o0, 1, %o0
	
reverse_bytes:
	ldub	[%o1], %o4
	subcc	%o2, 1, %o2
	stb	%o4, [%o0]
	sub	%o1, 1, %o1
	bne	reverse_bytes
	 sub	%o0, 1, %o0

	retl
	 mov	%g1, %o0

/* And here start optimizing again... */

dword_align:
	andcc	%o1, 1, %g0
	be	4f
	 andcc	%o1, 2, %g0

	ldub	[%o1], %g2
	add	%o1, 1, %o1
	stb	%g2, [%o0]
	sub	%o2, 1, %o2
	bne	3f
	 add	%o0, 1, %o0

	lduh	[%o1], %g2
	add	%o1, 2, %o1
	sth	%g2, [%o0]
	sub	%o2, 2, %o2
	b	3f
	 add	%o0, 2, %o0
4:
	lduh	[%o1], %g2
	add	%o1, 2, %o1
	sth	%g2, [%o0]
	sub	%o2, 2, %o2
	b	3f
	 add	%o0, 2, %o0

C_LABEL(__memcpy):
C_LABEL(memcpy):	/* %o0=dst %o1=src %o2=len */
	xor	%o0, %o1, %o4
1:
	andcc	%o4, 3, %o5
2:
	bne	cannot_optimize
	 cmp	%o2, 15

	bleu	short_aligned_end
	 andcc	%o1, 3, %g0

	bne	dword_align
3:
	 andcc	%o1, 4, %g0

	be	2f
	 mov	%o2, %g1

	ld	[%o1], %o4
	sub	%g1, 4, %g1
	st	%o4, [%o0]
	add	%o1, 4, %o1
	add	%o0, 4, %o0
2:
	andcc	%g1, 0xffffff80, %g7
	be	3f
	 andcc	%o0, 4, %g0

	be	ldd_std + 4
5:
	MOVE_BIGCHUNK(o1, o0, 0x00, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x20, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x40, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x60, o2, o3, o4, o5, g2, g3, g4, g5)
	subcc	%g7, 128, %g7
	add	%o1, 128, %o1
	bne	5b
	 add	%o0, 128, %o0
3:
	andcc	%g1, 0x70, %g7
	be	memcpy_table_end
	 andcc	%g1, 8, %g0

	sethi	%hi(memcpy_table_end), %o5
	srl	%g7, 1, %o4
	add	%g7, %o4, %o4
	add	%o1, %g7, %o1
	sub	%o5, %o4, %o5
	jmpl	%o5 + %lo(memcpy_table_end), %g0
	 add	%o0, %g7, %o0

memcpy_table:
	MOVE_LASTCHUNK(o1, o0, 0x60, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x50, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x40, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x30, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x20, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x10, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x00, g2, g3, g4, g5)

memcpy_table_end:
	be	memcpy_last7
	 andcc	%g1, 4, %g0

	ldd	[%o1], %g2
	add	%o0, 8, %o0
	add	%o1, 8, %o1
	st	%g2, [%o0 - 0x08]
	st	%g3, [%o0 - 0x04]
memcpy_last7:
	be	1f
	 andcc	%g1, 2, %g0

	ld	[%o1], %g2
	add	%o1, 4, %o1
	st	%g2, [%o0]
	add	%o0, 4, %o0
1:
	be	1f
	 andcc	%g1, 1, %g0

	lduh	[%o1], %g2
	add	%o1, 2, %o1
	sth	%g2, [%o0]
	add	%o0, 2, %o0
1:
	be	1f
	 nop

	ldub	[%o1], %g2
	stb	%g2, [%o0]
1:
	retl
 	 nop

	/* Placed here for cache reasons. */
	.globl	C_LABEL(__copy_to_user), C_LABEL(__copy_from_user)
C_LABEL(__copy_to_user):
	b	copy_user_common
	 st	%o0, [%g6 + THREAD_EX_ADDR]

C_LABEL(__copy_from_user):
	st	%o1, [%g6 + THREAD_EX_ADDR]

copy_user_common:
	ld	[%g6 + THREAD_EX_COUNT], %g1
	set	copy_user_failure, %g2
	add	%g1, 1, %g1
	st	%o7, [%g6 + THREAD_EX_PC]
	st	%g1, [%g6 + THREAD_EX_COUNT]
	call	C_LABEL(__memcpy)
	 st	%g2, [%g6 + THREAD_EX_EXPC]

copy_user_success:
	ldd	[%g6 + THREAD_EX_COUNT], %g2
	mov	0, %o0
	sub	%g2, 1, %g1
	jmpl	%g3 + 0x8, %g0
	 st	%g1, [%g6 + THREAD_EX_COUNT]

copy_user_failure:
	jmpl	%g3 + 0x8, %g0
	 mov	%g2, %o0

ldd_std:
	MOVE_BIGALIGNCHUNK(o1, o0, 0x00, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGALIGNCHUNK(o1, o0, 0x20, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGALIGNCHUNK(o1, o0, 0x40, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGALIGNCHUNK(o1, o0, 0x60, o2, o3, o4, o5, g2, g3, g4, g5)
	subcc	%g7, 128, %g7
	add	%o1, 128, %o1
	bne	ldd_std
	 add	%o0, 128, %o0

	andcc	%g1, 0x70, %g7
	be	memcpy_table_end
	 andcc	%g1, 8, %g0

	sethi	%hi(memcpy_table_end), %o5
	srl	%g7, 1, %o4
	add	%g7, %o4, %o4
	add	%o1, %g7, %o1
	sub	%o5, %o4, %o5
	jmpl	%o5 + %lo(memcpy_table_end), %g0
	 add	%o0, %g7, %o0

cannot_optimize:
	bleu	short_end
	 cmp	%o5, 2

	bne	byte_chunk
	 and	%o2, 0xfffffff0, %o3
	 
	andcc	%o1, 1, %g0
	be	1f
	 nop

	ldub	[%o1], %g2
	add	%o1, 1, %o1
	sub	%o2, 1, %o2
	stb	%g2, [%o0]
	andcc	%o2, 0xfffffff0, %o3
	be	short_end
	 add	%o0, 1, %o0
1:
	MOVE_HALFCHUNK(o1, o0, 0x00, g2, g3, g4, g5)
	MOVE_HALFCHUNK(o1, o0, 0x08, g2, g3, g4, g5)
	subcc	%o3, 0x10, %o3
	add	%o1, 0x10, %o1
	bne	1b
	 add	%o0, 0x10, %o0
	b	2f
	 and	%o2, 0xe, %o3
	
byte_chunk:
	MOVE_SHORTCHUNK(o1, o0, -0x02, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, -0x04, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, -0x06, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, -0x08, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, -0x0a, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, -0x0c, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, -0x0e, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, -0x10, g2, g3)
	subcc	%o3, 0x10, %o3
	add	%o1, 0x10, %o1
	bne	byte_chunk
	 add	%o0, 0x10, %o0

short_end:
	and	%o2, 0xe, %o3
2:
	sethi	%hi(short_table_end), %o5
	sll	%o3, 3, %o4
	add	%o0, %o3, %o0
	sub	%o5, %o4, %o5
	add	%o1, %o3, %o1
	jmpl	%o5 + %lo(short_table_end), %g0
	 andcc	%o2, 1, %g0

	MOVE_SHORTCHUNK(o1, o0, 0x0c, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x0a, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x08, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x06, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x04, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x02, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x00, g2, g3)
short_table_end:
	be	1f
	 nop
	ldub	[%o1], %g2
	stb	%g2, [%o0]
1:
	retl
 	 nop

short_aligned_end:
	bne	short_end
	 andcc	%o2, 8, %g0

	be	1f
	 andcc	%o2, 4, %g0

	ld	[%o1 + 0x00], %g2
	ld	[%o1 + 0x04], %g3
	add	%o1, 8, %o1
	st	%g2, [%o0 + 0x00]
	st	%g3, [%o0 + 0x04]
	add	%o0, 8, %o0
1:
	b	memcpy_last7
	 mov	%o2, %g1

/* copy_user.S: Sparc optimized copy_from_user code.
 *
 *  Copyright(C) 1995 Linus Torvalds
 *  Copyright(C) 1996 David S. Miller
 *  Copyright(C) 1996 Eddie C. Dost
 *  Copyright(C) 1996,1997 Jakub Jelinek
 *
 * derived from:
 *	e-mail between David and Eddie.
 *
 * Returns 0 if successful, otherwise count of bytes not copied yet
 *
 * FIXME: This code should be optimized for sparc64... -jj
 */

#include <asm/ptrace.h>
#include <asm/asi.h>
#include <asm/head.h>

#define PRE_RETL sethi %uhi(KERNBASE), %g4; sllx %g4, 32, %g4;

#define EX(x,y,a,b,z) 				\
98: 	x,y;					\
	.section .fixup,z##alloc,z##execinstr;	\
	.align	4;				\
99:	PRE_RETL				\
	retl;					\
	 a, b, %o0;				\
	.section __ex_table,z##alloc;		\
	.align	4;				\
	.word	98b, 99b;			\
	.text;					\
	.align	4

#define EX2(x,y,c,d,e,a,b,z) 			\
98: 	x,y;					\
	.section .fixup,z##alloc,z##execinstr;	\
	.align	4;				\
99:	c, d, e;				\
	PRE_RETL				\
	retl;					\
	 a, b, %o0;				\
	.section __ex_table,z##alloc;		\
	.align	4;				\
	.word	98b, 99b;			\
	.text;					\
	.align	4

#define EXO2(x,y,z) 				\
98: 	x,##y;					\
	.section __ex_table,z##alloc;		\
	.align	4;				\
	.word	98b, 97f;			\
	.text;					\
	.align	4

#define EXT(start,end,handler,z) 		\
	.section __ex_table,z##alloc;		\
	.align	4;				\
	.word	start, 0, end, handler;		\
	.text;					\
	.align	4

/* Please do not change following macros unless you change logic used
 * in .fixup at the end of this file as well
 */

/* Both these macros have to start with exactly the same insn */
#define MOVE_BIGCHUNK(src, dst, offset, t0, t1, t2, t3, t4, t5, t6, t7) \
	ldda	[%src + offset + 0x00] %asi, %t0; \
	ldda	[%src + offset + 0x08] %asi, %t2; \
	ldda	[%src + offset + 0x10] %asi, %t4; \
	ldda	[%src + offset + 0x18] %asi, %t6; \
	st	%t0, [%dst + offset + 0x00]; \
	st	%t1, [%dst + offset + 0x04]; \
	st	%t2, [%dst + offset + 0x08]; \
	st	%t3, [%dst + offset + 0x0c]; \
	st	%t4, [%dst + offset + 0x10]; \
	st	%t5, [%dst + offset + 0x14]; \
	st	%t6, [%dst + offset + 0x18]; \
	st	%t7, [%dst + offset + 0x1c];

#define MOVE_BIGALIGNCHUNK(src, dst, offset, t0, t1, t2, t3, t4, t5, t6, t7) \
	ldda	[%src + offset + 0x00] %asi, %t0; \
	ldda	[%src + offset + 0x08] %asi, %t2; \
	ldda	[%src + offset + 0x10] %asi, %t4; \
	ldda	[%src + offset + 0x18] %asi, %t6; \
	std	%t0, [%dst + offset + 0x00]; \
	std	%t2, [%dst + offset + 0x08]; \
	std	%t4, [%dst + offset + 0x10]; \
	std	%t6, [%dst + offset + 0x18];

#define MOVE_LASTCHUNK(src, dst, offset, t0, t1, t2, t3) \
	ldda	[%src - offset - 0x10] %asi, %t0; \
	ldda	[%src - offset - 0x08] %asi, %t2; \
	st	%t0, [%dst - offset - 0x10]; \
	st	%t1, [%dst - offset - 0x0c]; \
	st	%t2, [%dst - offset - 0x08]; \
	st	%t3, [%dst - offset - 0x04];

#define MOVE_HALFCHUNK(src, dst, offset, t0, t1, t2, t3) \
	lduha	[%src + offset + 0x00] %asi, %t0; \
	lduha	[%src + offset + 0x02] %asi, %t1; \
	lduha	[%src + offset + 0x04] %asi, %t2; \
	lduha	[%src + offset + 0x06] %asi, %t3; \
	sth	%t0, [%dst + offset + 0x00]; \
	sth	%t1, [%dst + offset + 0x02]; \
	sth	%t2, [%dst + offset + 0x04]; \
	sth	%t3, [%dst + offset + 0x06];

#define MOVE_SHORTCHUNK(src, dst, offset, t0, t1) \
	lduba	[%src - offset - 0x02] %asi, %t0; \
	lduba	[%src - offset - 0x01] %asi, %t1; \
	stb	%t0, [%dst - offset - 0x02]; \
	stb	%t1, [%dst - offset - 0x01];

	.text
	.align	4

	.globl	__copy_from_user
dword_align:
	andcc	%o1, 1, %g0
	be	4f
	 andcc	%o1, 2, %g0

	EXO2(lduba [%o1] %asi, %g2,#)
	add	%o1, 1, %o1
	stb	%g2, [%o0]
	sub	%o2, 1, %o2
	bne	3f
	 add	%o0, 1, %o0

	EXO2(lduha [%o1] %asi, %g2,#)
	add	%o1, 2, %o1
	sth	%g2, [%o0]
	sub	%o2, 2, %o2
	ba,pt	%xcc, 3f
	 add	%o0, 2, %o0
4:
	EXO2(lduha [%o1] %asi, %g2,#)
	add	%o1, 2, %o1
	sth	%g2, [%o0]
	sub	%o2, 2, %o2
	ba,pt	%xcc, 3f
	 add	%o0, 2, %o0

__copy_from_user:	/* %o0=dst %o1=src %o2=len */
	wr	%g0, ASI_S, %asi
	xor	%o0, %o1, %o4
1:
	andcc	%o4, 3, %o5
2:
	bne,pn	%icc, cannot_optimize
	 cmp	%o2, 15

	bleu,pn	%xcc, short_aligned_end
	 andcc	%o1, 3, %g0

	bne,pn	%icc, dword_align
3:
	 andcc	%o1, 4, %g0

	be,pt	%icc, 2f
	 mov	%o2, %g1

	EXO2(lda [%o1] %asi, %o4,#)
	sub	%g1, 4, %g1
	st	%o4, [%o0]
	add	%o1, 4, %o1
	add	%o0, 4, %o0
2:
	andcc	%g1, 0xffffffffffffff80, %g7
	be,pn	%xcc, 3f
	 andcc	%o0, 4, %g0

	be,pn	%icc, ldd_std + 4
5:
	MOVE_BIGCHUNK(o1, o0, 0x00, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x20, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x40, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x60, o2, o3, o4, o5, g2, g3, g4, g5)
80:
	EXT(5b, 80b, 50f,#)
	subcc	%g7, 128, %g7
	add	%o1, 128, %o1
	bne,pt	%xcc, 5b
	 add	%o0, 128, %o0
3:
	andcc	%g1, 0x70, %g7
	be,pn	%icc, copy_user_table_end
	 andcc	%g1, 8, %g0
100:
	rd	%pc, %o5
	srl	%g7, 1, %o4
	add	%g7, %o4, %o4
	add	%o1, %g7, %o1
	sub	%o5, %o4, %o5
	jmpl	%o5 + (copy_user_table_end - 100b), %g0
	 add	%o0, %g7, %o0

copy_user_table:
	MOVE_LASTCHUNK(o1, o0, 0x60, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x50, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x40, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x30, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x20, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x10, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x00, g2, g3, g4, g5)
copy_user_table_end:
	EXT(copy_user_table, copy_user_table_end, 51f,#)
	be,pt	%icc, copy_user_last7
	 andcc	%g1, 4, %g0

	EX(ldda	[%o1] %asi, %g2, and %g1, 0xf,#)
	add	%o0, 8, %o0
	add	%o1, 8, %o1
	st	%g2, [%o0 - 0x08]
	st	%g3, [%o0 - 0x04]
copy_user_last7:
	be,pn	%icc, 1f
	 andcc	%g1, 2, %g0

	EX(lda	[%o1] %asi, %g2, and %g1, 7,#)
	add	%o1, 4, %o1
	st	%g2, [%o0]
	add	%o0, 4, %o0
1:
	be,pn	%icc, 1f
	 andcc	%g1, 1, %g0

	EX(lduha [%o1] %asi, %g2, and %g1, 3,#)
	add	%o1, 2, %o1
	sth	%g2, [%o0]
	add	%o0, 2, %o0
1:
	be,pn	%icc, 1f
	 nop

	EX(lduba [%o1] %asi, %g2, add %g0, 1,#)
	stb	%g2, [%o0]
1:
	PRE_RETL
	retl
 	 clr	%o0

ldd_std:
	MOVE_BIGALIGNCHUNK(o1, o0, 0x00, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGALIGNCHUNK(o1, o0, 0x20, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGALIGNCHUNK(o1, o0, 0x40, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGALIGNCHUNK(o1, o0, 0x60, o2, o3, o4, o5, g2, g3, g4, g5)
81:
	EXT(ldd_std, 81b, 52f,#)
	subcc	%g7, 128, %g7
	add	%o1, 128, %o1
	bne,pt	%xcc, ldd_std
	add	%o0, 128, %o0

	andcc	%g1, 0x70, %g7
	be,pn	%icc, copy_user_table_end
	 andcc	%g1, 8, %g0
101:
	rd	%pc, %o5
	srl	%g7, 1, %o4
	add	%g7, %o4, %o4
	add	%o1, %g7, %o1
	sub	%o5, %o4, %o5
	jmpl	%o5 + (copy_user_table_end - 101b), %g0
	 add	%o0, %g7, %o0

cannot_optimize:
	bleu	short_end
	 cmp	%o5, 2

	bne	byte_chunk
	 and	%o2, 0xfffffffffffffff0, %o3
	 
	andcc	%o1, 1, %g0
	be	10f
	 nop

	EXO2(lduba [%o1] %asi, %g2,#)
	add	%o1, 1, %o1
	stb	%g2, [%o0]
	sub	%o2, 1, %o2
	andcc	%o2, 0xfffffffffffffff0, %o3
	be	short_end
	 add	%o0, 1, %o0
10:
	MOVE_HALFCHUNK(o1, o0, 0x00, g2, g3, g4, g5)
	MOVE_HALFCHUNK(o1, o0, 0x08, g2, g3, g4, g5)
82:
	EXT(10b, 82b, 53f,#)
	subcc	%o3, 0x10, %o3
	add	%o1, 0x10, %o1
	bne	10b
	 add	%o0, 0x10, %o0
	ba,pt	%xcc, 2f
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
83:
	EXT(byte_chunk, 83b, 54f,#)
	subcc	%o3, 0x10, %o3
	add	%o1, 0x10, %o1
	bne,pt	%xcc, byte_chunk
	 add	%o0, 0x10, %o0

short_end:
	and	%o2, 0xe, %o3
2:
	rd	%pc, %o5
	sll	%o3, 3, %o4
	add	%o0, %o3, %o0
	sub	%o5, %o4, %o5
	add	%o1, %o3, %o1
	jmpl	%o5 + (short_table_end - 2b), %g0
	 andcc	%o2, 1, %g0
84:
	MOVE_SHORTCHUNK(o1, o0, 0x0c, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x0a, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x08, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x06, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x04, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x02, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x00, g2, g3)
short_table_end:
	EXT(84b, short_table_end, 55f,#)
	be	1f
	 nop
	EX(lduba [%o1] %asi, %g2, add %g0, 1,#)
	stb	%g2, [%o0]
1:
	PRE_RETL
	retl
 	 clr	%o0

short_aligned_end:
	bne	short_end
	 andcc	%o2, 8, %g0

	be	1f
	 andcc	%o2, 4, %g0

	EXO2(lda [%o1 + 0x00] %asi, %g2,#)
	EX(lda	[%o1 + 0x04] %asi, %g3, sub %o2, 4,#)
	add	%o1, 8, %o1
	st	%g2, [%o0 + 0x00]
	st	%g3, [%o0 + 0x04]
	add	%o0, 8, %o0
1:
	ba,pt	%xcc, copy_user_last7
	 mov	%o2, %g1

	.section .fixup,#alloc,#execinstr
	.align	4
97:
	PRE_RETL
	retl
	 mov	%o2, %o0
/* exception routine sets %g2 to (broken_insn - first_insn)>>2 */
50:
/* This magic counts how many bytes are left when crash in MOVE_BIGCHUNK
 * happens. This is derived from the amount ldd reads, st stores, etc.
 * x = g2 % 12;
 * o0 = g1 + g7 - ((g2 / 12) * 32 + (x < 4) ? x * 8 : (x - 4) * 4)
 */
	cmp	%g2, 12
	bcs	1f
	 cmp	%g2, 24
	bcs	2f
	 cmp	%g2, 36
	bcs	3f
	 nop
	sub	%g2, 12, %g2
	sub	%g7, 32, %g7
3:
	sub	%g2, 12, %g2
	sub	%g7, 32, %g7
2:
	sub	%g2, 12, %g2
	sub	%g7, 32, %g7
1:
	cmp	%g2, 4
	bcs,a	1f
	 sll	%g2, 3, %g2
	sub	%g2, 4, %g2
	sll	%g2, 2, %g2
1:
	and	%g1, 0x7f, %o0
	add	%o0, %g7, %o0
	PRE_RETL
	retl
	 sub	%o0, %g2, %o0
51:
/* i = 41 - g2; j = i % 6;
 * o0 = (g1 & 15) + (i / 6) * 16 + (j < 4) ? (j + 1) * 4 : (j - 3) * 8;	
 */
	neg	%g2
	and	%g1, 0xf, %g1
	add	%g2, 41, %g2
1:
	cmp	%g2, 6
	bcs,a	2f
	 cmp	%g2, 4
	add	%g1, 16, %g1
	b	1b
	 sub	%g2, 6, %g2
2:
	bcs,a	3f
	 inc	%g2
	sub	%g2, 3, %g2
	b	2f
	 sll	%g2, 3, %g2
3:
	sll	%g2, 2, %g2
2:
	PRE_RETL
	retl
	 add	%g1, %g2, %o0
52:
/* o0 = g1 + g7 - (g2 / 8) * 32 + (x & 3) * 8 */
	and	%g2, 0xfffffffffffffff8, %g4
	and	%g2, 3, %g2
	sll	%g4, 2, %g4
	sll	%g2, 3, %g2
	add	%g2, %g4, %g2
	b,a	1b
53:
/* o0 = o3 + (o2 & 15) - (g2 & 8) - (g2 & 3) * 2 */
	and	%g2, 3, %g4
	and	%g2, 0xfffffffffffffff8, %g2
	sll	%g4, 1, %g4
	add	%g2, %g4, %g2
	and	%o2, 0xf, %o0
	add	%o0, %o3, %o0
	PRE_RETL
	retl
	 sub	%o0, %g2, %o0
54:
/* o0 = o3 + (o2 & 15) - (g2 / 4) * 2 - (g2 & 1) */
	srl	%g2, 2, %o4
	and	%g2, 1, %o1
	sll	%o4, 1, %o4
	and	%o2, 0xf, %o2
	sub	%o3, %o1, %o3
	sub	%o2, %o4, %o2
	PRE_RETL
	retl
	 add	%o2, %o3, %o0
55:
/* o0 = (o2 & 1) + (27 - g2)/4 * 2 + ((27 - g2) & 1) */
	neg	%g2
	and	%o2, 1, %o2
	add	%g2, 27, %g2
	srl	%g2, 2, %o1
	and	%g2, 1, %g2
	sll	%o1, 1, %o1
	add	%o2, %g2, %o0
	PRE_RETL
	retl
	 add	%o0, %o1, %o0

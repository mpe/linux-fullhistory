/* memcpy.S: Sparc optimized memcpy, bcopy and memmove code
 * Hand optimized from GNU libc's memcpy, bcopy and memmove
 * for UltraSparc
 * Copyright (C) 1991,1996 Free Software Foundation
 * Copyright (C) 1995 Linus Torvalds (Linus.Torvalds@helsinki.fi)
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <asm/asi.h>
#include <asm/head.h>

#ifdef __KERNEL__

#define FUNC(x) 		\
	.globl	x;		\
	.type	x,@function;	\
	.align	4;		\
x:

#define FASTER_ALIGNED

/* In kernel these functions don't return a value.
 * One should use macros in asm/string.h for that purpose.
 * We return 0, so that bugs are more apparent.
 */
#define SETUP_RETL
#define PRE_RETL	sethi	%uhi(KERNBASE), %g4; clr %o0
#define RETL_INSN	sllx	%g4, 32, %g4

#else

/* libc */

#define FASTER_ALIGNED

#ifdef DEBUG
#define FUNC(x) 			\
	.globl	jj##x##1;		\
	.type	jj##x##1,@function;	\
	.align	4;			\
jj##x##1:
#else
#include "DEFS.h"
#endif

#define SETUP_RETL	mov	%o0, %g6
#define PRE_RETL
#define RETL_INSN	mov	%g6, %o0

#endif

#define MOVE_BIGCHUNK(src, dst, offset, t0, t1, t2, t3, t4, t5, t6, t7) 	\
	ldd	[%src + offset + 0x00], %t0; 					\
	ldd	[%src + offset + 0x08], %t2; 					\
	ldd	[%src + offset + 0x10], %t4; 					\
	ldd	[%src + offset + 0x18], %t6; 					\
	stw	%t0, [%dst + offset + 0x00]; 					\
	stw	%t1, [%dst + offset + 0x04]; 					\
	stw	%t2, [%dst + offset + 0x08]; 					\
	stw	%t3, [%dst + offset + 0x0c]; 					\
	stw	%t4, [%dst + offset + 0x10]; 					\
	stw	%t5, [%dst + offset + 0x14]; 					\
	stw	%t6, [%dst + offset + 0x18]; 					\
	stw	%t7, [%dst + offset + 0x1c];

#define MOVE_BIGALIGNCHUNK(src, dst, offset, t0, t1, t2, t3, t4, t5, t6, t7)	\
	ldx	[%src + offset + 0x00], %t0; 					\
	ldx	[%src + offset + 0x08], %t1; 					\
	ldx	[%src + offset + 0x10], %t2; 					\
	ldx	[%src + offset + 0x18], %t3; 					\
	ldx	[%src + offset + 0x20], %t4; 					\
	ldx	[%src + offset + 0x28], %t5; 					\
	ldx	[%src + offset + 0x30], %t6; 					\
	ldx	[%src + offset + 0x38], %t7; 					\
	stx	%t0, [%dst + offset + 0x00]; 					\
	stx	%t1, [%dst + offset + 0x08]; 					\
	stx	%t2, [%dst + offset + 0x10]; 					\
	stx	%t3, [%dst + offset + 0x18]; 					\
	stx	%t4, [%dst + offset + 0x20]; 					\
	stx	%t5, [%dst + offset + 0x28]; 					\
	stx	%t6, [%dst + offset + 0x30]; 					\
	stx	%t7, [%dst + offset + 0x38];

#define MOVE_LASTCHUNK(src, dst, offset, t0, t1, t2, t3)	\
	ldd	[%src - offset - 0x10], %t0;			\
	ldd	[%src - offset - 0x08], %t2; 			\
	stw	%t0, [%dst - offset - 0x10]; 			\
	stw	%t1, [%dst - offset - 0x0c]; 			\
	stw	%t2, [%dst - offset - 0x08]; 			\
	stw	%t3, [%dst - offset - 0x04];

#define MOVE_LASTALIGNCHUNK(src, dst, offset, t0, t1) 		\
	ldx	[%src - offset - 0x10], %t0; 			\
	ldx	[%src - offset - 0x08], %t1; 			\
	stx	%t0, [%dst - offset - 0x10]; 			\
	stx	%t1, [%dst - offset - 0x08];

#define MOVE_SHORTCHUNK(src, dst, offset, t0, t1) 		\
	ldub	[%src - offset - 0x02], %t0; 			\
	ldub	[%src - offset - 0x01], %t1; 			\
	stb	%t0, [%dst - offset - 0x02]; 			\
	stb	%t1, [%dst - offset - 0x01];

	.text
	.align	4

FUNC(bcopy)

	mov		%o0, %o3
	mov		%o1, %o0
	mov		%o3, %o1
	brgez,a,pt	%o2, 1f
	 cmp		%o0, %o1

	retl
	 nop		! Only bcopy returns here and it retuns void...

#ifdef __KERNEL__
FUNC(amemmove)
FUNC(__memmove)
#endif
FUNC(memmove)

	cmp		%o0, %o1
1:
	SETUP_RETL
	bleu,pt		%xcc, 9f
	 sub		%o0, %o1, %o4

	add		%o1, %o2, %o3
	cmp		%o3, %o0
	bleu,pt		%xcc, 0f
	 andcc		%o4, 3, %o5

	add		%o1, %o2, %o1
	add		%o0, %o2, %o0
	sub		%o1, 1, %o1
	sub		%o0, 1, %o0
	
1:
	ldub		[%o1], %o4
	subcc		%o2, 1, %o2
	sub		%o1, 1, %o1
	stb		%o4, [%o0]
	bne,pt		%icc, 1b
	 sub		%o0, 1, %o0

	PRE_RETL
	retl
	 RETL_INSN

#ifdef __KERNEL__
FUNC(__memcpy)
#endif
FUNC(memcpy)	/* %o0=dst %o1=src %o2=len */

	sub		%o0, %o1, %o4
	SETUP_RETL
9:
	andcc		%o4, 3, %o5
0:
	bne,pn		%icc, 86f
	 cmp		%o2, 15

	bleu,pn		%xcc, 90f
	 andcc		%o1, 3, %g0

	be,a,pt		%icc, 3f		! check if we need to align
	 andcc		%o1, 4, %g0

	andcc		%o1, 1, %g0
	be,pn		%icc, 4f
	 andcc		%o1, 2, %g0

	ldub		[%o1], %g2
	add		%o1, 1, %o1
	sub		%o2, 1, %o2
	stb		%g2, [%o0]
	bne,pn		%icc, 5f
	 add		%o0, 1, %o0
4:
	lduh		[%o1], %g2
	add		%o1, 2, %o1
	sub		%o2, 2, %o2
	sth		%g2, [%o0]
	add		%o0, 2, %o0
5:
	andcc		%o1, 4, %g0
3:
	be,pn		%icc, 2f
	 mov		%o2, %g1

	lduw		[%o1], %o4
	sub		%g1, 4, %g1
	stw		%o4, [%o0]
	add		%o1, 4, %o1
	add		%o0, 4, %o0
2:
	andcc		%g1, -128, %g7
	be,pn		%xcc, 3f
	 andcc		%o0, 4, %g0

	be,a,pn		%icc, 82f + 4
	 ldx		[%o1], %o2
5:
	MOVE_BIGCHUNK(o1, o0, 0x00, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x20, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x40, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x60, o2, o3, o4, o5, g2, g3, g4, g5)
	subcc		%g7, 128, %g7
	add		%o1, 128, %o1
	bne,pt		%xcc, 5b
	 add		%o0, 128, %o0
3:
	andcc		%g1, 0x70, %g7
	be,pn		%icc, 80f
	 andcc		%g1, 8, %g0
79:
	rd		%pc, %o5
	srl		%g7, 1, %o4
	add		%g7, %o4, %o4
	add		%o1, %g7, %o1
	sub		%o5, %o4, %o5
	jmpl		%o5 + %lo(80f-79b), %g0
	 add		%o0, %g7, %o0

	MOVE_LASTCHUNK(o1, o0, 0x60, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x50, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x40, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x30, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x20, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x10, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x00, g2, g3, g4, g5)

80:	/* memcpy_table_end */
	be,pt		%icc, 81f
	 andcc		%g1, 4, %g0

	ldd		[%o1], %g2
	add		%o0, 8, %o0
	stw		%g2, [%o0 - 0x08]
	add		%o1, 8, %o1
	stw		%g3, [%o0 - 0x04]

81:	/* memcpy_last7 */

	be,pt		%icc, 1f
	 andcc		%g1, 2, %g0

	lduw		[%o1], %g2
	add		%o1, 4, %o1
	stw		%g2, [%o0]
	add		%o0, 4, %o0
1:
	be,pt		%icc, 1f
	 andcc		%g1, 1, %g0

	lduh		[%o1], %g2
	add		%o1, 2, %o1
	sth		%g2, [%o0]
	add		%o0, 2, %o0
1:
	be,pt		%icc, 1f
	 nop

	ldub		[%o1], %g2
	stb		%g2, [%o0]
1:
	PRE_RETL
	retl
 	 RETL_INSN

82:	/* ldx_stx */
	MOVE_BIGALIGNCHUNK(o1, o0, 0x00, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGALIGNCHUNK(o1, o0, 0x40, o2, o3, o4, o5, g2, g3, g4, g5)
	subcc		%g7, 128, %g7
	add		%o1, 128, %o1
	bne,pt		%xcc, 82b
	 add		%o0, 128, %o0

#ifndef FASTER_ALIGNED

	andcc		%g1, 0x70, %g7
	be,pn		%icc, 80b
	 andcc		%g1, 8, %g0
83:
	rd		%pc, %o5
	srl		%g7, 1, %o4
	add		%g7, %o4, %o4
	add		%o1, %g7, %o1
	sub		%o5, %o4, %o5
	jmpl		%o5 + %lo(80b - 83b), %g0
	 add		%o0, %g7, %o0

#else /* FASTER_ALIGNED */

	andcc		%g1, 0x70, %g7
	be,pn		%icc, 84f
	 andcc		%g1, 8, %g0
83:
	rd		%pc, %o5
	add		%o1, %g7, %o1
	sub		%o5, %g7, %o5
	jmpl		%o5 + %lo(84f - 83b), %g0
	 add		%o0, %g7, %o0

	MOVE_LASTALIGNCHUNK(o1, o0, 0x60, g2, g3)
	MOVE_LASTALIGNCHUNK(o1, o0, 0x50, g2, g3)
	MOVE_LASTALIGNCHUNK(o1, o0, 0x40, g2, g3)
	MOVE_LASTALIGNCHUNK(o1, o0, 0x30, g2, g3)
	MOVE_LASTALIGNCHUNK(o1, o0, 0x20, g2, g3)
	MOVE_LASTALIGNCHUNK(o1, o0, 0x10, g2, g3)
	MOVE_LASTALIGNCHUNK(o1, o0, 0x00, g2, g3)

84:	/* amemcpy_table_end */
	be,pt		%icc, 85f
	 andcc		%g1, 4, %g0

	ldx		[%o1], %g2
	add		%o1, 8, %o1
	stx		%g2, [%o0]
	add		%o0, 8, %o0
85:	/* amemcpy_last7 */
	be,pt		%icc, 1f
	 andcc		%g1, 2, %g0

	lduw		[%o1], %g2
	add		%o1, 4, %o1
	stw		%g2, [%o0]
	add		%o0, 4, %o0
1:
	be,pt		%icc, 1f
	 andcc		%g1, 1, %g0

	lduh		[%o1], %g2
	add		%o1, 2, %o1
	sth		%g2, [%o0]
	add		%o0, 2, %o0
1:
	be,pt		%icc, 1f
	 nop

	ldub		[%o1], %g2
	stb		%g2, [%o0]
1:
	PRE_RETL
	retl
 	 RETL_INSN

#endif /* FASTER_ALIGNED */

86:	/* non_aligned */
	cmp		%o2, 15
	bleu,pn		%xcc, 88f

	 andcc		%o0, 3, %g0
	be,pn		%icc, 61f
	 andcc		%o0, 1, %g0
	be,pn		%icc, 60f
	 andcc		%o0, 2, %g0

	ldub		[%o1], %g5
	add		%o1, 1, %o1
	stb		%g5, [%o0]
	sub		%o2, 1, %o2
	bne,pn		%icc, 61f
	 add		%o0, 1, %o0
60:
	ldub		[%o1], %g3
	add		%o1, 2, %o1
	stb		%g3, [%o0]
	sub		%o2, 2, %o2
	ldub		[%o1 - 1], %g3
	add		%o0, 2, %o0
	stb		%g3, [%o0 - 1]
61:
	and		%o1, 3, %g2
	and		%o2, 0xc, %g3
	and		%o1, -4, %o1
	cmp		%g3, 4
	sll		%g2, 3, %g4
	mov		32, %g2
	be,pn		%icc, 4f
	 sub		%g2, %g4, %g7
	
	blu,pn		%icc, 3f
	 cmp		%g3, 0x8

	be,pn		%icc, 2f
	 srl		%o2, 2, %g3

	lduw		[%o1], %o3
	add		%o0, -8, %o0
	lduw		[%o1 + 4], %o4
	ba,pt		%xcc, 8f
	 add		%g3, 1, %g3
2:
	lduw		[%o1], %o4
	add		%o0, -12, %o0
	lduw		[%o1 + 4], %o5
	add		%g3, 2, %g3
	ba,pt		%xcc, 9f
	 add		%o1, -4, %o1
3:
	lduw		[%o1], %g1
	add		%o0, -4, %o0
	lduw		[%o1 + 4], %o3
	srl		%o2, 2, %g3
	ba,pt		%xcc, 7f
	 add		%o1, 4, %o1
4:
	lduw		[%o1], %o5
	cmp		%o2, 7
	lduw		[%o1 + 4], %g1
	srl		%o2, 2, %g3
	bleu,pn		%xcc, 10f
	 add		%o1, 8, %o1

	lduw		[%o1], %o3
	add		%g3, -1, %g3
5:
	sll		%o5, %g4, %g2
	srl		%g1, %g7, %g5
	or		%g2, %g5, %g2
	stw		%g2, [%o0]
7:
	lduw		[%o1 + 4], %o4
	sll		%g1, %g4, %g2
	srl		%o3, %g7, %g5
	or		%g2, %g5, %g2
	stw		%g2, [%o0 + 4]
8:
	lduw		[%o1 + 8], %o5
	sll		%o3, %g4, %g2
	srl		%o4, %g7, %g5
	or		%g2, %g5, %g2
	stw		%g2, [%o0 + 8]
9:
	lduw		[%o1 + 12], %g1
	sll		%o4, %g4, %g2
	srl		%o5, %g7, %g5
	addcc		%g3, -4, %g3
	or		%g2, %g5, %g2
	add		%o1, 16, %o1
	stw		%g2, [%o0 + 12]
	add		%o0, 16, %o0
	bne,a,pt	%xcc, 5b
	 lduw		[%o1], %o3
10:
	sll		%o5, %g4, %g2
	srl		%g1, %g7, %g5
	srl		%g7, 3, %g3
	or		%g2, %g5, %g2
	sub		%o1, %g3, %o1
	andcc		%o2, 2, %g0
	stw		%g2, [%o0]
	be,pt		%icc, 1f
	 andcc		%o2, 1, %g0

	ldub		[%o1], %g2
	add		%o1, 2, %o1
	stb		%g2, [%o0 + 4]
	add		%o0, 2, %o0
	ldub		[%o1 - 1], %g2
	stb		%g2, [%o0 + 3]
1:
	be,pt		%icc, 1f
	 nop

	ldub		[%o1], %g2
	stb		%g2, [%o0 + 4]
1:
	PRE_RETL
	retl
	 RETL_INSN

88:	/* short_end */

	and		%o2, 0xe, %o3
20:
	rd		%pc, %o5
	sll		%o3, 3, %o4
	add		%o0, %o3, %o0
	sub		%o5, %o4, %o5
	add		%o1, %o3, %o1
	jmpl		%o5 + %lo(89f - 20b), %g0
	 andcc		%o2, 1, %g0

	MOVE_SHORTCHUNK(o1, o0, 0x0c, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x0a, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x08, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x06, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x04, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x02, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x00, g2, g3)

89:	/* short_table_end */

	be,pt		%icc, 1f
	 nop

	ldub		[%o1], %g2
	stb		%g2, [%o0]
1:
	PRE_RETL
	retl
 	 RETL_INSN

90:	/* short_aligned_end */
	bne,pn		%xcc, 88b
	 andcc		%o2, 8, %g0

	be,pt		%icc, 1f
	 andcc		%o2, 4, %g0

	lduw		[%o1 + 0x00], %g2
	lduw		[%o1 + 0x04], %g3
	add		%o1, 8, %o1
	stw		%g2, [%o0 + 0x00]
	stw		%g3, [%o0 + 0x04]
	add		%o0, 8, %o0
1:
	ba,pt		%xcc, 81b
	 mov		%o2, %g1

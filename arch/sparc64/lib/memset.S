/* linux/arch/sparc64/lib/memset.S: Sparc optimized memset, bzero and clear_user code
 * Copyright (C) 1991,1996 Free Software Foundation
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * Returns 0, if ok, and number of bytes not yet set if exception
 * occurs and we were called as clear_user.
 */

#include <asm/asi.h>
#include <asm/ptrace.h>

#define EX(x,y,a,b,z) 				\
98: 	x,y;					\
	.section .fixup,z##alloc,z##execinstr;	\
	.align	4;				\
99:	ba,pt	%xcc, 30f;			\
	 a, b, %o0;				\
	.section __ex_table,z##alloc;		\
	.align	4;				\
	.word	98b, 99b;			\
	.text;					\
	.align	4

#define EXT(start,end,handler,z) 		\
	.section __ex_table,z##alloc;		\
	.align	4;				\
	.word	start, 0, end, handler;		\
	.text;					\
	.align	4

/* Please don't change these macros, unless you change the logic
 * in the .fixup section below as well.
 * Store 64 bytes at (BASE + OFFSET) using value SOURCE. */
#define ZERO_BIG_BLOCK(base, offset, source)    \
	stxa	source, [base + offset + 0x00] %asi; \
	stxa	source, [base + offset + 0x08] %asi; \
	stxa	source, [base + offset + 0x10] %asi; \
	stxa	source, [base + offset + 0x18] %asi; \
	stxa	source, [base + offset + 0x20] %asi; \
	stxa	source, [base + offset + 0x28] %asi; \
	stxa	source, [base + offset + 0x30] %asi; \
	stxa	source, [base + offset + 0x38] %asi;

#define ZERO_LAST_BLOCKS(base, offset, source)	\
	stxa	source, [base - offset - 0x38] %asi; \
	stxa	source, [base - offset - 0x30] %asi; \
	stxa	source, [base - offset - 0x28] %asi; \
	stxa	source, [base - offset - 0x20] %asi; \
	stxa	source, [base - offset - 0x18] %asi; \
	stxa	source, [base - offset - 0x10] %asi; \
	stxa	source, [base - offset - 0x08] %asi; \
	stxa	source, [base - offset - 0x00] %asi;

	.text
	.align 4

	.globl	__bzero, __memset, __bzero_noasi
	.globl	memset, __memset_start, __memset_end
__memset_start:
__memset:
memset:
	and	%o1, 0xff, %g3
	sll	%g3, 8, %g2
	or	%g3, %g2, %g3
	sll	%g3, 16, %g2
	or	%g3, %g2, %g3
	mov	%o2, %o1
	wr	%g0, ASI_P, %asi
	sllx	%g3, 32, %g2
	ba,pt	%xcc, 1f
	 or	%g3, %g2, %g3
__bzero:
	wr	%g0, ASI_P, %asi
__bzero_noasi:
	mov	%g0, %g3
1:
	cmp	%o1, 7
	bleu,pn	%xcc, 7f
	 andcc	%o0, 3, %o2

	be,a,pt	%icc, 4f
	 andcc	%o0, 4, %g0

	cmp	%o2, 3
	be,pn	%icc, 2f
	 EX(stba	%g3, [%o0] %asi, sub %o1, 0,#)

	cmp	%o2, 2
	be,pt	%icc, 2f
	 EX(stba	%g3, [%o0 + 0x01] %asi, sub %o1, 1,#)

	EX(stba	%g3, [%o0 + 0x02] %asi, sub %o1, 2,#)
2:
	sub	%o2, 4, %o2
	sub	%o0, %o2, %o0
	add	%o1, %o2, %o1
	andcc	%o0, 4, %g0
4:
	be,a,pt	%icc, 2f
	 andncc	%o1, 0x7f, %o3

	EX(sta	%g3, [%o0] %asi, sub %o1, 0,#)
	sub	%o1, 4, %o1
	add	%o0, 4, %o0
	andncc	%o1, 0x7f, %o3		! Now everything is 8 aligned and o1 is len to run
2:
	be,pn	%xcc, 9f
	 andcc	%o1, 0x78, %o2
10:
	ZERO_BIG_BLOCK(%o0, 0x00, %g3)
	subcc	%o3, 128, %o3
	ZERO_BIG_BLOCK(%o0, 0x40, %g3)
11:
	EXT(10b, 11b, 20f,#)
	bne,pt	%xcc, 10b
	 add	%o0, 128, %o0

	tst	%o2
9:
	be,pn	%xcc, 13f
	 andcc	%o1, 7, %o1
14:
	rd	%pc, %o4
	srl	%o2, 1, %o3
	sub	%o4, %o3, %o4
	jmpl	%o4 + (13f - 14b), %g0
	 add	%o0, %o2, %o0
12:
	ZERO_LAST_BLOCKS(%o0, 0x48, %g3)
	ZERO_LAST_BLOCKS(%o0, 0x08, %g3)
13:
	be,pn	%icc, 8f
	 andcc	%o1, 4, %g0

	be,pn	%icc, 1f
	 andcc	%o1, 2, %g0

	EX(sta	%g3, [%o0] %asi, and %o1, 7,#)
	add	%o0, 4, %o0
1:
	be,pn	%icc, 1f
	 andcc	%o1, 1, %g0

	EX(stha	%g3, [%o0] %asi, and %o1, 3,#)
	add	%o0, 2, %o0
1:
	bne,a,pn %icc, 8f
	 EX(stba	%g3, [%o0] %asi, and %o1, 1,#)
8:
	retl
	 clr	%o0
7:
	be,pn	%icc, 13b
	 orcc	%o1, 0, %g0

	be,pn	%icc, 0f
8:
	 add	%o0, 1, %o0
	subcc	%o1, 1, %o1
	bne,a,pt %icc, 8b
	 EX(stba	%g3, [%o0 - 1] %asi, add %o1, 1,#)
0:
	retl
	 clr	%o0
__memset_end:

	.section .fixup,#alloc,#execinstr
	.align	4
20:
	cmp	%g2, 8
	bleu,pn	%xcc, 1f
	 and	%o1, 0x7f, %o1
	sub	%g2, 9, %g2
	add	%o3, 64, %o3
1:
	sll	%g2, 3, %g2
	add	%o3, %o1, %o0
	ba,pt	%xcc, 30f
	 sub	%o0, %g2, %o0
21:
	mov	8, %o0
	and	%o1, 7, %o1
	sub	%o0, %g2, %o0
	sll	%o0, 3, %o0
	ba,pt	%xcc, 30f
	 add	%o0, %o1, %o0
30:
/* %o4 is faulting address, %o5 is %pc where fault occured */
	save	%sp, -160, %sp
	mov	%i5, %o0
	mov	%i7, %o1
	call	lookup_fault
	 mov	%i4, %o2
	ret
	 restore

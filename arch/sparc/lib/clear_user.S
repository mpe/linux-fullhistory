/* linux/arch/sparc/lib/clear_user.S: Sparc optimized clear_user code
 *
 * Zero %o1 bytes at user %o0, handling exceptions as we go.
 * Returns 0 if successfull, # of bytes still not cleared otherwise
 *
 * Copyright (C) 1991,1996 Free Software Foundation
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/cprefix.h>
#include <asm/ptrace.h>

#define EX(x,y,a,b,z) 				\
98: 	x,y;					\
	.section .fixup,z##alloc,z##execinstr;	\
	.align	4;				\
99:	retl;					\
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

/* Please don't change these macros, unless you change the login
 * in the .fixup section below as well */
/* Store 64 bytes at (BASE + OFFSET) using value SOURCE. */
#define ZERO_BIG_BLOCK(base, offset, source)    \
	std	source, [base + offset + 0x00]; \
	std	source, [base + offset + 0x08]; \
	std	source, [base + offset + 0x10]; \
	std	source, [base + offset + 0x18]; \
	std	source, [base + offset + 0x20]; \
	std	source, [base + offset + 0x28]; \
	std	source, [base + offset + 0x30]; \
	std	source, [base + offset + 0x38];

#define ZERO_LAST_BLOCKS(base, offset, source)	\
	std	source, [base - offset - 0x38]; \
	std	source, [base - offset - 0x30]; \
	std	source, [base - offset - 0x28]; \
	std	source, [base - offset - 0x20]; \
	std	source, [base - offset - 0x18]; \
	std	source, [base - offset - 0x10]; \
	std	source, [base - offset - 0x08]; \
	std	source, [base - offset - 0x00];

	.text
	.align 4

	.globl	C_LABEL(__clear_user)
3:
	cmp	%o2, 3
	be	2f
	 EX(stb	%g0, [%o0], sub %o1, 0,#)

	cmp	%o2, 2
	be	2f
	 EX(stb	%g0, [%o0 + 0x01], sub %o1, 1,#)

	EX(stb	%g0, [%o0 + 0x02], sub %o1, 2,#)
2:
	sub	%o2, 4, %o2
	add	%o1, %o2, %o1
	b	4f
	 sub	%o0, %o2, %o0

C_LABEL(__clear_user):
	cmp	%o1, 7
	bleu	7f
	 andcc	%o0, 3, %o2
	bne	3b
4:
	 andcc	%o0, 4, %g0

	be	2f
	 mov	%g0, %g1

	EX(st	%g0, [%o0], sub %o1, 0,#)
	sub	%o1, 4, %o1
	add	%o0, 4, %o0
2:
	andcc	%o1, 0xffffff80, %o3	! Now everything is 8 aligned and o1 is len to run
	be	9f
	 andcc	%o1, 0x78, %o2
10:
	ZERO_BIG_BLOCK(%o0, 0x00, %g0)
	subcc	%o3, 128, %o3
	ZERO_BIG_BLOCK(%o0, 0x40, %g0)
11:
	EXT(10b, 11b, 20f,#)
	bne	10b
	 add	%o0, 128, %o0

	orcc	%o2, %g0, %g0
9:
	be	13f
	 andcc	%o1, 7, %o1

	srl	%o2, 1, %o3
	set	13f, %o4
	sub	%o4, %o3, %o4
	jmp	%o4
	 add	%o0, %o2, %o0

12:
	ZERO_LAST_BLOCKS(%o0, 0x48, %g0)
	ZERO_LAST_BLOCKS(%o0, 0x08, %g0)
13:
	EXT(12b, 13b, 21f,#)
	be	8f
	 andcc	%o1, 4, %g0

	be	1f
	 andcc	%o1, 2, %g0

	EX(st	%g0, [%o0], and %o1, 7,#)
	add	%o0, 4, %o0
1:
	be	1f
	 andcc	%o1, 1, %g0

	EX(sth	%g0, [%o0], and %o1, 3,#)
	add	%o0, 2, %o0
1:
	bne,a	8f
	 EX(stb	%g0, [%o0], add %g0, 1,#)
8:
	retl
	 clr	%o0
7:
	be	13b
	 orcc	%o1, 0, %g0

	andcc	%o1, 4, %g0
	be	1f
	 andcc	%o1, 2, %g0

	EX(stb	%g0, [%o0 + 0], sub %o1, 0,#)
	EX(stb	%g0, [%o0 + 1], sub %o1, 1,#)
	EX(stb	%g0, [%o0 + 2], sub %o1, 2,#)
	EX(stb	%g0, [%o0 + 3], sub %o1, 3,#)
	add	%o0, 4, %o0
1:
	be	1f
	 andcc	%o1, 1, %o2

	EX(stb	%g0, [%o0 + 0], add %o2, 2,#)
	EX(stb	%g0, [%o0 + 1], add %o2, 1,#)
	add	%o0, 2, %o0
1:
	bne,a	8b
	 EX(stb	%g0, [%o0], add %g0, 1,#)

	retl
	 clr	%o0

	.section .fixup,#alloc,#execinstr
	.align	4
20:
	cmp	%g2, 8
	bleu	1f
	 and	%o1, 0x7f, %o1
	sub	%g2, 9, %g2
	add	%o3, 64, %o3
1:
	sll	%g2, 3, %g2
	add	%o3, %o1, %o0
	retl
	 sub	%o0, %g2, %o0
21:
	mov	8, %o0
	and	%o1, 7, %o1
	sub	%o0, %g2, %o0
	sll	%o0, 3, %o0
	retl
	 add	%o0, %o1, %o0

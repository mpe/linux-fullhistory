/* $Id: memset.S,v 1.1 1996/12/22 07:42:16 davem Exp $
 * arch/sparc64/lib/memset.S: UltraSparc optimized memset and bzero code
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/asi.h>

	/* Store 64 bytes at (BASE + OFFSET) using value SOURCE. */
#define ZERO_BIG_BLOCK(base, offset, source)    \
	stx	source, [base + offset + 0x00]; \
	stx	source, [base + offset + 0x08]; \
	stx	source, [base + offset + 0x10]; \
	stx	source, [base + offset + 0x18]; \
	stx	source, [base + offset + 0x20]; \
	stx	source, [base + offset + 0x28]; \
	stx	source, [base + offset + 0x30]; \
	stx	source, [base + offset + 0x38];

#define ZERO_LAST_BLOCKS(base, offset, source)	\
	stx	source, [base - offset - 0x38]; \
	stx	source, [base - offset - 0x30]; \
	stx	source, [base - offset - 0x28]; \
	stx	source, [base - offset - 0x20]; \
	stx	source, [base - offset - 0x18]; \
	stx	source, [base - offset - 0x10]; \
	stx	source, [base - offset - 0x08]; \
	stx	source, [base - offset - 0x00];

	.text
	.align 4

	.globl	C_LABEL(__bzero), C_LABEL(__memset), C_LABEL(memset)
C_LABEL(__memset):
C_LABEL(memset):
	and		%o1, 0xff, %g3
	sll		%g3, 8, %g2
	or		%g3, %g2, %g3
	sll		%g3, 16, %g2
	or		%g3, %g2, %g3
	sllx		%g3, 32, %g2
	or		%g3, %g2, %g3
	b		1f
	 mov		%o2, %o1

3:
	cmp		%o2, 3
	be		2f
	 stb		%g3, [%o0]

	cmp		%o2, 2
	be		2f
	 stb		%g3, [%o0 + 0x01]

	stb		%g3, [%o0 + 0x02]
2:
	sub		%o2, 4, %o2
	add		%o1, %o2, %o1
	b		4f
	 sub		%o0, %o2, %o0

C_LABEL(__bzero):
	mov		%g0, %g3
1:
	cmp		%o1, 7
	bleu,pnt	%icc, 7f
	 mov		%o0, %g1

	andcc		%o0, 3, %o2
	bne,pnt		%icc, 3b
4:
	 andcc		%o0, 4, %g0

	be,a,pt		%icc, 2f
	 andcc		%o1, 0xffffff80, %o3	! everything 8 aligned, o1 is len to run

	stw		%g3, [%o0]
	sub		%o1, 4, %o1
	add		%o0, 4, %o0
	andcc		%o1, 0xffffff80, %o3	! everything 8 aligned, o1 is len to run
2:
	be		9f
	 andcc		%o1, 0x78, %o2
4:
	ZERO_BIG_BLOCK(%o0, 0x00, %g2)
	subcc		%o3, 128, %o3
	ZERO_BIG_BLOCK(%o0, 0x40, %g2)
	bne,pt		%icc, 4b
	 add		%o0, 128, %o0

	orcc		%o2, %g0, %g0
9:
	be,pnt		%icc, 6f
	 andcc		%o1, 7, %o1

	srl		%o2, 1, %o3
	set		bzero_table + 64, %o4
	sub		%o4, %o3, %o4
	jmp		%o4
	 add		%o0, %o2, %o0

bzero_table:
	ZERO_LAST_BLOCKS(%o0, 0x48, %g2)
	ZERO_LAST_BLOCKS(%o0, 0x08, %g2)

6:
	be,pt		%icc, 8f
	 andcc		%o1, 4, %g0

	be,pnt		%icc, 1f
	 andcc		%o1, 2, %g0

	stw		%g3, [%o0]
	add		%o0, 4, %o0
1:
	be,pt		%icc, 1f
	 andcc		%o1, 1, %g0

	sth		%g3, [%o0]
	add		%o0, 2, %o0
1:
	bne,a,pnt	%icc, 8f
	 stb		%g3, [%o0]
8:
	retl
	 mov		%g1, %o0

/* Don't care about alignment here. It is highly 
 * unprobable and at most two traps may happen
 */
7:
	ba,pt		%xcc, 6b
	 orcc		%o1, 0, %g0

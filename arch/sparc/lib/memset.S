/* linux/arch/sparc/lib/memset.S: Sparc optimized memset and bzero code
 * Hand optimized from GNU libc's memset
 * Copyright (C) 1991,1996 Free Software Foundation
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/cprefix.h>
#include <asm/ptrace.h>

#define HANDLE_UNALIGNED 1

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

	.globl	C_LABEL(__bzero), C_LABEL(__memset), C_LABEL(memset)
C_LABEL(__memset):
C_LABEL(memset):
	and	%o1, 0xff, %g3
	sll	%g3, 8, %g2
	or	%g3, %g2, %g3
	sll	%g3, 16, %g2
	or	%g3, %g2, %g3
	b	1f
	 mov	%o2, %o1

#if HANDLE_UNALIGNED
/* As this is highly unprobable, we optimize the other case (4 aligned)
 * Define HANDLE_UNALIGNED to 0, if all the alignment work is done by
 * the trap. Then we have to hope nobody will memset something unaligned
 * with large counts, as this would lead to a lot of traps...
 */
3:
	cmp	%o2, 3
	be	2f
	 stb	%g3, [%o0]

	cmp	%o2, 2
	be	2f
	 stb	%g3, [%o0 + 0x01]

	stb	%g3, [%o0 + 0x02]
2:
	sub	%o2, 4, %o2
	add	%o1, %o2, %o1
	b	4f
	 sub	%o0, %o2, %o0
#endif /* HANDLE_UNALIGNED */

C_LABEL(__bzero):
	mov	%g0, %g3
1:
	cmp	%o1, 7
	bleu	7f
	 mov	%o0, %g1

#if HANDLE_UNALIGNED
	andcc	%o0, 3, %o2
	bne	3b
#endif /* HANDLE_UNALIGNED */
4:
	 andcc	%o0, 4, %g0

	be	2f
	 mov	%g3, %g2

	st	%g3, [%o0]
	sub	%o1, 4, %o1
	add	%o0, 4, %o0
2:
	andcc	%o1, 0xffffff80, %o3	! Now everything is 8 aligned and o1 is len to run
	be	9f
	 andcc	%o1, 0x78, %o2
4:
	ZERO_BIG_BLOCK(%o0, 0x00, %g2)
	subcc	%o3, 128, %o3
	ZERO_BIG_BLOCK(%o0, 0x40, %g2)
	bne	4b
	 add	%o0, 128, %o0

	orcc	%o2, %g0, %g0
9:
	be	6f
	 andcc	%o1, 7, %o1

	srl	%o2, 1, %o3
	set	bzero_table + 64, %o4
	sub	%o4, %o3, %o4
	jmp	%o4
	 add	%o0, %o2, %o0

bzero_table:
	ZERO_LAST_BLOCKS(%o0, 0x48, %g2)
	ZERO_LAST_BLOCKS(%o0, 0x08, %g2)

6:
	be	8f
	 andcc	%o1, 4, %g0

	be	1f
	 andcc	%o1, 2, %g0

	st	%g3, [%o0]
	add	%o0, 4, %o0
1:
	be	1f
	 andcc	%o1, 1, %g0

	sth	%g3, [%o0]
	add	%o0, 2, %o0
1:
	bne,a	8f
	 stb	%g3, [%o0]
8:
	retl
	 mov	%g1,%o0

/* Don't care about alignment here. It is highly 
 * unprobable and at most two traps may happen
 */
7:
	b	6b
	 orcc	%o1, 0, %g0

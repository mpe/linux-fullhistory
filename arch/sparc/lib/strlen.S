/* strlen.S: Sparc optimized strlen().
 *
 * This was hand optimized by davem@caip.rutgers.edu from
 * the C-code in GNU-libc.
 */

#include <asm/cprefix.h>

#define LO_MAGIC 0x01010101
#define HI_MAGIC 0x80808080

	.align 4
	.global C_LABEL(strlen)
C_LABEL(strlen):
	mov	%o0,%o1
	andcc	%o0,3,%g0		! and with %o0 so no dependency problems
	be	scan_words
	 sethi	%hi(HI_MAGIC),%g2	! common case and most Sparcs predict taken

	ldsb	[%o0],%g2
still_not_word_aligned:
	cmp	%g2,0
	bne,a	1f
	 add	%o0,1,%o0

	/* Ok, so there are tons of quick interlocks above for the
	 * < 4 length string unaligned... not too common so I'm not
	 * very concerned.
	 */
	retl
	 sub	%o0,%o1,%o0

1:
	andcc	%o0,3,%g0
	bne,a	still_not_word_aligned
	 ldsb	[%o0],%g2

	/* HyperSparc executes each sethi/or pair in 1 cycle. */
	sethi	%hi(HI_MAGIC),%g2
scan_words:
	or	%g2,%lo(HI_MAGIC),%o3
	sethi	%hi(LO_MAGIC),%g3
	or	%g3,%lo(LO_MAGIC),%o2
next_word:
	ld	[%o0],%g2		! no dependencies
next_word_preloaded:
	sub	%g2,%o2,%g2		! lots of locks here
	andcc	%g2,%o3,%g0		! and I dont like it...
	be	next_word
	 add	%o0,4,%o0

	/* Check every byte. */
byte_zero:
	ldsb	[%o0-4],%g2
	cmp	%g2,0
	bne	byte_one
	 add	%o0,-4,%g3

	retl
	 sub	%g3,%o1,%o0

byte_one:
	ldsb	[%o0-3],%g2
	cmp	%g2,0
	bne,a	byte_two_and_three
	 ldsb	[%o0-2],%g2

	sub	%g3,%o1,%o0
	retl
	 add	%o0,1,%o0

byte_two_and_three:
	cmp	%g2,0
	be,a	found_it
	 sub	%g3,%o1,%o0

	ldsb	[%o0-1],%g2
	cmp	%g2,0
	bne,a	next_word_preloaded
	 ld	[%o0],%g2

	sub	%g3,%o1,%o0
	retl
	 add	%o0,3,%o0

found_it:
	retl
	 add	%o0,2,%o0

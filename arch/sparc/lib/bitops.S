/* bitops.S: Low level assembler bit operations.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/cprefix.h>
#include <asm/ptrace.h>
#include <asm/psr.h>

	.text
	.align	4

	/* Take bits in %g2 and set them in word at %g1,
	 * return whether bits were set in original value
	 * in %g2.  %g4 holds value to restore into %o7
	 * in delay slot of jmpl return, %g3 + %g5 + %g7 can be
	 * used as temporaries and thus is considered clobbered
	 * by all callers.
	 */
	.globl	___set_bit
___set_bit:
	rd	%psr, %g3
	andcc	%g3, PSR_PIL, %g0
	bne	1f
	 nop
	wr	%g3, PSR_PIL, %psr
	nop; nop; nop
1:
	ld	[%g1], %g7
	or	%g7, %g2, %g5
	andcc	%g3, PSR_PIL, %g0
	and	%g7, %g2, %g2
	bne	1f
	 st	%g5, [%g1]
	wr	%g3, 0x0, %psr
	nop; nop; nop
1:
	jmpl	%o7, %g0
	 mov	%g4, %o7

	/* Same as above, but clears the bits from %g2 instead. */
	.globl	___clear_bit
___clear_bit:
	rd	%psr, %g3
	andcc	%g3, PSR_PIL, %g0
	bne	1f
	 nop
	wr	%g3, PSR_PIL, %psr
	nop; nop; nop
1:
	ld	[%g1], %g7
	andn	%g7, %g2, %g5
	andcc	%g3, PSR_PIL, %g0
	and	%g7, %g2, %g2
	bne	1f
	 st	%g5, [%g1]
	wr	%g3, 0x0, %psr
	nop; nop; nop
1:
	jmpl	%o7, %g0
	 mov	%g4, %o7

	/* Same thing again, but this time toggles the bits from %g2. */
	.globl	___change_bit
___change_bit:
	rd	%psr, %g3
	andcc	%g3, PSR_PIL, %g0
	bne	1f
	 nop
	wr	%g3, PSR_PIL, %psr
	nop; nop; nop
1:
	ld	[%g1], %g7
	xor	%g7, %g2, %g5
	andcc	%g3, PSR_PIL, %g0
	and	%g7, %g2, %g2
	bne	1f
	 st	%g5, [%g1]
	wr	%g3, 0x0, %psr
	nop; nop; nop
1:
	jmpl	%o7, %g0
	 mov	%g4, %o7

	/* Now the little endian versions. */
	.globl	___set_le_bit
___set_le_bit:
	rd	%psr, %g3
	andcc	%g3, PSR_PIL, %g0
	bne	1f
	 nop
	wr	%g3, PSR_PIL, %psr
	nop; nop; nop
1:
	ldub	[%g1], %g7
	or	%g7, %g2, %g5
	andcc	%g3, PSR_PIL, %g0
	and	%g7, %g2, %g2
	bne	1f
	 stb	%g5, [%g1]
	wr	%g3, 0x0, %psr
	nop; nop; nop
1:
	jmpl	%o7, %g0
	 mov	%g4, %o7

	.globl	___clear_le_bit
___clear_le_bit:
	rd	%psr, %g3
	andcc	%g3, PSR_PIL, %g0
	bne	1f
	 nop
	wr	%g3, PSR_PIL, %psr
	nop; nop; nop
1:
	ldub	[%g1], %g7
	andn	%g7, %g2, %g5
	andcc	%g3, PSR_PIL, %g0
	and	%g7, %g2, %g2
	bne	1f
	 stb	%g5, [%g1]
	wr	%g3, 0x0, %psr
	nop; nop; nop
1:
	jmpl	%o7, %g0
	 mov	%g4, %o7

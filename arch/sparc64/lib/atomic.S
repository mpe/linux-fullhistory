/* $Id: atomic.S,v 1.2 1999/08/23 05:15:58 davem Exp $
 * atomic.S: These things are too big to do inline.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#include <asm/asi.h>

	.text
	.align	64

	.globl	atomic_impl_begin, atomic_impl_end

	.globl	__atomic_add
atomic_impl_begin:
__atomic_add:
	lduw	[%g1], %g5
	add	%g5, %g2, %g7
	cas	[%g1], %g5, %g7
	cmp	%g5, %g7
	bne,pn	%icc, __atomic_add
	 nop
	jmpl	%g3 + 8, %g0
	 add	%g7, %g2, %g2

	.globl	__atomic_sub
__atomic_sub:
	lduw	[%g1], %g5
	sub	%g5, %g2, %g7
	cas	[%g1], %g5, %g7
	cmp	%g5, %g7
	bne,pn	%icc, __atomic_sub
	 nop
	jmpl	%g3 + 8, %g0
	 sub	%g7, %g2, %g2
atomic_impl_end:

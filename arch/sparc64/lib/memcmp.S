/* $Id: memcmp.S,v 1.2 1997/04/01 03:43:18 davem Exp $
 * Sparc64 optimized memcmp code.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

	.text
	.align 4
	.globl __memcmp, memcmp
__memcmp:
memcmp:
	brlez,pn %o2, 2f
	 sub	%g0, %o2, %o3
	add	%o0, %o2, %o0
	add	%o1, %o2, %o1
	ldub	[%o0 + %o3], %o4
1:
	ldub	[%o1 + %o3], %o5
	sub	%o4, %o5, %o4
	brnz,pn	%o4, 3f
	 addcc	%o3, 1, %o3
	bne,a,pt %xcc, 1b
	 ldub	[%o0 + %o3], %o4
2:
	retl
	 clr	%o0
3:
	retl
	 mov	%o4, %o0

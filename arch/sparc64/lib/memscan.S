/* $Id: memscan.S,v 1.1 1997/03/14 21:04:24 jj Exp $
 * memscan.S: Optimized memscan for the Sparc64.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

/* In essence, this is just a fancy strlen. */

#define LO_MAGIC 0x01010101
#define HI_MAGIC 0x80808080

	.text
	.align	4
	.globl	__memscan_zero, __memscan_generic
	.globl	memscan
__memscan_zero:
	/* %o0 = addr, %o1 = size */
	brlez,pn %o1, 0f
	 andcc	%o0, 3, %g0
	be,pt	%icc, 9f
	 sethi	%hi(HI_MAGIC), %o4
	ldub	[%o0], %o5
	subcc	%o1, 1, %o1
	brz,pn	%o5, 10f
	 add	%o0, 1, %o0
	be,pn	%xcc, 0f
	 andcc	%o0, 3, %g0
	be,pn	%icc, 4f
	 or	%o4, %lo(HI_MAGIC), %o3
	ldub	[%o0], %o5
	subcc	%o1, 1, %o1
	brz,pn	%o5, 10f
	 add	%o0, 1, %o0
	be,pn	%xcc, 0f
	 andcc	%o0, 3, %g0
	be,pt	%icc, 5f
	 sethi	%hi(LO_MAGIC), %o4
	ldub	[%o0], %o5
	subcc	%o1, 1, %o1
	brz,pn	%o5, 10f
	 add	%o0, 1, %o0
	be,pn	%xcc, 0f
	 or	%o4, %lo(LO_MAGIC), %o2
	ba,pt	%xcc, 2f
	 ld	[%o0], %o5
9:
	or	%o4, %lo(HI_MAGIC), %o3
4:
	sethi	%hi(LO_MAGIC), %o4
5:
	or	%o4, %lo(LO_MAGIC), %o2
	ld	[%o0], %o5
2:
	sub	%o5, %o2, %o4
	sub	%o1, 4, %o1
	andcc	%o4, %o3, %g0
	be,pn	%icc, 1f
	 add	%o0, 4, %o0
	brgz,pt	%o1, 2b
	 ld	[%o0], %o5

	retl
	 add	%o0, %o1, %o0
1:
	/* Check every byte. */
	srl	%o5, 24, %g5
	andcc	%g5, 0xff, %g0
	be,pn	%icc, 1f
	 add	%o0, -4, %o4
	srl	%o5, 16, %g5
	andcc	%g5, 0xff, %g0
	be,pn	%icc, 1f
	 add	%o4, 1, %o4
	srl	%o5, 8, %g5
	andcc	%g5, 0xff, %g0
	be,pn	%icc, 1f
	 add	%o4, 1, %o4
	andcc	%o5, 0xff, %g0
	be,pn	%icc, 1f
	 add	%o4, 1, %o4
	brgz,pt	%o1, 2b
	 ld	[%o0], %o5
1:
	add	%o0, %o1, %o0
	cmp	%o4, %o0
	retl
	 movle	%xcc, %o4, %o0
0:
	retl
	 nop
10:
	retl
	 sub	%o0, 1, %o0

memscan:
__memscan_generic:
	/* %o0 = addr, %o1 = c, %o2 = size */
	brz,pn	%o2, 3f
	 add	%o0, %o2, %o3
	ldub	[%o0], %o5
	sub	%g0, %o2, %o4
1:
	cmp	%o5, %o1
	be,pn	%icc, 2f
	 addcc	%o4, 1, %o4
	bne,a,pt %xcc, 1b
	 ldub	[%o3 + %o4], %o5
	retl
	/* The delay slot is the same as the next insn, this is just to make it look more awful */
2:
	 add	%o3, %o4, %o0
	retl
	 sub	%o0, 1, %o0
3:
	retl
	 nop

/* $Id: strncpy_from_user.S,v 1.5 1997/09/08 11:29:23 jj Exp $
 * strncpy_from_user.S: Sparc64 strncpy from userspace.
 *
 *  Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <asm/asi.h>
#include <asm/errno.h>

	.data
	.align	8
0:	.xword	0x0101010101010101

	.text
	.align	4

	/* Must return:
	 *
	 * -EFAULT		for an exception
	 * count		if we hit the buffer limit
	 * bytes copied		if we hit a null byte
	 * (without the null byte)
	 *
	 * This implementation assumes:
	 * %o1 is 8 aligned => !(%o2 & 7)
	 * %o0 is 8 aligned (if not, it will be slooooow, but will work)
	 *
	 * This is optimized for the common case:
	 * in my stats, 90% of src are 8 aligned (even on sparc32)
	 * and average length is 18 or so.
	 */

	.globl	__strncpy_from_user
__strncpy_from_user:
	/* %o0=dest, %o1=src, %o2=count */
	sethi	%hi(0b), %o5		! IEU0	Group
	andcc	%o1, 7, %g0		! IEU1
	bne,pn	%icc, 30f		! CTI
	 ldx	[%o5 + %lo(0b)], %o4	! Load	Group
60:	ldxa	[%o1] ASI_S, %g1	! Load	Group
	add	%o1, %o2, %o1		! IEU0
	subcc	%g0, %o2, %o3		! IEU1
	bgeu,pn	%xcc, 10f		! CTI
	 sllx	%o4, 7, %o5		! IEU0	Group
	add	%o0, %o2, %o0		! IEU1
1:	sub	%g1, %o4, %g2		! IEU0	Group
	stx	%g1, [%o0 + %o3]	! Store
	andcc	%g2, %o5, %g0		! IEU1	Group
	bne,pn	%xcc, 5f		! CTI
	 add	%o3, 8, %o3		! IEU0
	brlz,a,pt %o3, 1b		! CTI(IEU1) Group
61:	 ldxa	[%o1 + %o3] ASI_S, %g1	! Load
10:	retl				! CTI	Group
	 mov	%o2, %o0		! IEU0
5:	srlx	%g2, 32, %g7		! IEU0	Group
	sethi	%hi(0xff00), %g5	! IEU1
	andcc	%g7, %o5, %g0		! IEU1	Group
	be,pn	%icc, 2f		! CTI
	 or	%g5, %lo(0xff00), %g5	! IEU0
	srlx	%g1, 48, %g7		! IEU0	Group
	andcc	%g7, %g5, %g0		! IEU1	Group
	be,pn	%icc, 50f		! CTI
	 andcc	%g7, 0xff, %g0		! IEU1	Group
	be,pn	%icc, 51f		! CTI
	 srlx	%g1, 32, %g7		! IEU0
	andcc	%g7, %g5, %g0		! IEU1	Group
	be,pn	%icc, 52f		! CTI
	 andcc	%g7, 0xff, %g0		! IEU1	Group
	be,pn	%icc, 53f		! CTI
2:	 andcc	%g2, %o5, %g0		! IEU1	Group
	be,pn	%icc, 2f		! CTI
	 srl	%g1, 16, %g7		! IEU0
	andcc	%g7, %g5, %g0		! IEU1	Group
	be,pn	%icc, 54f		! CTI
	 andcc	%g7, 0xff, %g0		! IEU1	Group
	be,pn	%icc, 55f		! CTI
	 andcc	%g1, %g5, %g0		! IEU1	Group
	be,pn	%icc, 56f		! CTI
	 andcc	%g1, 0xff, %g0		! IEU1	Group
	be,a,pn	%icc, 57f		! CTI
	 add	%o2, %o3, %o0		! IEU0
2:	brlz,a,pt %o3, 1b		! CTI(IEU1) Group
62:	 ldxa	[%o1 + %o3] ASI_S, %g1	! Load
	retl				! CTI	Group
	 mov	%o2, %o0		! IEU0
50:	add	%o2, %o3, %o0
	retl
	 sub	%o0, 8, %o0
51:	add	%o2, %o3, %o0
	retl
	 sub	%o0, 7, %o0
52:	add	%o2, %o3, %o0
	retl
	 sub	%o0, 6, %o0
53:	add	%o2, %o3, %o0
	retl
	 sub	%o0, 5, %o0
54:	add	%o2, %o3, %o0
	retl
	 sub	%o0, 4, %o0
55:	add	%o2, %o3, %o0
	retl
	 sub	%o0, 3, %o0
56:	add	%o2, %o3, %o0
	retl
	 sub	%o0, 2, %o0
57:	retl
	 sub	%o0, 1, %o0
30:	brlez,pn %o2, 3f
	 add	%o1, %o2, %o1
	sub	%g0, %o2, %o3
	add	%o0, %o2, %o0
63:	lduba	[%o1 + %o3] ASI_S, %o4
1:	brz,pn	%o4, 2f
	 stb	%o4, [%o0 + %o3]
	addcc	%o3, 1, %o3
	bne,pt	%xcc, 1b
64:	 lduba	[%o1 + %o3] ASI_S, %o4
3:	retl
	 mov	%o2, %o0
2:	retl
	 add	%o2, %o3, %o0

	.section .fixup,#alloc,#execinstr
	.align	4
4:	retl
	 mov	-EFAULT, %o0

	.section __ex_table,#alloc
	.align	4
	.word	60b, 4b
	.word	61b, 4b
	.word	62b, 4b
	.word	63b, 4b
	.word	64b, 4b

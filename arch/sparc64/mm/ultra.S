/* $Id: ultra.S,v 1.6 1997/06/30 10:31:43 jj Exp $
 * ultra.S: Don't expand these all over the place...
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/asi.h>
#include <asm/spitfire.h>

	/* All callers check mm->context != NO_CONTEXT for us. */
	.text
	.align		32
	.globl		__flush_tlb_mm, __flush_tlb_range, __flush_tlb_page
__flush_tlb_mm:		/* %o0 == (mm->context & 0x1fff) */
	rdpr		%otherwin, %g1
	brz,pt		%g1, 1f
	 mov		%o7, %g3
	call		__flushw_user
	 clr		%g2
1:	rdpr		%pil, %g1
9:	mov		SECONDARY_CONTEXT, %g7
	wrpr		%g0, 15, %pil

	ldxa		[%g7] ASI_DMMU, %g2
	cmp		%g2, %o0
	be,pt		%icc, 1f
	 mov		0x50, %g3
	stxa		%o0, [%g7] ASI_DMMU
1:	stxa		%g0, [%g3] ASI_DMMU_DEMAP
	be,pt		%icc, 1f
	 stxa		%g0, [%g3] ASI_IMMU_DEMAP

	stxa		%g2, [%g7] ASI_DMMU
1:	wrpr		%g1, 0x0, %pil
	retl
	 flush		%g6
__flush_tlb_range:	/* %o0 == (mm->context & 0x1fff), %o1 == start, %o2 == end */
	sethi		%hi(8192 - 1), %g5
	or		%g5, %lo(8192 - 1), %g5
	andn		%o1, %g5, %o1
	andn		%o2, %g5, %o2

	sub		%o2, %o1, %o3
	add		%g5, 1, %g5
	orcc		%o1, 0x50, %o1
	srlx		%o3, 13, %o4
	rdpr		%otherwin, %g1
	brz,pt		%g1, 1f
	 mov		%o7, %g3
	call		__flushw_user

	 clr		%g2
1:	cmp		%o4, 96
	bgu,pn		%icc, 9b
	 rdpr		%pil, %g1
	mov		SECONDARY_CONTEXT, %g7
	wrpr		%g0, 15, %pil
	ldxa		[%g7] ASI_DMMU, %g2
	cmp		%g2, %o0

	be,pt		%icc, 1f
	 sub		%o3, %g5, %o3
	stxa		%o0, [%g7] ASI_DMMU
1:	stxa		%g0, [%o1 + %o3] ASI_DMMU_DEMAP
	stxa		%g0, [%o1 + %o3] ASI_IMMU_DEMAP
	brnz,pt		%o3, 1b
	 sub		%o3, %g5, %o3
	nop

	be,pt		%icc, 1f
	 wrpr		%g1, 0x0, %pil
	stxa		%g2, [%g7] ASI_DMMU
1:	retl
	 flush		%g6

	.align		32
__flush_tlb_page:	/* %o0 == (mm->context & 0x1fff), %o1 == page & PAGE_MASK */
	rdpr		%otherwin, %g1
	brz,pt		%g1, 1f
	 mov		%o7, %g3
	call		__flushw_user
	 clr		%g2
1:	rdpr		%pil, %g1
	mov		SECONDARY_CONTEXT, %g7
	wrpr		%g0, 15, %pil

	ldxa		[%g7] ASI_DMMU, %g2
	cmp		%g2, %o0
	be,pt		%icc, 1f
	 or		%o1, 0x10, %g3
	stxa		%o0, [%g7] ASI_DMMU
1:	stxa		%g0, [%g3] ASI_DMMU_DEMAP
	be,pt		%icc, 1f
	 stxa		%g0, [%g3] ASI_IMMU_DEMAP
	stxa		%g2, [%g7] ASI_DMMU
1:	wrpr		%g1, 0x0, %pil
	retl
	 flush		%g6


	

/* $Id: ultra.S,v 1.9 1997/07/24 12:15:08 davem Exp $
 * ultra.S: Don't expand these all over the place...
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/asi.h>
#include <asm/pgtable.h>
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

#ifdef __SMP__
	/* These are all called by the slaves of a cross call, at
	 * trap level 1, with interrupts fully disabled.
	 *
	 * Register usage:
	 *   %g5	mm->context	(all tlb flushes)
	 *   %g6	address arg 1	(tlb page and range flushes)
	 *   %g7	address arg 2	(tlb range flush only)
	 *
	 *   %g1	ivector table, don't touch
	 *   %g2	scratch 1
	 *   %g3	scratch 2
	 *   %g4	scratch 3
	 *
	 * NOTE: We do not acknowledge the UPA until we are done
	 *       with the service.  This is what tells the master
	 *       that he can consider the effects of the flush
	 *       "complete" on this cpu.
	 */
	.align		32
	.globl		xcall_flush_tlb_page
xcall_flush_tlb_page:
	mov		SECONDARY_CONTEXT, %g2
	nop
	ldxa		[%g2] ASI_DMMU, %g3
	cmp		%g3, %g5
	be,pt		%icc, 1f
	 or		%g6, 0x10, %g4
	stxa		%g5, [%g2] ASI_DMMU
1:	stxa		%g0, [%g4] ASI_DMMU_DEMAP

	be,pt		%icc, 1f
	 stxa		%g0, [%g4] ASI_IMMU_DEMAP
	stxa		%g3, [%g2] ASI_DMMU
1:	b,pt		%xcc, do_ivec_return
	 flush		%g1

	.align		32
	.globl		xcall_flush_tlb_mm
xcall_flush_tlb_mm:
	mov		SECONDARY_CONTEXT, %g2
	nop
	ldxa		[%g2] ASI_DMMU, %g3
	cmp		%g3, %g5
	be,pt		%icc, 1f
	 mov		0x50, %g4
	stxa		%g5, [%g2] ASI_DMMU
1:	stxa		%g0, [%g4] ASI_DMMU_DEMAP

	be,pt		%icc, 1f
	 stxa		%g0, [%g4] ASI_IMMU_DEMAP
	stxa		%g3, [%g2] ASI_DMMU
1:	b,pt		%xcc, do_ivec_return
	 flush		%g1

	.align		32
	.globl		xcall_flush_tlb_range
xcall_flush_tlb_range:
	sethi		%hi(8192 - 1), %g2
	or		%g2, %lo(8192 - 1), %g2
	andn		%g6, %g2, %g6
	andn		%g7, %g2, %g7
	sub		%g7, %g6, %g3
	add		%g2, 1, %g2
	orcc		%g6, 0x50, %g6
	srlx		%g3, 13, %g4

	cmp		%g4, 96
	bgu,pn		%icc, xcall_flush_tlb_mm
	 mov		SECONDARY_CONTEXT, %g4
	ldxa		[%g4] ASI_DMMU, %g7
	cmp		%g7, %g5
	be,pt		%icc, 1f
	 sub		%g3, %g2, %g3
	stxa		%g5, [%g4] ASI_DMMU

1:	stxa		%g0, [%g6 + %g3] ASI_DMMU_DEMAP
	stxa		%g0, [%g6 + %g3] ASI_IMMU_DEMAP
	brnz,pt		%g3, 1b
	 sub		%g3, %g2, %g3
	bne,a,pn	%icc, 1f
	 stxa		%g7, [%g4] ASI_DMMU
1:	b,pt		%xcc, do_ivec_return
	 flush		%g1	

	/* These two are not performance critical... */
	.globl		xcall_flush_tlb_all
xcall_flush_tlb_all:
	clr		%g2
	clr		%g3
1:	ldxa		[%g3] ASI_DTLB_DATA_ACCESS, %g4
	and		%g4, _PAGE_L, %g5
	brnz,pn		%g5, 2f
	 mov		TLB_TAG_ACCESS, %g7
	stxa		%g0, [%g7] ASI_DMMU
	membar		#Sync

	stxa		%g0, [%g3] ASI_DTLB_DATA_ACCESS
	membar		#Sync
2:	ldxa		[%g3] ASI_ITLB_DATA_ACCESS, %g4
	and		%g4, _PAGE_L, %g5
	brnz,pn		%g5, 2f
	 mov		TLB_TAG_ACCESS, %g7
	stxa		%g0, [%g7] ASI_IMMU
	membar		#Sync

	stxa		%g0, [%g3] ASI_ITLB_DATA_ACCESS
2:	add		%g2, 1, %g2
	cmp		%g2, 63
	ble,pt		%icc, 1b
	 sll		%g2, 3, %g3
	b,pt		%xcc, do_ivec_return
	 flush		%g1

	.globl		xcall_flush_cache_all
xcall_flush_cache_all:
	sethi		%hi(16383), %g2
	or		%g2, %lo(16383), %g2
	clr		%g3
1:	stxa		%g0, [%g3] ASI_IC_TAG
	add		%g3, 32, %g3
	cmp		%g3, %g2
	bleu,pt		%xcc, 1b
	 nop
	b,pt		%xcc, do_ivec_return
	 flush		%g1
#endif /* __SMP__ */

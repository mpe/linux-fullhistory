/* $Id: ultra.S,v 1.21 1998/04/28 08:23:30 davem Exp $
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
__flush_tlb_mm:		/* %o0 == (mm->context & 0x3ff) */
	mov		SECONDARY_CONTEXT, %g7
9:	ldxa		[%g7] ASI_DMMU, %g2
	cmp		%g2, %o0
	bne,pn		%icc, 1f
	 mov		0x50, %g3
	stxa		%g0, [%g3] ASI_DMMU_DEMAP
	stxa		%g0, [%g3] ASI_IMMU_DEMAP
	retl
	 flush		%g6
1:	rdpr		%pstate, %g1
	wrpr		%g1, PSTATE_IE, %pstate
	stxa		%o0, [%g7] ASI_DMMU
	stxa		%g0, [%g3] ASI_DMMU_DEMAP
	stxa		%g0, [%g3] ASI_IMMU_DEMAP
	flush		%g6
	stxa		%g2, [%g7] ASI_DMMU
	flush		%g6
	retl
	 wrpr		%g1, 0, %pstate
	nop
__flush_tlb_range:	/* %o0 == (mm->context & 0x3ff), %o1 == start, %o2 == end */
	sethi		%hi(8192 - 1), %g5
	or		%g5, %lo(8192 - 1), %g5
	andn		%o1, %g5, %o1
	andn		%o2, %g5, %o2

	sub		%o2, %o1, %o3
	add		%g5, 1, %g5
	orcc		%o1, 0x10, %o1
	srlx		%o3, 13, %o4
	cmp		%o4, 96
	bgu,pn		%icc, 9b
	 mov		SECONDARY_CONTEXT, %g7
	ldxa		[%g7] ASI_DMMU, %g2

	nop
	nop
	cmp		%g2, %o0
	be,pt		%icc, 1f
	 sub		%o3, %g5, %o3
	rdpr		%pstate, %g1
	wrpr		%g1, PSTATE_IE, %pstate
	stxa		%o0, [%g7] ASI_DMMU

1:	stxa		%g0, [%o1 + %o3] ASI_DMMU_DEMAP
	stxa		%g0, [%o1 + %o3] ASI_IMMU_DEMAP
	brnz,pt		%o3, 1b
	 sub		%o3, %g5, %o3
	flush		%g6
	be,pt		%icc, 1f
	 nop
	stxa		%g2, [%g7] ASI_DMMU

	flush		%g6
	wrpr		%g1, 0, %pstate
1:	retl
	 nop

	.align		32
__flush_tlb_page:	/* %o0 == (mm->context & 0x3ff), %o1 == page & PAGE_MASK */
	mov		SECONDARY_CONTEXT, %g7
	ldxa		[%g7] ASI_DMMU, %g2
	cmp		%g2, %o0
	bne,pt		%icc, 1f
	 or		%o1, 0x10, %g3
	stxa		%g0, [%g3] ASI_DMMU_DEMAP
	stxa		%g0, [%g3] ASI_IMMU_DEMAP
	retl
	 flush		%g6
1:	rdpr		%pstate, %g1
	wrpr		%g1, PSTATE_IE, %pstate
	stxa		%o0, [%g7] ASI_DMMU
	stxa		%g0, [%g3] ASI_DMMU_DEMAP
	stxa		%g0, [%g3] ASI_IMMU_DEMAP
	flush		%g6
	stxa		%g2, [%g7] ASI_DMMU
	flush		%g6
	retl
	 wrpr		%g1, 0, %pstate

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
	 */
	.align		32
	.globl		xcall_flush_tlb_page, xcall_flush_tlb_mm, xcall_flush_tlb_range
xcall_flush_tlb_page:
	mov		SECONDARY_CONTEXT, %g2
	or		%g6, 0x10, %g4
	ldxa		[%g2] ASI_DMMU, %g3
	stxa		%g5, [%g2] ASI_DMMU
	stxa		%g0, [%g4] ASI_DMMU_DEMAP
	stxa		%g0, [%g4] ASI_IMMU_DEMAP
	stxa		%g3, [%g2] ASI_DMMU
	retry

xcall_flush_tlb_mm:
	mov		SECONDARY_CONTEXT, %g2
	mov		0x50, %g4
	ldxa		[%g2] ASI_DMMU, %g3
	stxa		%g5, [%g2] ASI_DMMU
	stxa		%g0, [%g4] ASI_DMMU_DEMAP
	stxa		%g0, [%g4] ASI_IMMU_DEMAP
	stxa		%g3, [%g2] ASI_DMMU
	retry

xcall_flush_tlb_range:
	sethi		%hi(8192 - 1), %g2
	or		%g2, %lo(8192 - 1), %g2
	andn		%g6, %g2, %g6
	andn		%g7, %g2, %g7
	sub		%g7, %g6, %g3
	add		%g2, 1, %g2
	orcc		%g6, 0x10, %g6
	srlx		%g3, 13, %g4

	cmp		%g4, 96
	bgu,pn		%icc, xcall_flush_tlb_mm
	 mov		SECONDARY_CONTEXT, %g4
	ldxa		[%g4] ASI_DMMU, %g7
	sub		%g3, %g2, %g3
	stxa		%g5, [%g4] ASI_DMMU
	nop
	nop

1:	stxa		%g0, [%g6 + %g3] ASI_DMMU_DEMAP
	stxa		%g0, [%g6 + %g3] ASI_IMMU_DEMAP
	brnz,pt		%g3, 1b
	 sub		%g3, %g2, %g3
	stxa		%g7, [%g4] ASI_DMMU
	retry
	nop
	nop

	.globl		xcall_report_regs
xcall_report_regs:
	rdpr		%pstate, %g2
	wrpr		%g2, PSTATE_IG | PSTATE_AG, %pstate
	rdpr		%pil, %g2
	wrpr		%g0, 15, %pil
	sethi		%hi(109f), %g7
	b,pt		%xcc, etrap_irq
109:	 or		%g7, %lo(109b), %g7
	call		__show_regs
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	b,pt		%xcc, rtrap
	 clr		%l6

	.globl		xcall_capture
xcall_capture:
	rdpr		%pstate, %g2
	wrpr		%g2, PSTATE_IG | PSTATE_AG, %pstate
	rdpr		%pil, %g2
	wrpr		%g0, 15, %pil
	sethi		%hi(109f), %g7
	b,pt		%xcc, etrap_irq
109:	 or		%g7, %lo(109b), %g7
	call		smp_penguin_jailcell
	 nop
	b,pt		%xcc, rtrap
	 clr		%l6

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
	flush		%g1
	retry

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
	flush		%g1
	retry
#endif /* __SMP__ */

/* $Id: ultra.S,v 1.31 1998/11/07 06:39:21 davem Exp $
 * ultra.S: Don't expand these all over the place...
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/asi.h>
#include <asm/pgtable.h>
#include <asm/spitfire.h>

	/* This file is meant to be read efficiently by the CPU, not humans.
	 * Staraj sie tego nikomu nie pierdolnac...
	 */
	.text
	.align		32
	.globl		__flush_tlb_page, __flush_tlb_mm, __flush_tlb_range
__flush_tlb_page: /* %o0=(ctx & 0x3ff), %o1=page&PAGE_MASK, %o2=SECONDARY_CONTEXT */
/*IC1*/	ldxa		[%o2] ASI_DMMU, %g2
	cmp		%g2, %o0
	bne,pn		%icc, __flush_tlb_page_slow
	 or		%o1, 0x10, %g3
	stxa		%g0, [%g3] ASI_DMMU_DEMAP
	stxa		%g0, [%g3] ASI_IMMU_DEMAP
	retl
	 flush		%g6
__flush_tlb_mm: /* %o0=(ctx & 0x3ff), %o1=SECONDARY_CONTEXT */
/*IC2*/	ldxa		[%o1] ASI_DMMU, %g2
	cmp		%g2, %o0
	bne,pn		%icc, __flush_tlb_mm_slow
	 mov		0x50, %g3
	stxa		%g0, [%g3] ASI_DMMU_DEMAP
	stxa		%g0, [%g3] ASI_IMMU_DEMAP
	retl
	 flush		%g6
__flush_tlb_range: /* %o0=(ctx&0x3ff), %o1=start&PAGE_MASK, %o2=SECONDARY_CONTEXT,
		    * %o3=end&PAGE_MASK, %o4=PAGE_SIZE, %o5=(end - start)
		    */
#define TLB_MAGIC	206 /* Students, do you know how I calculated this?  -DaveM */
/*IC3*/	cmp		%o5, %o4
	be,pt		%xcc, __flush_tlb_page
	 srlx		%o5, 13, %g5
	cmp		%g5, TLB_MAGIC
	bgeu,pn		%icc, __flush_tlb_range_constant_time
	 or		%o1, 0x10, %g5
	ldxa		[%o2] ASI_DMMU, %g2
	cmp		%g2, %o0
__flush_tlb_range_page_by_page:
/*IC4*/	bne,pn		%icc, __flush_tlb_range_pbp_slow
	 sub		%o5, %o4, %o5
1:	stxa		%g0, [%g5 + %o5] ASI_DMMU_DEMAP
	stxa		%g0, [%g5 + %o5] ASI_IMMU_DEMAP
	brnz,pt		%o5, 1b
	 sub		%o5, %o4, %o5
	retl
	 flush		%g6
__flush_tlb_range_constant_time: /* %o0=ctx, %o1=start, %o3=end */
/*IC5*/	rdpr		%pstate, %g1
	wrpr		%g1, PSTATE_IE, %pstate
	mov		TLB_TAG_ACCESS, %g3
	mov		(62 << 3), %g2
1:	ldxa		[%g2] ASI_ITLB_TAG_READ, %o4
	and		%o4, 0x3ff, %o5
	cmp		%o5, %o0
	bne,pt		%icc, 2f
/*IC6*/	 andn		%o4, 0x3ff, %o4
	cmp		%o4, %o1
	blu,pt		%xcc, 2f
	 cmp		%o4, %o3
	blu,pn		%xcc, 4f
2:	 ldxa		[%g2] ASI_DTLB_TAG_READ, %o4
	and		%o4, 0x3ff, %o5
	cmp		%o5, %o0
/*IC7*/	andn		%o4, 0x3ff, %o4
	bne,pt		%icc, 3f
	 cmp		%o4, %o1
	blu,pt		%xcc, 3f
	 cmp		%o4, %o3
	blu,pn		%xcc, 5f
	 nop
3:	brnz,pt		%g2, 1b
/*IC8*/	 sub		%g2, (1 << 3), %g2
	retl
	 wrpr		%g1, 0x0, %pstate
4:	stxa		%g0, [%g3] ASI_IMMU
	stxa		%g0, [%g2] ASI_ITLB_DATA_ACCESS
	ba,pt		%xcc, 2b
	 flush		%g6
5:	stxa		%g0, [%g3] ASI_DMMU
/*IC9*/	stxa		%g0, [%g2] ASI_DTLB_DATA_ACCESS
	ba,pt		%xcc, 3b
	 flush		%g6

	.align		32
__flush_tlb_mm_slow:
/*IC10*/rdpr		%pstate, %g1
	wrpr		%g1, PSTATE_IE, %pstate
	stxa		%o0, [%o1] ASI_DMMU
	stxa		%g0, [%g3] ASI_DMMU_DEMAP
	stxa		%g0, [%g3] ASI_IMMU_DEMAP
	flush		%g6
	stxa		%g2, [%o1] ASI_DMMU
	flush		%g6
/*IC11*/retl
	 wrpr		%g1, 0, %pstate

	.align		32
__flush_tlb_page_slow:
/*IC12*/rdpr		%pstate, %g1
	wrpr		%g1, PSTATE_IE, %pstate
	stxa		%o0, [%o2] ASI_DMMU
	stxa		%g0, [%g3] ASI_DMMU_DEMAP
	stxa		%g0, [%g3] ASI_IMMU_DEMAP
	flush		%g6
	stxa		%g2, [%o2] ASI_DMMU
	flush		%g6
/*IC13*/retl
	 wrpr		%g1, 0, %pstate

	.align		32
__flush_tlb_range_pbp_slow:
/*IC13*/rdpr		%pstate, %g1
	wrpr		%g1, PSTATE_IE, %pstate
	stxa		%o0, [%o2] ASI_DMMU
2:	stxa		%g0, [%g5 + %o5] ASI_DMMU_DEMAP
	stxa		%g0, [%g5 + %o5] ASI_IMMU_DEMAP
	brnz,pt		%o5, 2b
	 sub		%o5, %o4, %o5
	flush		%g6
/*IC14*/stxa		%g2, [%o2] ASI_DMMU
	flush		%g6
	retl
	 wrpr		%g1, 0x0, %pstate

	.align		32
	.globl		flush_icache_page
flush_icache_page:	/* %o0 = phys_page */
	sethi		%hi(1 << 13), %o2	! IC_set bit
	mov		1, %g1
	srlx		%o0, 5, %o0		! phys-addr comparitor
	clr		%o1			! IC_addr
	sllx		%g1, 36, %g1
	sub		%g1, 1, %g2
	andn		%g2, 0xff, %g2		! IC_tag mask
	nop

1:	ldda		[%o1] ASI_IC_TAG, %o4
	and		%o5, %g2, %o5
	cmp		%o5, %o0
	be,pn		%xcc, iflush1
	 nop
2:	ldda		[%o1 + %o2] ASI_IC_TAG, %o4
	and		%o5, %g2, %o5
	cmp		%o5, %o0

	be,pn		%xcc, iflush2
	 nop
3:	add		%o1, 0x20, %o1
	cmp		%o1, %o2
	bne,pt		%xcc, 1b
	 nop
	retl
	 nop
iflush1:stxa		%g0, [%o1] ASI_IC_TAG
	ba,pt		%xcc, 2b
	 flush		%g6
iflush2:stxa		%g0, [%o1 + %o2] ASI_IC_TAG
	ba,pt		%xcc, 3b
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
	 * TODO: Make xcall TLB range flushes use the tricks above... -DaveM
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

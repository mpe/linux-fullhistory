/* $Id: tsunami.S,v 1.1 1997/05/03 05:09:09 davem Exp $
 * tsunami.S: High speed MicroSparc-I mmu/cache operations.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/ptrace.h>
#include <asm/psr.h>
#include <asm/asi.h>
#include <asm/page.h>
#include <asm/pgtsrmmu.h>

#define WINDOW_FLUSH(tmp1, tmp2)					\
	mov	0, tmp1;						\
98:	ld	[%g6 + AOFF_task_tss + AOFF_thread_uwinmask], tmp2;	\
	orcc	%g0, tmp2, %g0;						\
	add	tmp1, 1, tmp1;						\
	bne	98b;							\
	 save	%sp, -64, %sp;						\
99:	subcc	tmp1, 1, tmp1;						\
	bne	99b;							\
	 restore %g0, %g0, %g0;

	.text
	.align	4

	.globl	tsunami_flush_cache_all, tsunami_flush_cache_mm
	.globl	tsunami_flush_cache_range, tsunami_flush_cache_page
	.globl	tsunami_flush_page_to_ram, tsunami_flush_page_for_dma
	.globl	tsunami_flush_sig_insns, tsunami_flush_chunk
	.globl	tsunami_flush_tlb_all, tsunami_flush_tlb_mm
	.globl	tsunami_flush_tlb_range, tsunami_flush_tlb_page

	/* Sliiick... */
tsunami_flush_cache_page:
	ld	[%o0 + 0x0], %o0	/* XXX vma->vm_mm, GROSS XXX */
tsunami_flush_cache_mm:
tsunami_flush_cache_range:
	ld	[%o0 + AOFF_mm_context], %g2
#ifndef __SMP__
	cmp	%g2, -1
	be	tsunami_flush_cache_out
#endif
tsunami_flush_cache_all:
	WINDOW_FLUSH(%g4, %g5)
tsunami_flush_page_for_dma:
	sta	%g0, [%g0] ASI_M_DC_FLCLEAR
	sta	%g0, [%g0] ASI_M_IC_FLCLEAR
tsunami_flush_cache_out:
tsunami_flush_page_to_ram:
tsunami_flush_chunk:
	retl
	 nop

tsunami_flush_sig_insns:
	flush	%o1
	retl
	 flush	%o1 + 4

	/* More slick stuff... */
tsunami_flush_tlb_mm:
tsunami_flush_tlb_range:
#ifndef __SMP__
	ld	[%o0 + AOFF_mm_context], %g2
	cmp	%g2, -1
	be	tsunami_flush_tlb_out
#endif
tsunami_flush_tlb_all:
	 mov	0x400, %o1
	sta	%g0, [%o1] ASI_M_FLUSH_PROBE
tsunami_flush_tlb_out:
	retl
	 nop

	/* This one can be done in a fine grained manner... */
tsunami_flush_tlb_page:
	ld	[%o0 + 0x00], %o0	/* XXX vma->vm_mm GROSS XXX */
	mov	SRMMU_CTX_REG, %g1
	ld	[%o0 + AOFF_mm_context], %o3
	andn	%o1, (PAGE_SIZE - 1), %o1
#ifndef __SMP__
	cmp	%o3, -1
	be	tsunami_flush_tlb_page_out
#endif
	 lda	[%g1] ASI_M_MMUREGS, %g5
	sta	%o3, [%g1] ASI_M_MMUREGS
	sta	%g0, [%o1] ASI_M_FLUSH_PROBE
tsunami_flush_tlb_page_out:
	retl
	 sta	%g5, [%g1] ASI_M_MMUREGS

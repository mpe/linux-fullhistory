/* $Id: viking.S,v 1.11 1998/02/20 18:07:50 jj Exp $
 * viking.S: High speed Viking cache/mmu operations
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 1997,1998  Jakub Jelinek  (jj@sunsite.mff.cuni.cz)
 */

#include <asm/ptrace.h>
#include <asm/psr.h>
#include <asm/asi.h>
#include <asm/mxcc.h>
#include <asm/page.h>
#include <asm/pgtsrmmu.h>
#include <asm/viking.h>
#include <asm/cprefix.h>
#include <asm/btfixup.h>

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

	.globl	viking_flush_cache_all, viking_flush_cache_mm
	.globl	viking_flush_cache_range, viking_flush_cache_page
	.globl	viking_flush_page, viking_mxcc_flush_page
	.globl	viking_flush_page_for_dma, viking_flush_page_to_ram
	.globl	viking_flush_chunk, viking_mxcc_flush_chunk
	.globl	viking_flush_sig_insns
	.globl	viking_flush_tlb_all, viking_flush_tlb_mm
	.globl	viking_flush_tlb_range, viking_flush_tlb_page

	.globl	viking_c_flush_chunk, viking_s_flush_chunk

viking_s_flush_chunk:
	sethi	%hi(KERNBASE), %g2
	ba	2f
	 sub	%o0, %g2, %g3

viking_c_flush_chunk:
	sethi	%hi(KERNBASE), %g2
	cmp	%o0, %g2
	bgeu	2f
	 sub	%o0, %g2, %g3
	sethi	BTFIXUP_SETHI(page_contig_offset), %g2
	ba	2f
	 sub	%o0, %g2, %g3

viking_flush_page:
viking_flush_chunk:
	sethi	%hi(C_LABEL(srmmu_v2p_hash)), %g2
	srl	%o0, 24, %o1
	or	%g2, %lo(C_LABEL(srmmu_v2p_hash)), %g2
	sll	%o1, 2, %o1
	ld	[%g2 + %o1], %g3
	and	%o0, PAGE_MASK, %o0
	cmp	%g3, -1
	be	9f
	 add	%o0, %g3, %g3
2:	srl	%g3, 12, %g1		! ppage >> 12

	clr	%o1			! set counter, 0 - 127
	sethi	%hi(KERNBASE + PAGE_SIZE - 0x80000000), %o3
	sethi	%hi(0x80000000), %o4
	sethi	%hi(VIKING_PTAG_VALID | VIKING_PTAG_DIRTY), %o5
	sethi	%hi(2*PAGE_SIZE), %o0
	sethi	%hi(PAGE_SIZE), %g7
	clr	%o2			! block counter, 0 - 3
5:
	sll	%o1, 5, %g4
	or	%g4, %o4, %g4		! 0x80000000 | (set << 5)

	sll	%o2, 26, %g5		! block << 26
6:
	or	%g5, %g4, %g5
	ldda	[%g5] ASI_M_DATAC_TAG, %g2
	cmp	%g3, %g1		! ptag == ppage?
	bne,a	7f
	 inc	%o2

	and	%g2, %o5, %g3		! ptag VALID and DIRTY?
	cmp	%g3, %o5
	bne,a	7f
	 inc	%o2

	add	%g4, %o3, %g2		! (KERNBASE + PAGE_SIZE) | (set << 5)
	ld	[%g2], %g3
	ld	[%g2 + %g7], %g3
	add	%g2, %o0, %g2
	ld	[%g2], %g3
	ld	[%g2 + %g7], %g3
	add	%g2, %o0, %g2
	ld	[%g2], %g3
	ld	[%g2 + %g7], %g3
	add	%g2, %o0, %g2
	ld	[%g2], %g3
	ld	[%g2 + %g7], %g3

	b	8f
	 inc	%o1

7:
	cmp	%o2, 3
	ble	6b
	 sll	%o2, 26, %g5			! block << 26

	inc	%o1
8:
	cmp	%o1, 0x7f
	ble	5b
	 clr	%o2

9:	retl
	 nop

viking_mxcc_flush_page:
	sethi	%hi(C_LABEL(srmmu_v2p_hash)), %g2
	srl	%o0, 24, %o1
	or	%g2, %lo(C_LABEL(srmmu_v2p_hash)), %g2
	sll	%o1, 2, %o1
	ld	[%g2 + %o1], %g3
	and	%o0, PAGE_MASK, %o0
	cmp	%g3, -1
	be	9f
	 add	%o0, %g3, %g3
2:	sub	%g3, -PAGE_SIZE, %g3		! ppage + PAGE_SIZE
	sethi	%hi(MXCC_SRCSTREAM), %o3	! assume %hi(MXCC_SRCSTREAM) == %hi(MXCC_DESTSTREAM)
	mov	0x10, %g2			! set cacheable bit
	or	%o3, %lo(MXCC_SRCSTREAM), %o2
	or	%o3, %lo(MXCC_DESSTREAM), %o3
	sub	%g3, MXCC_STREAM_SIZE, %g3
6:
	stda	%g2, [%o2] ASI_M_MXCC
	stda	%g2, [%o3] ASI_M_MXCC
	andncc	%g3, PAGE_MASK, %g0
	bne	6b
	 sub	%g3, MXCC_STREAM_SIZE, %g3

9:	retl
	 nop

viking_mxcc_flush_chunk:
	retl
	 nop

viking_flush_cache_all:
viking_flush_cache_mm:
viking_flush_cache_range:
viking_flush_cache_page:
	retl
	 nop

viking_flush_tlb_all:
	mov	0x400, %g1
	retl
	 sta	%g0, [%g1] ASI_M_FLUSH_PROBE

viking_flush_tlb_mm:
	mov	SRMMU_CTX_REG, %g1
	ld	[%o0 + AOFF_mm_context], %o1
	lda	[%g1] ASI_M_MMUREGS, %g5
#ifndef __SMP__
	cmp	%o1, -1
	be	1f
#endif
	mov	0x300, %g2
	sta	%o1, [%g1] ASI_M_MMUREGS
	sta	%g0, [%g2] ASI_M_FLUSH_PROBE
	retl
	 sta	%g5, [%g1] ASI_M_MMUREGS
1:	retl
	 nop

viking_flush_tlb_range:
	mov	SRMMU_CTX_REG, %g1
	ld	[%o0 + AOFF_mm_context], %o3
	lda	[%g1] ASI_M_MMUREGS, %g5
#ifndef __SMP__
	cmp	%o3, -1
	be	2f
#endif
	sethi	%hi(~((1 << SRMMU_PGDIR_SHIFT) - 1)), %o4
	sta	%o3, [%g1] ASI_M_MMUREGS
	and	%o1, %o4, %o1
	add	%o1, 0x200, %o1
	sta	%g0, [%o1] ASI_M_FLUSH_PROBE
1:	sub	%o1, %o4, %o1
	cmp	%o1, %o2
	blu,a	1b
	 sta	%g0, [%o1] ASI_M_FLUSH_PROBE
	retl
	 sta	%g5, [%g1] ASI_M_MMUREGS
2:	retl
	 nop

viking_flush_tlb_page:
	ld	[%o0 + 0x00], %o0	/* XXX vma->vm_mm GROSS XXX */
	mov	SRMMU_CTX_REG, %g1
	ld	[%o0 + AOFF_mm_context], %o3
	lda	[%g1] ASI_M_MMUREGS, %g5
#ifndef __SMP__
	cmp	%o3, -1
	be	1f
#endif
	and	%o1, PAGE_MASK, %o1
	sta	%o3, [%g1] ASI_M_MMUREGS
	sta	%g0, [%o1] ASI_M_FLUSH_PROBE
	retl
	 sta	%g5, [%g1] ASI_M_MMUREGS
1:	retl
	 nop

viking_flush_page_to_ram:
viking_flush_page_for_dma:
viking_flush_sig_insns:
	retl
	 nop

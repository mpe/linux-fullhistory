/* s4ckflt.S:  Quick in window kernel faults on the sun4c.
 *
 * Copyright (C) 1995 David S. Miller (davem@caipfs.rutgers.edu)
 */

#include "s4clow.h"

	.text
	.align 8
	.globl	sun4c_quick_kernel_fault
sun4c_quick_kernel_fault:
	sethi	%hi(REAL_PGDIR_MASK), %l5
	and	%l7, %l5, %l7
	sethi	%hi(C_LABEL(invalid_segment)), %l5
	lduba	[%l7] 0x3, %l4
	ld	[%l5 + %lo(C_LABEL(invalid_segment))], %l5
	cmp	%l4, %l5
	bne	segment_loaded
	 nop

	/* We need some breathing room to pull this off, save
	 * away some globals.
	 */
	std	%g0, [REGSAVE_BASE + KFLTREGS + 0x00]
	std	%g2, [REGSAVE_BASE + KFLTREGS + 0x08]
	std	%g4, [REGSAVE_BASE + KFLTREGS + 0x10]
	std	%g6, [REGSAVE_BASE + KFLTREGS + 0x18]
	std	%l0, [REGSAVE_BASE + KFLTREGS + 0x20]
	std	%l2, [REGSAVE_BASE + KFLTREGS + 0x28]

	set	C_LABEL(sun4c_kfree_ring), %g1
	ld	[%g1 + RING_NENTRIES], %g2
	cmp	%g2, 0x0
	be,a	pseg_steal
	 nop

	b	distribute_segmap
	 ld	[%g1 + RING_RINGHD + MMU_ENTRY_NEXT], %g2

pseg_steal:
	/* This is the hard case. */	
	set	C_LABEL(sun4c_kernel_ring), %g1
	ld	[%g1 + RING_RINGHD + MMU_ENTRY_PREV], %g2
	b	kernel_segment_cache_flush
	 ld	[%g2 + MMU_ENTRY_VADDR], %l0

pseg_steal_after_flush:
	ld	[%g2 + MMU_ENTRY_VADDR], %l0
	sethi	%hi(0x30000000), %l1
	lduba	[%l1] 0x02, %g7
	sethi	%hi(C_LABEL(num_contexts)), %g6
	mov	0, %g5
	ld	[%g6 + %lo(C_LABEL(num_contexts))], %g6
1:
	stba	%g5, [%l1] 0x02
	add	%g5, 1, %g5
	cmp	%g5, %g6
	bl	1b
	 stba	%l5, [%l0] 0x03

	stba	%g7, [%l1] 0x02

	

distribute_segmap:
	st	%l7, [%g2 + MMU_ENTRY_VADDR]
	ldub	[%g2 + MMU_ENTRY_PSEG], %g3
	sethi	%hi(0x30000000), %l0
	lduba	[%l0] 0x02, %g7
	sethi	%hi(C_LABEL(num_contexts)), %g6
	mov	0, %g5
	ld	[%g6 + %lo(C_LABEL(num_contexts))], %g6
1:
	stba	%g5, [%l0] 0x02
	add	%g5, 1, %g5
	cmp	%g5, %g6
	bl	1b
	 stba	%g3, [%l7] 0x03

	stba	%g7, [%l0] 0x02

segment_loaded:
	sethi	%hi(VMALLOC_START), %l4
	cmp	%l7, %l4
	bge	vmalloc_kernel_fault
	 nop





vmalloc_kernel_fault:


qkf_exit:
	/* Fault serviced, return from trap, but reload
	 * registers first.
	 */
	ldd	[REGSAVE_BASE + KFLTREGS + 0x00], %g0
	ldd	[REGSAVE_BASE + KFLTREGS + 0x08], %g2
	ldd	[REGSAVE_BASE + KFLTREGS + 0x10], %g4
	ldd	[REGSAVE_BASE + KFLTREGS + 0x18], %g6
	ldd	[REGSAVE_BASE + KFLTREGS + 0x20], %l0
	ldd	[REGSAVE_BASE + KFLTREGS + 0x28], %l2


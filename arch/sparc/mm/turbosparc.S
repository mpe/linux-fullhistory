/* $Id: turbosparc.S,v 1.1 1997/06/24 15:48:05 jj Exp $
 * turbosparc.S: High speed Hypersparc mmu/cache operations.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
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

	.globl	turbosparc_flush_cache_all
	.globl	turbosparc_flush_sig_insns

turbosparc_flush_cache_all:
	WINDOW_FLUSH(%g4, %g5)
	sethi	%hi(vac_cache_size), %g4
	ld	[%g4 + %lo(vac_cache_size)], %g5
	sethi	%hi(vac_line_size), %g1
	ld	[%g1 + %lo(vac_line_size)], %g2
1:	
	subcc	%g5, %g2, %g5
	bne	1b
	 sta	%g0, [%g5] ASI_M_DATAC_TAG
	retl
	 sta	%g0, [%g0] ASI_M_IC_FLCLEAR

turbosparc_flush_sig_insns:
	retl
	 nop

/* s4cflsh.S: Low-level segment cache flush routines, shared by
 *            the kernel and user in-window tlb refill routines.
 *
 * Copyright (C) 1995 David S. Miller (davem@caipfs.rutgers.edu)
 */

#include "s4clow.h"

/* %g1 -- base address of segment to flush
 * %g4 -- pc of caller
 */
	.text
	.align	4
	.globl	sun4c_segment_cache_flush
sun4c_segment_cache_flush:
	std	%g0, [REGSAVE_BASE + FLUSHREGS + 0x0]
	std	%g2, [REGSAVE_BASE + FLUSHREGS + 0x8]
	set	C_LABEL(sun4c_vacinfo), %g2
	ld	[%g2 + VACINFO_HWFLSH], %g3
	cmp	%g3, 0x0
	ld	[%g2 + VACINFO_SIZE], %g3
	add	%g1, %g3, %g3
	be,a	sw_flush
	 ld	[%g2 + VACINFO_LSIZE], %g2

	/* Hardware flush */
	sethi	%hi(PAGE_SIZE), %g2
	sta	%g0, [%g1] 0x05
1:
	add	%g1, %g2, %g1
	cmp	%g1, %g3
	blu,a	1b
	 sta	%g0, [%g1] 0x05

	ldd	[REGSAVE_BASE + FLUSHREGS + 0x0], %g0
	jmpl	%g4, %g0
	 ldd	[REGSAVE_BASE + FLUSHREGS + 0x8], %g2

sw_flush:
	sta	%g0, [%g1] 0x0c
2:
	add	%g1, %g2, %g1
	cmp	%g1, %g3
	blu,a	2b
	 sta	%g0, [%g1] 0x0c

	ldd	[REGSAVE_BASE + FLUSHREGS + 0x0], %g0
	jmpl	%g4, %g0
	 ldd	[REGSAVE_BASE + FLUSHREGS + 0x8], %g2

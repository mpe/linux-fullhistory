/* $Id: s4cflush.S,v 1.8 1995/11/25 00:59:29 davem Exp $
 * s4cflush.S: Inline management of the sun4c cache.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/head.h>
#include <asm/cprefix.h>
#include <asm/asi.h>
#include <asm/page.h>

/* We need to be able to call a routine which will in low-level
 * assembly flush an entire segment from the virtual cache using
 * a base virtual address and that will use as few registers as
 * possible.  The address of that function is stored here.
 * The register usage for this routine is defined as
 * %l2 -- Address of Segment to flush
 * %l4 -- Return address
 * %l6 -- scratch
 * %l7 -- scratch
 */
	.align	4
	.globl	C_LABEL(sun4c_ctxflush), C_LABEL(sun4c_segflush)
	.globl	C_LABEL(sun4c_pgflush)
C_LABEL(sun4c_ctxflush):	.word	C_LABEL(sun4c_ctxflush_sw64KB16B)
C_LABEL(sun4c_segflush):	.word	C_LABEL(sun4c_segflush_sw64KB16B)
C_LABEL(sun4c_pgflush):		.word	C_LABEL(sun4c_pgflush_sw64KB16B)

	.text

	/* Here are the assembly in-line virtual cache flushing
	 * routines on the sun4c.
	 */

	.align 4
	.globl C_LABEL(sun4c_ctxflush_hw64KB16B)

	/* Flush an entire context using hardware assisted flushes
	 * for a cache of size 64KB with 16B lines.
	 */
C_LABEL(sun4c_ctxflush_hw64KB16B):
	sethi	%hi(PAGE_SIZE), %l6
	/* Now flush 16 pages starting at virtual address zero */
	or	%g0, %g0, %l7	
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* One */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Two */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Three */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Four */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Five */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Six */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Seven */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Eight */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Nine */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Ten */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Eleven */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Twelve */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Thirteen */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Fourteen */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Fifteen */
	add	%l7, %l6, %l7

	/* Return to caller and flush the last page */
	jmpl	%l4 + 0x8, %g0
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Sixteen */


	.globl C_LABEL(sun4c_ctxflush_sw64KB16B)

	/* Flush an entire context using software flushes
	 * for a cache of size 64KB with 16B lines.
	 */
C_LABEL(sun4c_ctxflush_sw64KB16B):
	/* Starting at virtual address zero, software flush
	 * 4096 lines at 16 byte intervals.
	 */
#define SWFLUSHCTX16_2LINES \
	sta	%g0, [%l7] ASI_FLUSHCTX; \
	add	%l7, 0x10, %l7; \
	sta	%g0, [%l7] ASI_FLUSHCTX; \
	add	%l7, 0x10, %l7; \

#define SWFLUSHCTX16_4LINES \
	SWFLUSHCTX16_2LINES \
	SWFLUSHCTX16_2LINES \
	
#define SWFLUSHCTX16_8LINES \
	SWFLUSHCTX16_4LINES \
	SWFLUSHCTX16_4LINES \
	
#define SWFLUSHCTX16_16LINES \
	SWFLUSHCTX16_8LINES \
	SWFLUSHCTX16_8LINES \
	
#define SWFLUSHCTX16_32LINES \
	SWFLUSHCTX16_16LINES \
	SWFLUSHCTX16_16LINES \
	
#define SWFLUSHCTX16_64LINES \
	SWFLUSHCTX16_32LINES \
	SWFLUSHCTX16_32LINES \
	
#define SWFLUSHCTX16_128LINES \
	SWFLUSHCTX16_64LINES \
	SWFLUSHCTX16_64LINES \
	
#define SWFLUSHCTX16_256LINES \
	SWFLUSHCTX16_128LINES \
	SWFLUSHCTX16_128LINES \
	
	/* WHEE! */
	or	%g0, %g0, %l7		/* Base register */
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES

	/* Done, return to caller */
	jmpl	%l4 + 0x8, %g0
	nop

	.globl C_LABEL(sun4c_ctxflush_hw64KB32B)
	/* Flush an entire context using hardware assisted flushes
	 * for a cache of size 64KB with 32B lines.
	 */
C_LABEL(sun4c_ctxflush_hw64KB32B):
	sethi	%hi(PAGE_SIZE), %l6
	/* Now flush 16 pages starting at virtual address zero */
	or	%g0, %g0, %l7	
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* One */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Two */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Three */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Four */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Five */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Six */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Seven */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Eight */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Nine */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Ten */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Eleven */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Twelve */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Thirteen */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Fourteen */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Fifteen */
	add	%l7, %l6, %l7

	/* Return to caller and flush the last page */
	jmpl	%l4 + 0x8, %g0
	sta	%g0, [%l7] ASI_HWFLUSHCONTEXT   /* Sixteen */

	.globl C_LABEL(sun4c_ctxflush_sw64KB32B)

	/* Flush an entire context using software flushes
	 * for a cache of size 64KB with 32B lines.
	 */
C_LABEL(sun4c_ctxflush_sw64KB32B):
	/* Starting at virtual address zero, software flush
	 * 2048 lines at 32 byte intervals.
	 */
#define SWFLUSHCTX32_2LINES \
	sta	%g0, [%l7] ASI_FLUSHCTX; \
	add	%l7, 0x20, %l7; \
	sta	%g0, [%l7] ASI_FLUSHCTX; \
	add	%l7, 0x20, %l7; \
	
#define SWFLUSHCTX32_4LINES \
	SWFLUSHCTX32_2LINES \
	SWFLUSHCTX32_2LINES \

#define SWFLUSHCTX32_8LINES \
	SWFLUSHCTX32_4LINES \
	SWFLUSHCTX32_4LINES \

#define SWFLUSHCTX32_16LINES \
	SWFLUSHCTX32_8LINES \
	SWFLUSHCTX32_8LINES \

#define SWFLUSHCTX32_32LINES \
	SWFLUSHCTX32_16LINES \
	SWFLUSHCTX32_16LINES \

#define SWFLUSHCTX32_64LINES \
	SWFLUSHCTX32_32LINES \
	SWFLUSHCTX32_32LINES \

#define SWFLUSHCTX32_128LINES \
	SWFLUSHCTX32_64LINES \
	SWFLUSHCTX32_64LINES \

	/* WHEE! */
	or	%g0, %g0, %l7		/* Base register */
	SWFLUSHCTX32_128LINES
	SWFLUSHCTX32_128LINES
	SWFLUSHCTX32_128LINES
	SWFLUSHCTX32_128LINES
	SWFLUSHCTX32_128LINES
	SWFLUSHCTX32_128LINES
	SWFLUSHCTX32_128LINES
	SWFLUSHCTX32_128LINES
	SWFLUSHCTX32_128LINES
	SWFLUSHCTX32_128LINES
	SWFLUSHCTX32_128LINES
	SWFLUSHCTX32_128LINES
	SWFLUSHCTX32_128LINES
	SWFLUSHCTX32_128LINES
	SWFLUSHCTX16_256LINES
	SWFLUSHCTX16_256LINES

	jmpl	%l4 + 0x8, %g0
	nop

	.align 4
	.globl C_LABEL(sun4c_segflush_hw64KB16B)

	/* Flush an entire segment using hardware assisted flushes
	 * for a cache of size 64KB with 16B lines.
	 */
C_LABEL(sun4c_segflush_hw64KB16B):
	sethi	%hi(PAGE_SIZE), %l6	/* Increment */
	sethi	%hi(0xfffc0000), %l7
	and	%l2, %l7, %l7		/* Base address */

	/* Now flush 16 pages starting at beginning of vseg */
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* One */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Two */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Three */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Four */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Five */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Six */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Seven */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Eight */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Nine */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Ten */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Eleven */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Twelve */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Thirteen */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Fourteen */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Fifteen */
	add	%l7, %l6, %l7

	/* Return to caller and flush the last page */
	jmpl	%l4 + 0x8, %g0
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Sixteen */

	.globl C_LABEL(sun4c_segflush_sw64KB16B)

	/* Flush an entire segment using software flushes
	 * for a cache of size 64KB with 16B lines.
	 */
C_LABEL(sun4c_segflush_sw64KB16B):
	/* Starting at virtual address in %l2, software flush
	 * 4096 lines at 16 byte intervals.
	 */
#define SWFLUSHSEG16_2LINES \
	sta	%g0, [%l7] ASI_FLUSHSEG; \
	add	%l7, 0x10, %l7; \
	sta	%g0, [%l7] ASI_FLUSHSEG; \
	add	%l7, 0x10, %l7; \

#define SWFLUSHSEG16_4LINES \
	SWFLUSHSEG16_2LINES \
	SWFLUSHSEG16_2LINES \

#define SWFLUSHSEG16_8LINES \
	SWFLUSHSEG16_4LINES \
	SWFLUSHSEG16_4LINES \

#define SWFLUSHSEG16_16LINES \
	SWFLUSHSEG16_8LINES \
	SWFLUSHSEG16_8LINES \

#define SWFLUSHSEG16_32LINES \
	SWFLUSHSEG16_16LINES \
	SWFLUSHSEG16_16LINES \

#define SWFLUSHSEG16_64LINES \
	SWFLUSHSEG16_32LINES \
	SWFLUSHSEG16_32LINES \

#define SWFLUSHSEG16_128LINES \
	SWFLUSHSEG16_64LINES \
	SWFLUSHSEG16_64LINES \

#define SWFLUSHSEG16_256LINES \
	SWFLUSHSEG16_128LINES \
	SWFLUSHSEG16_128LINES \

	sethi	%hi(0xfffc0000), %l7
	and	%l2, %l7, %l7		/* Base register */
	SWFLUSHSEG16_256LINES
	SWFLUSHSEG16_256LINES
	SWFLUSHSEG16_256LINES
	SWFLUSHSEG16_256LINES
	SWFLUSHSEG16_256LINES
	SWFLUSHSEG16_256LINES
	SWFLUSHSEG16_256LINES
	SWFLUSHSEG16_256LINES
	SWFLUSHSEG16_256LINES
	SWFLUSHSEG16_256LINES
	SWFLUSHSEG16_256LINES
	SWFLUSHSEG16_256LINES
	SWFLUSHSEG16_256LINES
	SWFLUSHSEG16_256LINES
	SWFLUSHSEG16_256LINES
	SWFLUSHSEG16_256LINES

	/* Done, return to caller */
	jmpl	%l4 + 0x8, %g0
	nop

	.globl C_LABEL(sun4c_segflush_hw64KB32B)

	/* Flush an entire segment using hardware assisted flushes
	 * for a cache of size 64KB with 32B lines.
	 */
C_LABEL(sun4c_segflush_hw64KB32B):
	sethi	%hi(PAGE_SIZE), %l6	/* Increment */
	sethi	%hi(0xfffc0000), %l7
	and	%l2, %l7, %l7		/* Base register */

	/* Now flush 16 pages starting at virtual address in %l7 */
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* One */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Two */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Three */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Four */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Five */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Six */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Seven */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Eight */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Nine */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Ten */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Eleven */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Twelve */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Thirteen */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Fourteen */
	add	%l7, %l6, %l7
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Fifteen */
	add	%l7, %l6, %l7

	/* Return to caller and flush the last page */
	jmpl	%l4 + 0x8, %g0
	sta	%g0, [%l7] ASI_HWFLUSHSEG   /* Sixteen */
	
	.globl C_LABEL(sun4c_segflush_sw64KB32B)

	/* Flush an entire segment using software flushes
	 * for a cache of size 64KB with 32B lines.
	 */
C_LABEL(sun4c_segflush_sw64KB32B):
	/* Starting at virtual address passed in %l2, software
	 * flush 2048 lines at 32 byte intervals.
	 */
#define SWFLUSHSEG32_2LINES \
	sta	%g0, [%l7] ASI_FLUSHSEG; \
	add	%l7, 0x20, %l7; \
	sta	%g0, [%l7] ASI_FLUSHSEG; \
	add	%l7, 0x20, %l7; \

#define SWFLUSHSEG32_4LINES \
	SWFLUSHSEG32_2LINES \
	SWFLUSHSEG32_2LINES \

#define SWFLUSHSEG32_8LINES \
	SWFLUSHSEG32_4LINES \
	SWFLUSHSEG32_4LINES \

#define SWFLUSHSEG32_16LINES \
	SWFLUSHSEG32_8LINES \
	SWFLUSHSEG32_8LINES \

#define SWFLUSHSEG32_32LINES \
	SWFLUSHSEG32_16LINES \
	SWFLUSHSEG32_16LINES \

#define SWFLUSHSEG32_64LINES \
	SWFLUSHSEG32_32LINES \
	SWFLUSHSEG32_32LINES \

#define SWFLUSHSEG32_128LINES \
	SWFLUSHSEG32_64LINES \
	SWFLUSHSEG32_64LINES \

	/* WHEE! */
	sethi	%hi(0xfffc0000), %l7
	and	%l2, %l7, %l7		/* Base register */
	SWFLUSHSEG32_128LINES
	SWFLUSHSEG32_128LINES
	SWFLUSHSEG32_128LINES
	SWFLUSHSEG32_128LINES
	SWFLUSHSEG32_128LINES
	SWFLUSHSEG32_128LINES
	SWFLUSHSEG32_128LINES
	SWFLUSHSEG32_128LINES
	SWFLUSHSEG32_128LINES
	SWFLUSHSEG32_128LINES
	SWFLUSHSEG32_128LINES
	SWFLUSHSEG32_128LINES
	SWFLUSHSEG32_128LINES
	SWFLUSHSEG32_128LINES
	SWFLUSHSEG32_128LINES
	SWFLUSHSEG32_128LINES

	jmpl	%l4 + 0x8, %g0
	nop

	.align 4
	.globl C_LABEL(sun4c_pgflush_hw64KB16B)
	.globl C_LABEL(sun4c_pgflush_hw64KB32B)

	/* Flush a page using hardware assisted flushes
	 * for a cache of size 64KB with 16B or 32B lines.
	 */
C_LABEL(sun4c_pgflush_hw64KB16B):
C_LABEL(sun4c_pgflush_hw64KB32B):
	/* Easiest flush possible on sun4c */
	jmpl	%l4 + 0x8, %g0
	sta	%g0, [%l2] ASI_HWFLUSHPAGE

	.globl	C_LABEL(sun4c_pgflush_sw64KB16B)
	/* Flush a page using software flushes for a cache
	 * of size 64KB with 16B lines.
	 */
C_LABEL(sun4c_pgflush_sw64KB16B):
	/* Flush every line within the page */
#define SWFLUSHPG16_2LINES \
	sta	%g0, [%l7] ASI_FLUSHPG; \
	add	%l7, 0x10, %l7; \
	sta	%g0, [%l7] ASI_FLUSHPG; \
	add	%l7, 0x10, %l7; \

#define SWFLUSHPG16_4LINES \
	SWFLUSHPG16_2LINES \
	SWFLUSHPG16_2LINES \

#define SWFLUSHPG16_8LINES \
	SWFLUSHPG16_4LINES \
	SWFLUSHPG16_4LINES \

#define SWFLUSHPG16_16LINES \
	SWFLUSHPG16_8LINES \
	SWFLUSHPG16_8LINES \

#define SWFLUSHPG16_32LINES \
	SWFLUSHPG16_16LINES \
	SWFLUSHPG16_16LINES \

#define SWFLUSHPG16_64LINES \
	SWFLUSHPG16_32LINES \
	SWFLUSHPG16_32LINES \

#define SWFLUSHPG16_128LINES \
	SWFLUSHPG16_64LINES \
	SWFLUSHPG16_64LINES \

#define SWFLUSHPG16_256LINES \
	SWFLUSHPG16_128LINES \
	SWFLUSHPG16_128LINES \

	or	%l2, %g0, %l7		/* Base address of page */
	SWFLUSHPG16_256LINES

	/* Return to caller and flush last line */
	jmpl	%l4 + 0x8, %g0
	nop

	.globl	C_LABEL(sun4c_pgflush_sw64KB32B)
	/* Flush a page using software flushes for a cache
	 * of size 64KB with 32B lines.
	 */
C_LABEL(sun4c_pgflush_sw64KB32B):
	/* Flush every line within the page */
#define SWFLUSHPG32_2LINES \
	sta	%g0, [%l7] ASI_FLUSHPG; \
	add	%l7, 0x20, %l7; \
	sta	%g0, [%l7] ASI_FLUSHPG; \
	add	%l7, 0x20, %l7; \

#define SWFLUSHPG32_4LINES \
	SWFLUSHPG32_2LINES \
	SWFLUSHPG32_2LINES \

#define SWFLUSHPG32_8LINES \
	SWFLUSHPG32_4LINES \
	SWFLUSHPG32_4LINES \

#define SWFLUSHPG32_16LINES \
	SWFLUSHPG32_8LINES \
	SWFLUSHPG32_8LINES \

#define SWFLUSHPG32_32LINES \
	SWFLUSHPG32_16LINES \
	SWFLUSHPG32_16LINES \

#define SWFLUSHPG32_64LINES \
	SWFLUSHPG32_32LINES \
	SWFLUSHPG32_32LINES \

#define SWFLUSHPG32_128LINES \
	SWFLUSHPG32_64LINES \
	SWFLUSHPG32_64LINES \

	or	%l2, %g0, %l7		/* Base address of page */
	SWFLUSHPG32_128LINES

	/* Return to caller and flush last line */
	jmpl	%l4 + 0x8, %g0
	nop

/* All that crap just to flush a fucking virtual cache, sheesh... */

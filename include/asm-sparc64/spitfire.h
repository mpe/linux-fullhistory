/* $Id: spitfire.h,v 1.2 1996/12/28 18:39:55 davem Exp $
 * spitfire.h: SpitFire/BlackBird/Cheetah inline MMU operations.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_SPITFIRE_H
#define _SPARC64_SPITFIRE_H

#include <asm/asi.h>

/* The following register addresses are accessible via ASI_DMMU
 * and ASI_IMMU, that is there is a distinct and unique copy of
 * each these registers for each TLB.
 */
#define TSB_TAG_TARGET		0x0000000000000000
#define TLB_SFSR		0x0000000000000018
#define TSB_REG			0x0000000000000028
#define TLB_TAG_ACCESS		0x0000000000000030

/* These registers only exist as one entity, and are accessed
 * via ASI_DMMU only.
 */
#define PRIMARY_CONTEXT		0x0000000000000008
#define SECONDARY_CONTEXT	0x0000000000000010
#define DMMU_SFAR		0x0000000000000020
#define VIRT_WATCHPOINT		0x0000000000000038
#define PHYS_WATCHPOINT		0x0000000000000040

#ifndef __ASSEMBLY__

extern __inline__ unsigned long spitfire_get_primary_context(void)
{
	unsigned long ctx;

	__asm__ __volatile__("ldxa	[%1] %2, %0"
			     : "=r" (ctx)
			     : "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));
}

extern __inline__ unsigned long spitfire_set_primary_context(unsigned long ctx)
{
	__asm__ __volatile__("stxa	%0, [%1] %2"
			     : : "r" (ctx), "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));
}

extern __inline__ unsigned long spitfire_get_secondary_context(void)
{
	unsigned long ctx;

	__asm__ __volatile__("ldxa	[%1] %2, %0"
			     : "=r" (ctx)
			     : "r" (SECONDARY_CONTEXT), "i" (ASI_DMMU));
}

extern __inline__ unsigned long spitfire_set_secondary_context(unsigned long ctx)
{
	__asm__ __volatile__("stxa	%0, [%1] %2"
			     : : "r" (ctx), "r" (SECONDARY_CONTEXT), "i" (ASI_DMMU));
}

/* The data cache is write through, so this just invalidates the
 * specified line.
 */
extern __inline__ void spitfire_put_dcache_tag(unsigned long addr, unsigned long tag)
{
	__asm__ __volatile__("stxa	%0, [%1] %2"
			     : /* No outputs */
			     : "r" (tag), "r" (addr), "i" (ASI_DCACHE_TAG));
}

/* The instruction cache lines are flushed with this, but note that
 * this does not flush the pipeline.  It is possible for a line to
 * get flushed but stale instructions to still be in the pipeline,
 * a flush instruction (to any address) is sufficient to handle
 * this issue after the line is invalidated.
 */
extern __inline__ void spitfire_put_icache_tag(unsigned long addr, unsigned long tag)
{
	__asm__ __volatile__("stxa	%0, [%1] %2"
			     : /* No outputs */
			     : "r" (tag), "r" (addr), "i" (ASI_IC_TAG));
}

extern __inline__ unsigned long spitfire_get_dtlb_data(int entry)
{
	unsigned long data;

	__asm__ __volatile__("ldxa	[%1] %2, %0"
			     : "=r" (data)
			     : "r" (entry << 3), "i" (ASI_DTLB_DATA_ACCESS));
	return data;
}

extern __inline__ unsigned long spitfire_put_dtlb_data(int entry, unsigned long data)
{
	__asm__ __volatile__("stxa	%0, [%1] %2"
			     : : "r" (data), "r" (entry << 3),
			         "i" (ASI_DTLB_DATA_ACCESS));
}

extern __inline__ unsigned long spitfire_get_itlb_data(int entry)
{
	unsigned long data;

	__asm__ __volatile__("ldxa	[%1] %2, %0"
			     : "=r" (data)
			     : "r" (entry << 3), "i" (ASI_ITLB_DATA_ACCESS));
	return data;
}

extern __inline__ unsigned long spitfire_put_itlb_data(int entry, unsigned long data)
{
	__asm__ __volatile__("stxa	%0, [%1] %2"
			     : : "r" (data), "r" (entry << 3),
			         "i" (ASI_ITLB_DATA_ACCESS));
}

/* Spitfire hardware assisted TLB flushes. */

/* Context level flushes. */
extern __inline__ void spitfire_flush_dtlb_primary_context(void)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1"
			     : : "r" (0x40), "i" (ASI_DMMU_DEMAP));
}

extern __inline__ void spitfire_flush_itlb_primary_context(void)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1"
			     : : "r" (0x40), "i" (ASI_IMMU_DEMAP));
}

extern __inline__ void spitfire_flush_dtlb_secondary_context(void)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1"
			     : : "r" (0x41), "i" (ASI_DMMU_DEMAP));
}

extern __inline__ void spitfire_flush_itlb_secondary_context(void)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1"
			     : : "r" (0x41), "i" (ASI_IMMU_DEMAP));
}

extern __inline__ void spitfire_flush_dtlb_nucleus_context(void)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1"
			     : : "r" (0x42), "i" (ASI_DMMU_DEMAP));
}

extern __inline__ void spitfire_flush_itlb_nucleus_context(void)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1"
			     : : "r" (0x42), "i" (ASI_IMMU_DEMAP));
}

/* Page level flushes. */
extern __inline__ void spitfire_flush_dtlb_primary_page(unsigned long page)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1"
			     : : "r" (page), "i" (ASI_DMMU_DEMAP));
}

extern __inline__ void spitfire_flush_itlb_primary_page(unsigned long page)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1"
			     : : "r" (page), "i" (ASI_IMMU_DEMAP));
}

extern __inline__ void spitfire_flush_dtlb_secondary_page(unsigned long page)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1"
			     : : "r" (page | 0x10), "i" (ASI_DMMU_DEMAP));
}

extern __inline__ void spitfire_flush_itlb_secondary_page(unsigned long page)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1"
			     : : "r" (page | 0x10), "i" (ASI_IMMU_DEMAP));
}

extern __inline__ void spitfire_flush_dtlb_nucleus_page(unsigned long page)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1"
			     : : "r" (page | 0x20), "i" (ASI_DMMU_DEMAP));
}

extern __inline__ void spitfire_flush_itlb_nucleus_page(unsigned long page)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1"
			     : : "r" (page | 0x20), "i" (ASI_IMMU_DEMAP));
}

#endif /* !(__ASSEMBLY__) */

#endif /* !(_SPARC64_SPITFIRE_H) */

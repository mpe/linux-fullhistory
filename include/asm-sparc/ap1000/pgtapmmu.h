  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/* 
 * based on pgtsrmmu.h
 *
 */

#ifndef _SPARC_PGTAPMMU_H
#define _SPARC_PGTAPMMU_H

#include <asm/page.h>
#include <asm/ap1000/apreg.h>


/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define APMMU_PMD_SHIFT         18
#define APMMU_PMD_SIZE          (1UL << APMMU_PMD_SHIFT)
#define APMMU_PMD_MASK          (~(APMMU_PMD_SIZE-1))
#define APMMU_PMD_ALIGN(addr)   (((addr)+APMMU_PMD_SIZE-1)&APMMU_PMD_MASK)

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define APMMU_PGDIR_SHIFT       24
#define APMMU_PGDIR_SIZE        (1UL << APMMU_PGDIR_SHIFT)
#define APMMU_PGDIR_MASK        (~(APMMU_PGDIR_SIZE-1))
#define APMMU_PGDIR_ALIGN(addr) (((addr)+APMMU_PGDIR_SIZE-1)&APMMU_PGDIR_MASK)

#define APMMU_PTRS_PER_PTE      64
#define APMMU_PTRS_PER_PMD      64
#define APMMU_PTRS_PER_PGD      256

#define APMMU_PTE_TABLE_SIZE    0x100 /* 64 entries, 4 bytes a piece */
#define APMMU_PMD_TABLE_SIZE    0x100 /* 64 entries, 4 bytes a piece */
#define APMMU_PGD_TABLE_SIZE    0x400 /* 256 entries, 4 bytes a piece */

#define APMMU_VMALLOC_START   (0xfe300000)
#define APMMU_VMALLOC_END     ~0x0UL

/* Definition of the values in the ET field of PTD's and PTE's */
#define APMMU_ET_MASK         0x3
#define APMMU_ET_INVALID      0x0
#define APMMU_ET_PTD          0x1
#define APMMU_ET_PTE          0x2
#define APMMU_ET_REPTE        0x3 /* AIEEE, SuperSparc II reverse endian page! */

/* Physical page extraction from PTP's and PTE's. */
#define APMMU_CTX_PMASK    0xfffffff0
#define APMMU_PTD_PMASK    0xfffffff0
#define APMMU_PTE_PMASK    0xffffff00

/* The pte non-page bits.  Some notes:
 * 1) cache, dirty, valid, and ref are frobbable
 *    for both supervisor and user pages.
 * 2) exec and write will only give the desired effect
 *    on user pages
 * 3) use priv and priv_readonly for changing the
 *    characteristics of supervisor ptes
 */
#define APMMU_CACHE        0x80
#define APMMU_DIRTY        0x40
#define APMMU_REF          0x20
#define APMMU_EXEC         0x08
#define APMMU_WRITE        0x04
#define APMMU_VALID        0x02 /* APMMU_ET_PTE */
#define APMMU_PRIV         0x1c
#define APMMU_PRIV_RDONLY  0x18

#define APMMU_CHG_MASK    (0xffffff00 | APMMU_REF | APMMU_DIRTY)

/*
 * "normal" sun systems have their memory on bus 0. This means the top
 * 4 bits of 36 bit physical addresses are 0. We use this define to
 * determine if a piece of memory might be normal memory, or if its
 * definately some sort of device memory.  
 *
 * On the AP+ normal memory is on bus 8. Why? Ask Fujitsu :-)
*/
#define MEM_BUS_SPACE 8

/* Some day I will implement true fine grained access bits for
 * user pages because the APMMU gives us the capabilities to
 * enforce all the protection levels that vma's can have.
 * XXX But for now...
 */
#define APMMU_PAGE_NONE    __pgprot((MEM_BUS_SPACE<<28) | \
				    APMMU_VALID | APMMU_CACHE | \
				    APMMU_PRIV | APMMU_REF)
#define APMMU_PAGE_SHARED  __pgprot((MEM_BUS_SPACE<<28) | \
				    APMMU_VALID | APMMU_CACHE | \
				    APMMU_EXEC | APMMU_WRITE | APMMU_REF)
#define APMMU_PAGE_COPY    __pgprot((MEM_BUS_SPACE<<28) | \
				    APMMU_VALID | APMMU_CACHE | \
				    APMMU_EXEC | APMMU_REF)
#define APMMU_PAGE_RDONLY  __pgprot((MEM_BUS_SPACE<<28) | \
				    APMMU_VALID | APMMU_CACHE | \
				    APMMU_EXEC | APMMU_REF)
#define APMMU_PAGE_KERNEL  __pgprot((MEM_BUS_SPACE<<28) | \
				    APMMU_VALID | APMMU_CACHE | APMMU_PRIV | \
				    APMMU_DIRTY | APMMU_REF)

#define APMMU_CTXTBL_PTR         0x00000100
#define APMMU_CTX_REG            0x00000200

extern __inline__ unsigned long apmmu_get_ctable_ptr(void)
{
	unsigned int retval;

	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (retval) :
			     "r" (APMMU_CTXTBL_PTR),
			     "i" (ASI_M_MMUREGS));
	return (retval & APMMU_CTX_PMASK) << 4;
}

extern __inline__ void apmmu_set_context(int context)
{
	__asm__ __volatile__("sta %0, [%1] %2\n\t" : :
			     "r" (context), "r" (APMMU_CTX_REG),
			     "i" (ASI_M_MMUREGS) : "memory");
	/* The AP1000+ message controller also needs to know
	   the current task's context. */
	MSC_OUT(MSC_PID, context);
}

extern __inline__ int apmmu_get_context(void)
{
	register int retval;
	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (retval) :
			     "r" (APMMU_CTX_REG),
			     "i" (ASI_M_MMUREGS));
	return retval;
}

#endif /* !(_SPARC_PGTAPMMU_H) */



/* $Id: pgtsun4c.h,v 1.24 1996/03/26 06:51:56 miguel Exp $
 * pgtsun4c.h:  Sun4c specific pgtable.h defines and code.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_PGTSUN4C_H
#define _SPARC_PGTSUN4C_H

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define SUN4C_PMD_SHIFT       22
#define SUN4C_PMD_SIZE        (1UL << SUN4C_PMD_SHIFT)
#define SUN4C_PMD_MASK        (~(SUN4C_PMD_SIZE-1))
#define SUN4C_PMD_ALIGN(addr) (((addr)+SUN4C_PMD_SIZE-1)&SUN4C_PMD_MASK)

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define SUN4C_PGDIR_SHIFT       22
#define SUN4C_PGDIR_SIZE        (1UL << SUN4C_PGDIR_SHIFT)
#define SUN4C_PGDIR_MASK        (~(SUN4C_PGDIR_SIZE-1))
#define SUN4C_PGDIR_ALIGN(addr) (((addr)+SUN4C_PGDIR_SIZE-1)&SUN4C_PGDIR_MASK)

/* To represent how the sun4c mmu really lays things out. */
#define SUN4C_REAL_PGDIR_SHIFT       18
#define SUN4C_REAL_PGDIR_SIZE        (1UL << SUN4C_REAL_PGDIR_SHIFT)
#define SUN4C_REAL_PGDIR_MASK        (~(SUN4C_REAL_PGDIR_SIZE-1))
#define SUN4C_REAL_PGDIR_ALIGN(addr) (((addr)+SUN4C_REAL_PGDIR_SIZE-1)&SUN4C_REAL_PGDIR_MASK)

/*
 * To be efficient, and not have to worry about allocating such
 * a huge pgd, we make the kernel sun4c tables each hold 1024
 * entries and the pgd similarly just like the i386 tables.
 */
#define SUN4C_PTRS_PER_PTE    1024
#define SUN4C_PTRS_PER_PMD    1
#define SUN4C_PTRS_PER_PGD    1024

/* On the sun4c the physical ram limit is 128MB.  We set up our I/O
 * translations at KERNBASE + 128MB for 1MB, then we begin the VMALLOC
 * area, makes sense.  This works out to the value below.
 */
#define SUN4C_VMALLOC_START   (0xfe200000)

/*
 * Sparc SUN4C pte fields.
 */
#define _SUN4C_PAGE_VALID     0x80000000   /* valid page */
#define _SUN4C_PAGE_WRITE     0x40000000   /* can be written to */
#define _SUN4C_PAGE_PRIV      0x20000000   /* bit to signify privileged page */
#define _SUN4C_PAGE_USER      0x00000000   /* User page */
#define _SUN4C_PAGE_NOCACHE   0x10000000   /* non-cacheable page */
#define _SUN4C_PAGE_IO        0x04000000   /* I/O page */
#define _SUN4C_PAGE_REF       0x02000000   /* Page has been accessed/referenced */
#define _SUN4C_PAGE_DIRTY     0x01000000   /* Page has been modified, is dirty */

#define _SUN4C_PAGE_CHG_MASK  (0xffff | _SUN4C_PAGE_REF | _SUN4C_PAGE_DIRTY)

#define SUN4C_PAGE_NONE     __pgprot(_SUN4C_PAGE_VALID | _SUN4C_PAGE_PRIV | \
				     _SUN4C_PAGE_REF)
#define SUN4C_PAGE_SHARED   __pgprot(_SUN4C_PAGE_VALID | _SUN4C_PAGE_WRITE | \
				     _SUN4C_PAGE_USER | _SUN4C_PAGE_REF)
#define SUN4C_PAGE_COPY     __pgprot(_SUN4C_PAGE_VALID | _SUN4C_PAGE_USER | \
				     _SUN4C_PAGE_REF)
#define SUN4C_PAGE_READONLY __pgprot(_SUN4C_PAGE_VALID | _SUN4C_PAGE_USER | \
				     _SUN4C_PAGE_REF)
#define SUN4C_PAGE_KERNEL   __pgprot(_SUN4C_PAGE_VALID | _SUN4C_PAGE_WRITE | \
				     _SUN4C_PAGE_PRIV | _SUN4C_PAGE_DIRTY | \
				     _SUN4C_PAGE_REF | _SUN4C_PAGE_NOCACHE)

extern __inline__ unsigned long sun4c_get_synchronous_error(void)
{
	unsigned long sync_err;

	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (sync_err) :
			     "r" (AC_SYNC_ERR), "i" (ASI_CONTROL));
	return sync_err;
}

extern __inline__ unsigned long sun4c_get_synchronous_address(void)
{
	unsigned long sync_addr;

	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (sync_addr) :
			     "r" (AC_SYNC_VA), "i" (ASI_CONTROL));
	return sync_addr;
}

/* SUN4C pte, segmap, and context manipulation */
extern __inline__ unsigned long sun4c_get_segmap(unsigned long addr)
{
  register unsigned long entry;

  __asm__ __volatile__("\n\tlduba [%1] %2, %0\n\t" : 
		       "=r" (entry) :
		       "r" (addr), "i" (ASI_SEGMAP));

  return entry;
}

extern __inline__ void sun4c_put_segmap(unsigned long addr, unsigned long entry)
{

  __asm__ __volatile__("\n\tstba %1, [%0] %2\n\t" : :
		       "r" (addr), "r" (entry),
		       "i" (ASI_SEGMAP));

  return;
}

extern __inline__ unsigned long sun4c_get_pte(unsigned long addr)
{
  register unsigned long entry;

  __asm__ __volatile__("\n\tlda [%1] %2, %0\n\t" : 
		       "=r" (entry) :
		       "r" (addr), "i" (ASI_PTE));
  return entry;
}

extern __inline__ void sun4c_put_pte(unsigned long addr, unsigned long entry)
{
  __asm__ __volatile__("\n\tsta %1, [%0] %2\n\t" : :
		       "r" (addr), 
		       "r" (entry), "i" (ASI_PTE));

  return;
}

extern __inline__ int sun4c_get_context(void)
{
  register int ctx;

  __asm__ __volatile__("\n\tlduba [%1] %2, %0\n\t" :
		       "=r" (ctx) :
		       "r" (AC_CONTEXT), "i" (ASI_CONTROL));

  return ctx;
}

extern __inline__ int sun4c_set_context(int ctx)
{
  __asm__ __volatile__("\n\tstba %0, [%1] %2\n\t" : :
		       "r" (ctx), "r" (AC_CONTEXT), "i" (ASI_CONTROL));

  return ctx;
}

#endif /* !(_SPARC_PGTSUN4C_H) */

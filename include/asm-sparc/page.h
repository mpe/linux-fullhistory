/* page.h:  Various defines and such for MMU operations on the Sparc for
            the Linux kernel.

   Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
*/

#ifndef _SPARC_PAGE_H
#define _SPARC_PAGE_H

#include <asm/asi.h>        /* for get/set segmap/pte routines */
#include <asm/contregs.h>   /* for switch_to_context */

#define PAGE_SHIFT   12             /* This is the virtual page... */

#ifndef __ASSEMBLY__
#define PAGE_SIZE    (1UL << PAGE_SHIFT)

/* to mask away the intra-page address bits */
#define PAGE_MASK         (~(PAGE_SIZE-1))

#ifdef __KERNEL__

/* The following structure is used to hold the physical
 * memory configuration of the machine.  This is filled
 * in probe_memory() and is later used by mem_init() to
 * set up mem_map[].  We statically allocate 14 of these
 * structs, this is arbitrary.  The entry after the last
 * valid one has num_bytes==0.
 */

struct sparc_phys_banks {
  unsigned long base_addr;
  unsigned long num_bytes;
};

#define CONFIG_STRICT_MM_TYPECHECKS

#ifdef CONFIG_STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)      ((x).pmd)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pmd(x)        ((pmd_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#else
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)	(x)
#define pmd_val(x)      (x)
#define pgd_val(x)	(x)
#define pgprot_val(x)	(x)

#define __pte(x)	(x)
#define __pmd(x)        (x)
#define __pgd(x)	(x)
#define __pgprot(x)	(x)

#endif

/* The current va context is global and known, so all that is needed to
 * do an invalidate is flush the VAC.
 */

#define invalidate() flush_vac_context()  /* how conveeeiiiiinnnient :> */

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)  (((addr)+PAGE_SIZE-1)&PAGE_MASK)

#define PAGE_OFFSET    0
#define MAP_NR(addr) (((unsigned long)(addr)) >> PAGE_SHIFT)
#define MAP_PAGE_RESERVED (1<<15)


#endif /* !(__ASSEMBLY__) */

/* The rest is kind of funky because on the sparc, the offsets into the mmu 
 * entries are encoded in magic alternate address space tables. I will 
 * probably find some nifty inline assembly routines to do the equivalent. 
 * Much thought must go into this code.   (davem@caip.rutgers.edu)
 */

/* Bitfields within a Sparc sun4c PTE (page table entry). */

#define PTE_V     0x80000000   /* valid bit */
#define PTE_ACC   0x60000000   /* access bits */
#define PTE_W     0x40000000   /* writable bit */
#define PTE_P     0x20000000   /* privileged page */
#define PTE_NC    0x10000000   /* page is non-cacheable */
#define PTE_TYP   0x0c000000   /* page type field */
#define PTE_RMEM  0x00000000   /* type == on board real memory */
#define PTE_IO    0x04000000   /* type == i/o area */
#define PTE_VME16 0x08000000   /* type == 16-bit VME area */
#define PTE_VME32 0x0c000000   /* type == 32-bit VME area */
#define PTE_R     0x02000000   /* page has been referenced */
#define PTE_M     0x01000000   /* page has been modified */
#define PTE_RESV  0x00f80000   /* reserved bits */
#define PTE_PHYPG 0x0007ffff   /* phys pg number, sun4c only uses 16bits */

extern __inline__ unsigned long get_segmap(unsigned long addr)
{
  register unsigned long entry;

  __asm__ __volatile__("lduba [%1] 0x3, %0" : 
		       "=r" (entry) :
		       "r" (addr));

  return (entry&0x7f);
}

extern __inline__ void put_segmap(unsigned long addr, unsigned long entry)
{

  __asm__ __volatile__("stba %1, [%0] 0x3" : : "r" (addr), "r" (entry&0x7f));

  return;
}

extern __inline__ unsigned long get_pte(unsigned long addr)
{
  register unsigned long entry;

  __asm__ __volatile__("lda [%1] 0x4, %0" : 
		       "=r" (entry) :
		       "r" (addr));
  return entry;
}

extern __inline__ void put_pte(unsigned long addr, unsigned long entry)
{
  __asm__ __volatile__("sta %1, [%0] 0x4" : :
		       "r" (addr), 
		       "r" (entry));

  return;
}

extern __inline__ void switch_to_context(int context)
{
  __asm__ __volatile__("stba %0, [%1] 0x2" : :
		       "r" (context),
		       "r" (0x30000000));		       

  return;
}

extern __inline__ int get_context(void)
{
  register int ctx;

  __asm__ __volatile__("lduba [%1] 0x2, %0" :
		       "=r" (ctx) :
		       "r" (0x30000000));

  return ctx;
}

typedef unsigned short mem_map_t;

#endif /* __KERNEL__ */

#endif /* _SPARC_PAGE_H */

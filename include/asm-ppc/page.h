#ifndef _PPC_PAGE_H
#define _PPC_PAGE_H

#include <linux/config.h>

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

/* This handles the memory map.. */

/*
 * these virtual mappings for prep and pmac
 * on the prep machine the io areas are at different physical locations
 * than their virtual address.  On the pmac the io areas
 * are mapped 1-1 virtual/physical.
 * -- Cort
 */
#ifdef CONFIG_PREP
#define KERNELBASE	0x90000000
#endif
#ifdef CONFIG_PMAC
#define KERNELBASE	0xc0000000
#endif
#define PAGE_OFFSET	KERNELBASE


#ifndef __ASSEMBLY__
#ifdef __KERNEL__

#define STRICT_MM_TYPECHECKS

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((x).pmd)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pmd(x)	((pmd_t) { (x) } )
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
#define pmd_val(x)	(x)
#define pgd_val(x)	(x)
#define pgprot_val(x)	(x)

#define __pte(x)	(x)
#define __pmd(x)	(x)
#define __pgd(x)	(x)
#define __pgprot(x)	(x)

#endif


/* align addr on a size boundry - adjust address up if needed -- Cort */
#define _ALIGN(addr,size)	(((addr)+size-1)&(~(size-1)))

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)


#define clear_page(page)        memset((void *)(page), 0, PAGE_SIZE)
#define copy_page(to,from)	memcpy((void *)(to), (void *)(from), PAGE_SIZE)
/* map phys->virtual and virtual->phys */
#define __pa(x)			((unsigned long)(x)-PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))

#define MAP_NR(addr)	(__pa(addr) >> PAGE_SHIFT)
#define MAP_PAGE_RESERVED	(1<<15)

extern __inline__ unsigned long get_prezerod_page(void);

#endif /* __KERNEL__ */
#endif /* __ASSEMBLY__ */
#endif /* _PPC_PAGE_H */

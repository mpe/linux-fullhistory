#ifndef __ASM_MIPS_PAGE_H
#define __ASM_MIPS_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__

#define STRICT_MM_TYPECHECKS

#ifndef __LANGUAGE_ASSEMBLY__

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
#define __pme(x)	((pme_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#else /* !defined (STRICT_MM_TYPECHECKS) */
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

#endif /* !defined (STRICT_MM_TYPECHECKS) */

/*
 * We need a special version of copy_page that can handle virtual caches.
 * While we're at tweaking with caches we can use that to make it even
 * faster.  The R10000 accelerated caching mode will further accelerate it.
 */
extern void __copy_page(unsigned long from, unsigned long to);
#define copy_page(from,to) __copy_page((unsigned long)from, (unsigned long)to)

#endif /* __LANGUAGE_ASSEMBLY__ */

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

/* This handles the memory map */
#if __mips == 3
/*
 * We handle pages at XKPHYS + 0x1800000000000000 (cachable, noncoherent)
 * Pagetables are at  XKPHYS + 0x1000000000000000 (uncached)
 */
#define PAGE_OFFSET	0x9800000000000000UL
#define PT_OFFSET	0x9000000000000000UL
#define MAP_MASK        0x07ffffffffffffffUL
#else
/*
 * We handle pages at KSEG0 (cachable, noncoherent)
 * Pagetables are at  KSEG1 (uncached)
 */
#define PAGE_OFFSET	0x80000000
#define PT_OFFSET	0xa0000000
#define MAP_MASK        0x1fffffff
#endif

#define MAP_NR(addr)	((((unsigned long)(addr)) & MAP_MASK) >> PAGE_SHIFT)

#ifndef __LANGUAGE_ASSEMBLY__

extern unsigned long page_colour_mask;

extern inline unsigned long
page_colour(unsigned long page)
{
	return page & page_colour_mask;
}

#endif /* defined (__LANGUAGE_ASSEMBLY__) */
#endif /* defined (__KERNEL__) */

#endif /* __ASM_MIPS_PAGE_H */

#ifndef _M68K_PAGE_H
#define _M68K_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__

#define STRICT_MM_TYPECHECKS

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd[16]; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((&x)->pmd[0])
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
typedef struct { unsigned long pmd[16]; } pmd_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)	(x)
#define pmd_val(x)	((&x)->pmd[0])
#define pgd_val(x)	(x)
#define pgprot_val(x)	(x)

#define __pte(x)	(x)
#define __pmd(x)	((pmd_t) { (x) } )
#define __pgd(x)	(x)
#define __pgprot(x)	(x)

#endif

/* m68k_is040or060 is != 0 for a '040 or higher; used numbers are 4 for 68040
 * and 6 for 68060 */
extern int m68k_is040or060;

/* This is the cache mode to be used for pages containing page descriptors for
 * processors >= '040. It is in pte_mknocache(), and the variable is defined
 * and initialized in head.S */
extern int m68k_pgtable_cachemode;

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

/* This handles the memory map.. */
#define PAGE_OFFSET		0
#define MAP_NR(addr)		(((unsigned long)(addr)) >> PAGE_SHIFT)
#define MAP_PAGE_RESERVED	(1<<15)

#endif /* __KERNEL__ */

#endif /* _M68K_PAGE_H */

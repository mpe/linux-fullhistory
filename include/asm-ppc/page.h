#include <linux/config.h>

#ifndef _PPC_PAGE_H
#define _PPC_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#define PAGE_OFFSET	0xc0000000


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
/* map phys->virtual and virtual->phys for RAM pages */
#ifdef CONFIG_APUS
#include <asm/amigappc.h>
/* Word at CYBERBASEp has the value (-KERNELBASE+CYBERBASE). */
#define __pa(x)			((unsigned long)(x)+(*(unsigned long*)CYBERBASEp))
#define __va(x)			((void *)((unsigned long)(x)-(*(unsigned long*)CYBERBASEp)))
#else
#define __pa(x)			((unsigned long)(x)-PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))
#endif

#define MAP_NR(addr)		(((unsigned long)addr-PAGE_OFFSET) >> PAGE_SHIFT)
#define MAP_PAGE_RESERVED	(1<<15)

extern unsigned long get_zero_page_fast(void);
#endif /* __KERNEL__ */
#endif /* __ASSEMBLY__ */
#endif /* _PPC_PAGE_H */

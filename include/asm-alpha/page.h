#ifndef _ALPHA_PAGE_H
#define _ALPHA_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	13
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__

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
#define pmd_val(x)	((x).pmd)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
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
#define __pgd(x)	(x)
#define __pgprot(x)	(x)

#endif

#define invalidate_all() \
__asm__ __volatile__( \
	"lda $16,-2($31)\n\t" \
	".long 51" \
	: : :"$1", "$16", "$17", "$22","$23","$24","$25")

#define invalidate() \
__asm__ __volatile__( \
	"lda $16,-1($31)\n\t" \
	".long 51" \
	: : :"$1", "$16", "$17", "$22","$23","$24","$25")

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)		(((addr)+PAGE_SIZE-1)&PAGE_MASK)

#define PAGE_OFFSET		0xFFFFFC0000000000
#define MAP_NR(addr)		((((unsigned long) (addr)) - PAGE_OFFSET) >> PAGE_SHIFT)
#define MAP_PAGE_RESERVED	(1<<31)

typedef unsigned int mem_map_t;

#endif /* __KERNEL__ */

#endif /* _ALPHA_PAGE_H */

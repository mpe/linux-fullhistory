#ifndef _ALPHA_PAGE_H
#define _ALPHA_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	13
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__

#define STRICT_MM_TYPECHECKS

/*
 * A _lot_ of the kernel time is spent clearing pages, so
 * do this as fast as we possibly can. Also, doing this
 * as a separate inline function (rather than memset())
 * results in clearer kernel profiles as we see _who_ is
 * doing page clearing or copying.
 */
static inline void clear_page(unsigned long page)
{
	unsigned long count;
	__asm__ __volatile__(
		".align 4\n"
		"1:\n\t"
		"stq $31,0(%1)\n\t"
		"stq $31,8(%1)\n\t"
		"stq $31,16(%1)\n\t"
		"stq $31,24(%1)\n\t"
		"subq %0,1,%0\n\t"
		"stq $31,32(%1)\n\t"
		"stq $31,40(%1)\n\t"
		"stq $31,48(%1)\n\t"
		"stq $31,56(%1)\n\t"
		"addq $1,64,$1\n\t"
		"bne %0,1b"
		:"=r" (count),"=r" (page)
		:"0" (PAGE_SIZE/64), "1" (page));
}

static inline void copy_page(unsigned long to, unsigned long from)
{
	unsigned long count;
	__asm__ __volatile__(
		".align 4\n"
		"1:\n\t"
		"ldq $0,0(%1)\n\t"
		"ldq $1,8(%1)\n\t"
		"ldq $2,16(%1)\n\t"
		"ldq $3,24(%1)\n\t"
		"ldq $4,32(%1)\n\t"
		"ldq $5,40(%1)\n\t"
		"ldq $6,48(%1)\n\t"
		"ldq $7,56(%1)\n\t"
		"subq %0,1,%0\n\t"
		"addq %1,64,%1\n\t"
		"stq $0,0(%2)\n\t"
		"stq $1,8(%2)\n\t"
		"stq $2,16(%2)\n\t"
		"stq $3,24(%2)\n\t"
		"stq $4,32(%2)\n\t"
		"stq $5,40(%2)\n\t"
		"stq $6,48(%2)\n\t"
		"stq $7,56(%2)\n\t"
		"addq %2,64,%2\n\t"
		"bne %0,1b"
		:"=r" (count), "=r" (from), "=r" (to)
		:"0" (PAGE_SIZE/64), "1" (from), "2" (to)
		:"$0","$1","$2","$3","$4","$5","$6","$7");
}

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

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)		(((addr)+PAGE_SIZE-1)&PAGE_MASK)

#define PAGE_OFFSET		0xFFFFFC0000000000UL
#define __pa(x)			((unsigned long) (x) - PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long) (x) + PAGE_OFFSET))
#define MAP_NR(addr)		(__pa(addr) >> PAGE_SHIFT)

#endif /* __KERNEL__ */

#endif /* _ALPHA_PAGE_H */

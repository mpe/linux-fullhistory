#ifndef _I386_PAGE_H
#define _I386_PAGE_H

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

/*
 * TLB invalidation:
 *
 *  - invalidate() invalidates the current task TLBs
 *  - invalidate_all() invalidates all processes TLBs
 *  - invalidate_task(task) invalidates the specified tasks TLB's
 *  - invalidate_page(task, vmaddr) invalidates one page
 *
 * ..but the i386 has somewhat limited invalidation capabilities.
 */
 
#ifndef __SMP__
#define invalidate() \
__asm__ __volatile__("movl %%cr3,%%eax\n\tmovl %%eax,%%cr3": : :"ax")

#define invalidate_all() invalidate()
#define invalidate_task(task) \
do { if ((task)->mm == current->mm) invalidate(); } while (0)
#define invalidate_page(task,addr) \
do { if ((task)->mm == current->mm) invalidate(); } while (0)

#else
#include <asm/smp.h>
#define local_invalidate() \
__asm__ __volatile__("movl %%cr3,%%eax\n\tmovl %%eax,%%cr3": : :"ax")
#define invalidate() \
	smp_invalidate();
#endif

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

/* This handles the memory map.. */
#define PAGE_OFFSET		0
#define MAP_NR(addr)		(((unsigned long)(addr)) >> PAGE_SHIFT)

typedef struct {
	unsigned count:24,
		 age:6,
		 dirty:1,
		 reserved:1;
} mem_map_t;

#endif /* __KERNEL__ */

#endif /* _I386_PAGE_H */

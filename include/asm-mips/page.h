#ifndef __ASM_MIPS_PAGE_H
#define __ASM_MIPS_PAGE_H

#include <linux/linkage.h>

#ifndef __ASSEMBLY__
#define invalidate()	tlbflush();
extern asmlinkage void tlbflush(void);
#endif

			/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT			12
#define PGDIR_SHIFT			22
#define PAGE_SIZE			(1UL << PAGE_SHIFT)
#define PGDIR_SIZE			(1UL << PGDIR_SHIFT)

#ifdef __KERNEL__

			/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR			(8*sizeof(unsigned long))
			/* to mask away the intra-page address bits */
#define PAGE_MASK			(~(PAGE_SIZE-1))
			/* to mask away the intra-page address bits */
#define PGDIR_MASK			(~(PGDIR_SIZE-1))
			/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)		(((addr)+PAGE_SIZE-1)&PAGE_MASK)
			/* to align the pointer to a pointer address */
#define PTR_MASK			(~(sizeof(void*)-1))

					/* sizeof(void*)==1<<SIZEOF_PTR_LOG2 */
					/* 64-bit machines, beware!  SRB. */
#define SIZEOF_PTR_LOG2			2

/* to find an entry in a page-table-directory */
#define PAGE_DIR_OFFSET(tsk,address) \
((((unsigned long)(address)) >> 22) + (unsigned long *) (tsk)->tss.pg_dir)

/* to find an entry in a page-table */
#define PAGE_PTR(address)		\
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

/* the no. of pointers that fit on a page */
#define PTRS_PER_PAGE			(PAGE_SIZE/sizeof(void*))

/* to set the page-dir */
#define SET_PAGE_DIR(tsk,pgdir) \
do { \
	(tsk)->tss.pg_dir = (unsigned long) (pgdir); \
	if ((tsk) == current) \
		invalidate(); \
} while (0)

#endif /* __KERNEL__ */

#endif /* __ASM_MIPS_PAGE_H */

#ifndef _ALPHA_PAGE_H
#define _ALPHA_PAGE_H

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

			/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT			13
#define PGDIR_SHIFT			23
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
#define SIZEOF_PTR_LOG2			4

/* to find an entry in a page-table-directory */
/*
 * XXXXX This isn't right: we shouldn't use the ptbr, but the L2 pointer.
 * This is just for getting it through the compiler right now
 */
#define PAGE_DIR_OFFSET(tsk,address) \
((unsigned long *) ((tsk)->tss.ptbr + ((((unsigned long)(address)) >> 21) & PTR_MASK & ~PAGE_MASK)))

/* to find an entry in a page-table */
#define PAGE_PTR(address)		\
  ((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

/* the no. of pointers that fit on a page */
#define PTRS_PER_PAGE			(PAGE_SIZE/sizeof(void*))

/* to set the page-dir */
/*
 * XXXXX This isn't right: we shouldn't use the ptbr, but the L2 pointer.
 * This is just for getting it through the compiler right now
 */
#define SET_PAGE_DIR(tsk,pgdir) \
do { \
	(tsk)->tss.ptbr = (unsigned long) (pgdir); \
	if ((tsk) == current) \
		invalidate(); \
} while (0)

#endif /* __KERNEL__ */

#endif /* _ALPHA_PAGE_H */

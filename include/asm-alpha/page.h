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

#define PAGE_OFFSET 0xFFFFFC0000000000
#define MAP_NR(addr) (((addr) - PAGE_OFFSET) >> PAGE_SHIFT)
#define MAP_PAGE_RESERVED (1<<31)

typedef unsigned int mem_map_t;

#define PAGE_PRESENT	0x001
#define PAGE_RW		0x002
#define PAGE_USER	0x004
#define PAGE_ACCESSED	0x020
#define PAGE_DIRTY	0x040
#define PAGE_COW	0x200	/* implemented in software (one of the AVL bits) */

#define PAGE_PRIVATE	(PAGE_PRESENT | PAGE_RW | PAGE_USER | PAGE_ACCESSED | PAGE_COW)
#define PAGE_SHARED	(PAGE_PRESENT | PAGE_RW | PAGE_USER | PAGE_ACCESSED)
#define PAGE_COPY	(PAGE_PRESENT | PAGE_USER | PAGE_ACCESSED | PAGE_COW)
#define PAGE_READONLY	(PAGE_PRESENT | PAGE_USER | PAGE_ACCESSED)
#define PAGE_EXECONLY	(PAGE_PRESENT | PAGE_USER | PAGE_ACCESSED)
#define PAGE_TABLE	(PAGE_PRESENT | PAGE_RW | PAGE_USER | PAGE_ACCESSED)

#define PAGE_CHG_MASK (PAGE_MASK | PAGE_ACCESSED | PAGE_DIRTY)

#ifdef __KERNEL__

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long __bad_page(void);
extern unsigned long __bad_pagetable(void);
extern unsigned long __zero_page(void);

#define BAD_PAGETABLE __bad_pagetable()
#define BAD_PAGE __bad_page()
#define ZERO_PAGE __zero_page()

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
#define SIZEOF_PTR_LOG2			3

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

#ifndef _I386_PAGE_H
#define _I386_PAGE_H

#define invalidate() \
__asm__ __volatile__("movl %%cr3,%%eax\n\tmovl %%eax,%%cr3": : :"ax")

			/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT			12
#define PGDIR_SHIFT			22
#define PAGE_SIZE			(1UL << PAGE_SHIFT)
#define PGDIR_SIZE			(1UL << PGDIR_SHIFT)

#define PAGE_OFFSET	0
#define MAP_NR(addr) ((addr) >> PAGE_SHIFT)
#define MAP_PAGE_RESERVED (1<<15)

typedef unsigned short mem_map_t;

#define PAGE_PRESENT	0x001
#define PAGE_RW		0x002
#define PAGE_USER	0x004
#define PAGE_PWT	0x008	/* 486 only - not used currently */
#define PAGE_PCD	0x010	/* 486 only - not used currently */
#define PAGE_ACCESSED	0x020
#define PAGE_DIRTY	0x040
#define PAGE_COW	0x200	/* implemented in software (one of the AVL bits) */

#define PAGE_PRIVATE	(PAGE_PRESENT | PAGE_RW | PAGE_USER | PAGE_ACCESSED | PAGE_COW)
#define PAGE_SHARED	(PAGE_PRESENT | PAGE_RW | PAGE_USER | PAGE_ACCESSED)
#define PAGE_COPY	(PAGE_PRESENT | PAGE_USER | PAGE_ACCESSED | PAGE_COW)
#define PAGE_READONLY	(PAGE_PRESENT | PAGE_USER | PAGE_ACCESSED)
#define PAGE_EXECONLY	(PAGE_PRESENT | PAGE_USER | PAGE_ACCESSED)
#define PAGE_TABLE	(PAGE_PRESENT | PAGE_RW | PAGE_USER | PAGE_ACCESSED)

#define PAGE_CHG_MASK (PAGE_MASK | PAGE_ACCESSED | PAGE_DIRTY | PAGE_PWT | PAGE_PCD)

#ifdef __KERNEL__

/*
 * Define this if things work differently on a i386 and a i486:
 * it will (on a i486) warn about kernel memory accesses that are
 * done without a 'verify_area(VERIFY_WRITE,..)'
 */
#undef CONFIG_TEST_VERIFY_AREA

/* page table for 0-4MB for everybody */
extern unsigned long pg0[1024];

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
#define SIZEOF_PTR_LOG2			2

/* to find an entry in a page-table-directory */
#define PAGE_DIR_OFFSET(tsk,address) \
((((unsigned long)(address)) >> 22) + (unsigned long *) (tsk)->tss.cr3)

/* to find an entry in a page-table */
#define PAGE_PTR(address)		\
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

/* the no. of pointers that fit on a page */
#define PTRS_PER_PAGE			(PAGE_SIZE/sizeof(void*))

/* to set the page-dir */
#define SET_PAGE_DIR(tsk,pgdir) \
do { \
	(tsk)->tss.cr3 = (unsigned long) (pgdir); \
	if ((tsk) == current) \
		__asm__ __volatile__("movl %0,%%cr3": :"a" ((tsk)->tss.cr3)); \
} while (0)

#endif /* __KERNEL__ */

#endif /* _I386_PAGE_H */

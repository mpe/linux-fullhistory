#ifndef __ASM_MIPS_PAGE_H
#define __ASM_MIPS_PAGE_H

#ifndef __ASSEMBLY__

#include <linux/linkage.h>

#define invalidate()	tlbflush();
extern asmlinkage void tlbflush(void);
#endif

			/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT			12
#define PGDIR_SHIFT			22
#define PAGE_SIZE			(1UL << PAGE_SHIFT)
#define PGDIR_SIZE			(1UL << PGDIR_SHIFT)

#define PAGE_OFFSET	0
#define MAP_NR(addr) ((addr) >> PAGE_SHIFT)
#define MAP_PAGE_RESERVED (1<<15)

typedef unsigned short mem_map_t;

/*
 * Note that we shift the lower 32bits of each EntryLo[01] entry
 * 6 bits to the left. That way we can convert the PFN into the
 * physical address by a single 'and' operation and gain 6 aditional
 * bits for storing information which isn't present in a normal
 * MIPS page table.
 * I've also changed the naming of some bits so that they conform
 * the i386 naming as much as possible.
 * PAGE_USER isn't implemented in software yet.
 */
#define PAGE_PRESENT               (1<<0)   /* implemented in software */
#define PAGE_COW                   (1<<1)   /* implemented in software */
#define PAGE_DIRTY                 (1<<2)   /* implemented in software */
#define PAGE_USER                  (1<<3)   /* implemented in software */
#define PAGE_UNUSED1               (1<<4)   /* implemented in software */
#define PAGE_UNUSED2               (1<<5)   /* implemented in software */
#define PAGE_GLOBAL                (1<<6)
#define PAGE_ACCESSED              (1<<7)   /* The MIPS valid bit      */
#define PAGE_RW                    (1<<8)   /* The MIPS dirty bit      */
#define CACHE_CACHABLE_NO_WA       (0<<9)
#define CACHE_CACHABLE_WA          (1<<9)
#define CACHE_UNCACHED             (2<<9)
#define CACHE_CACHABLE_NONCOHERENT (3<<9)
#define CACHE_CACHABLE_CE          (4<<9)
#define CACHE_CACHABLE_COW         (5<<9)
#define CACHE_CACHABLE_CUW         (6<<9)
#define CACHE_MASK                 (7<<9)

#define PAGE_PRIVATE    (PAGE_PRESENT | PAGE_ACCESSED | PAGE_DIRTY | PAGE_RW | \
                         PAGE_COW | CACHE_CACHABLE_NO_WA)
#define PAGE_SHARED     (PAGE_PRESENT | PAGE_ACCESSED | PAGE_DIRTY | PAGE_RW | \
                         CACHE_CACHABLE_NO_WA)
#define PAGE_COPY       (PAGE_PRESENT | PAGE_ACCESSED | PAGE_COW | \
                         CACHE_CACHABLE_NO_WA)
#define PAGE_READONLY   (PAGE_PRESENT | PAGE_ACCESSED | CACHE_CACHABLE_NO_WA)
#define PAGE_TABLE      (PAGE_PRESENT | PAGE_ACCESSED | PAGE_DIRTY | PAGE_RW | \
                         CACHE_CACHABLE_NO_WA)

#define PAGE_CHG_MASK (PAGE_MASK | PAGE_ACCESSED | PAGE_DIRTY | CACHE_MASK)

#ifdef __KERNEL__

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

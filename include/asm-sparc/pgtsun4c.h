/* pgtsun4c.h:  Sun4c specific pgtable.h defines and code.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_PGTSUN4C_H
#define _SPARC_PGTSUN4C_H

#define SUN4C_PAGE_TABLE_SIZE 0x100   /* 64 entries, 4 bytes a piece */

/* NOTE:  Now we put the free page pool and the page structures
 *        up in high memory above the kernel image which itself
 *        starts at KERNBASE.  Also note PAGE_OFFSET in page.h
 *        This is just like what Linus does on the ALPHA.
 */

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define SUN4C_PMD_SHIFT       22
#define SUN4C_PMD_SIZE        (1UL << SUN4C_PMD_SHIFT)
#define SUN4C_PMD_MASK        (~(SUN4C_PMD_SIZE-1))
#define SUN4C_PMD_ALIGN(addr) (((addr)+SUN4C_PMD_SIZE-1)&SUN4C_PMD_MASK)

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define SUN4C_PGDIR_SHIFT       22
#define SUN4C_PGDIR_SIZE        (1UL << SUN4C_PGDIR_SHIFT)
#define SUN4C_PGDIR_MASK        (~(SUN4C_PGDIR_SIZE-1))
#define SUN4C_PGDIR_ALIGN(addr) (((addr)+SUN4C_PGDIR_SIZE-1)&SUN4C_PGDIR_MASK)

/* To make sun4c_paging_init() happy, I provide the following macros. */
#define SUN4C_REAL_PGDIR_SHIFT  18
#define SUN4C_REAL_PGDIR_SIZE        (1UL << SUN4C_REAL_PGDIR_SHIFT)
#define SUN4C_REAL_PGDIR_MASK        (~(SUN4C_REAL_PGDIR_SIZE-1))
#define SUN4C_REAL_PGDIR_ALIGN(addr) (((addr)+SUN4C_REAL_PGDIR_SIZE-1)&SUN4C_REAL_PGDIR_MASK)

/*
 * To be efficient, and not have to worry about allocating such
 * a huge pgd, we make the kernel sun4c tables each hold 1024
 * entries and the pgd similarly.
 */

#define SUN4C_PTRS_PER_PTE    1024
#define SUN4C_PTRS_PER_PMD    1
#define SUN4C_PTRS_PER_PGD    1024

/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define SUN4C_VMALLOC_OFFSET  (8*1024*1024)
#define SUN4C_VMALLOC_START ((high_memory + SUN4C_VMALLOC_OFFSET) & ~(SUN4C_VMALLOC_OFFSET-1))

/*
 * Sparc SUN4C page table fields. (for now, basically the same as the i386)
 */

#define _SUN4C_PAGE_VALID     0x80000000   /* valid page */
#define _SUN4C_PAGE_WRITE     0x40000000   /* can be written to */
#define _SUN4C_PAGE_USER      0x00000000   /* User page */
#define _SUN4C_PAGE_NOCACHE   0x10000000   /* non-cacheable page */
#define _SUN4C_PAGE_PRIV      0x20000000   /* bit to signify privileged page */
#define _SUN4C_PAGE_REF       0x02000000   /* Page has been accessed/referenced */
#define _SUN4C_PAGE_DIRTY     0x01000000   /* Page has been modified, is dirty */
#define _SUN4C_PAGE_COW       0x00800000   /* COW page */

/* Sparc sun4c mmu has only a writable bit. Thus if a page is valid it can be
 * read in a load, and executed as code automatically. Although, the memory 
 * fault hardware does make a distinction between data-read faults and 
 * insn-read faults which is determined by which trap happened plus magic
 * sync/async fault register values which must be checked in the actual
 * fault handler.
 */

#define _SUN4C_PFN_MASK       0x0000ffff    /* just the page frame number */
#define _SUN4C_MMU_MASK       0xffff0000    /* just the non-page pte bits */

/* The following are for pgd/pmd's */
#define _SUN4C_PGD_PFN_MASK   0x00ffffff    /* bits to hold page tables address */
#define _SUN4C_PGD_MMU_MASK   0xff000000    /* pgd/pfn protection bits          */
#define _SUN4C_PGD_PAGE_SHIFT 8             /* bits to shift to obtain address  */

/* We want the swapper not to swap out page tables, thus dirty and writable
 * so that the kernel can change the entries as needed. Also valid for
 * obvious reasons.
 */
#define _SUN4C_PAGE_TABLE     (_SUN4C_PAGE_VALID | _SUN4C_PAGE_WRITE | _SUN4C_PAGE_REF | _SUN4C_PAGE_DIRTY | _SUN4C_PAGE_PRIV | _SUN4C_PAGE_NOCACHE) /* No cache for now */
#define _SUN4C_PAGE_CHG_MASK  (_SUN4C_PAGE_REF | _SUN4C_PAGE_DIRTY | _SUN4C_PFN_MASK)
#define _SUN4C_PGD_CHG_MASK   (_SUN4C_PAGE_REF | _SUN4C_PAGE_DIRTY | _SUN4C_PGD_PFN_MASK)

#define SUN4C_PAGE_NONE       __pgprot(_SUN4C_PAGE_VALID | _SUN4C_PAGE_REF)
#define SUN4C_PAGE_SHARED     __pgprot(_SUN4C_PAGE_VALID | _SUN4C_PAGE_WRITE | _SUN4C_PAGE_REF)
#define SUN4C_PAGE_COPY       __pgprot(_SUN4C_PAGE_VALID | _SUN4C_PAGE_REF | _SUN4C_PAGE_COW)
#define SUN4C_PAGE_READONLY   __pgprot(_SUN4C_PAGE_VALID | _SUN4C_PAGE_REF)
#define SUN4C_PAGE_KERNEL     __pgprot(_SUN4C_PAGE_VALID | _SUN4C_PAGE_WRITE | _SUN4C_PAGE_PRIV | _SUN4C_PAGE_REF | _SUN4C_PAGE_DIRTY | _SUN4C_PAGE_NOCACHE)
#define SUN4C_PAGE_INVALID    __pgprot(0)

#define _SUN4C_PAGE_NORMAL(x) __pgprot(_SUN4C_PAGE_VALID | _SUN4C_PAGE_REF | (x))

/* The Sun4c mmu physical segment map allocation data structure.
 * For each physical segmap available on the mmu we have one entry,
 * 127 on the sun4c (except SparcStation 2's which seem to have 255)
 * and 512 on the sun4.  Each segmap can be in various stages of
 * allocation.
 */
#define PSEG_ENTRIES  513     /* We allocate 513 entries for simplicity */
extern unsigned int phys_seg_map[PSEG_ENTRIES];
extern unsigned int phys_seg_life[PSEG_ENTRIES];

/* for phys_seg_map entries */
#define PSEG_AVL      0x0     /* Physical segment is available/free */
#define PSEG_USED     0x1     /* A segmap currently being used */
#define PSEG_RSV      0x2     /* This segmap is reserved (used for proms addr space) */
#define PSEG_KERNEL   0x3     /* This is a kernel hard segment, cannot deallocate */

/* for phys_seg_life entries */
/* The idea is, every call to update_mmu_cache we increment all the life
 * counters.  When we re-allocate or allocate a physical segment for the
 * first time we set the phys_seg_life entry to PSEG_BORN.  Also, when we
 * fill a pte for a segment already loaded we *decrease* the life count
 * by two for that segment.  We'll see how this works.
 */
#define PSEG_BORN     0x00     /* Just allocated */

#endif /* !(_SPARC_PGTSUN4C_H) */

#ifndef _SPARC_PGTABLE_H
#define _SPARC_PGTABLE_H

/*  asm-sparc/pgtable.h:  Defines and functions used to work
 *                        with Sparc page tables.
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/mm.h>
#include <asm/asi.h>
#include <asm/pgtsun4c.h>
#include <asm/pgtsrmmu.h>

extern void load_mmu(void);

extern unsigned int pmd_shift;
extern unsigned int pmd_size;
extern unsigned int pmd_mask;
extern unsigned int (*pmd_align)(unsigned int);

extern unsigned int pgdir_shift;
extern unsigned int pgdir_size;
extern unsigned int pgdir_mask;
extern unsigned int (*pgdir_align)(unsigned int);

extern unsigned int ptrs_per_pte;
extern unsigned int ptrs_per_pmd;
extern unsigned int ptrs_per_pgd;

extern unsigned int ptrs_per_page;

extern unsigned long (*(vmalloc_start))(void);

#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_START vmalloc_start()

extern pgprot_t page_none;
extern pgprot_t page_shared;
extern pgprot_t page_copy;
extern pgprot_t page_readonly;
extern pgprot_t page_kernel;
extern pgprot_t page_invalid;

#define PMD_SHIFT      (pmd_shift)
#define PMD_SIZE       (pmd_size)
#define PMD_MASK       (pmd_mask)
#define PMD_ALIGN      (pmd_align)
#define PGDIR_SHIFT    (pgdir_shift)
#define PGDIR_SIZE     (pgdir_size)
#define PGDIR_MASK     (pgdir_mask)
#define PGDIR_ALIGN    (pgdir_align)
#define PTRS_PER_PTE   (ptrs_per_pte)
#define PTRS_PER_PMD   (ptrs_per_pmd)
#define PTRS_PER_PGD   (ptrs_per_pgd)

#define PAGE_NONE      (page_none)
#define PAGE_SHARED    (page_shared)
#define PAGE_COPY      (page_copy)
#define PAGE_READONLY  (page_readonly)
#define PAGE_KERNEL    (page_kernel)
#define PAGE_INVALID   (page_invalid)

/* Top-level page directory */
extern pgd_t swapper_pg_dir[1024];

/* Page table for 0-4MB for everybody, on the Sparc this
 * holds the same as on the i386.
 */
extern unsigned long pg0[1024];

extern unsigned long ptr_in_current_pgd;

/* the no. of pointers that fit on a page: this will go away */
#define PTRS_PER_PAGE   (PAGE_SIZE/sizeof(void*))

/* I define these like the i386 does because the check for text or data fault
 * is done at trap time by the low level handler. Maybe I can set these bits
 * then once determined. I leave them like this for now though.
 */
#define __P000  PAGE_NONE
#define __P001  PAGE_READONLY
#define __P010  PAGE_COPY
#define __P011  PAGE_COPY
#define __P100  PAGE_READONLY
#define __P101  PAGE_READONLY
#define __P110  PAGE_COPY
#define __P111  PAGE_COPY

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY
#define __S101	PAGE_READONLY
#define __S110	PAGE_SHARED
#define __S111	PAGE_SHARED

/* Contexts on the Sparc. */
#define MAX_CTXS 256
#define NO_CTX   0xffff     /* In tss.context means task has no context currently */
extern struct task_struct * ctx_tasks[MAX_CTXS];
extern int ctx_tasks_last_frd;

extern int num_contexts;

/* This routine allocates a new context.  And 'p' must not be 'current'! */
extern inline int alloc_mmu_ctx(struct task_struct *p)
{
	int i;

	for(i=0; i<num_contexts; i++)
		if(ctx_tasks[i] == NULL) break;

	if(i<num_contexts) {
		p->tss.context = i;
		ctx_tasks[i] = p;
		return i;
	}

	/* Have to free one up */
	ctx_tasks_last_frd++;
	if(ctx_tasks_last_frd >= num_contexts) ctx_tasks_last_frd=0;
	/* Right here is where we invalidate the user mappings that were
	 * present.  TODO
	 */
        ctx_tasks[ctx_tasks_last_frd]->tss.context = NO_CTX;
	ctx_tasks[ctx_tasks_last_frd] = p;
	p->tss.context = ctx_tasks_last_frd;
	return ctx_tasks_last_frd;
}

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern pte_t * __bad_pagetable(void);

extern unsigned long __zero_page(void);


#define BAD_PAGETABLE __bad_pagetable()
#define BAD_PAGE __bad_page()
#define ZERO_PAGE __zero_page()

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR      (8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK          (~(sizeof(void*)-1))


#define SIZEOF_PTR_LOG2   2

extern unsigned long (*pte_page)(pte_t);
extern unsigned long (*pmd_page)(pmd_t);
extern unsigned long (*pgd_page)(pgd_t);

/* to set the page-dir
 *
 * On the Sparc the page segments hold 64 pte's which means 256k/segment.
 * Therefore there is no global idea of 'the' page directory, although we
 * make a virtual one in kernel memory so that we can keep the stats on
 * all the pages since not all can be loaded at once in the mmu.
 *
 * Actually on the SRMMU things do work exactly like the i386, the
 * page tables live in real physical ram, no funky TLB buisness.  But
 * we have to do lots of flushing. And we have to update the root level
 * page table pointer for this process if it has a context.
 */

extern void (*sparc_update_rootmmu_dir)(struct task_struct *, pgd_t *pgdir);

#define SET_PAGE_DIR(tsk,pgdir) \
do { sparc_update_rootmmu_dir(tsk, pgdir); } while (0)
       
/* to find an entry in a page-table */
#define PAGE_PTR(address) \
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

extern unsigned long high_memory;

extern int (*pte_none)(pte_t);
extern int (*pte_present)(pte_t);
extern int (*pte_inuse)(pte_t *);
extern void (*pte_clear)(pte_t *);
extern void (*pte_reuse)(pte_t *);

extern int (*pmd_none)(pmd_t);
extern int (*pmd_bad)(pmd_t);
extern int (*pmd_present)(pmd_t);
extern int (*pmd_inuse)(pmd_t *);
extern void (*pmd_clear)(pmd_t *);
extern void (*pmd_reuse)(pmd_t *);

extern int (*pgd_none)(pgd_t);
extern int (*pgd_bad)(pgd_t);
extern int (*pgd_present)(pgd_t);
extern int (*pgd_inuse)(pgd_t *);
extern void (*pgd_clear)(pgd_t *);
extern void (*pgd_reuse)(pgd_t *);

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern int (*pte_read)(pte_t);
extern int (*pte_write)(pte_t);
extern int (*pte_exec)(pte_t);
extern int (*pte_dirty)(pte_t);
extern int (*pte_young)(pte_t);
extern int (*pte_cow)(pte_t);

extern pte_t (*pte_wrprotect)(pte_t);
extern pte_t (*pte_rdprotect)(pte_t);
extern pte_t (*pte_exprotect)(pte_t);
extern pte_t (*pte_mkclean)(pte_t);
extern pte_t (*pte_mkold)(pte_t);
extern pte_t (*pte_uncow)(pte_t);
extern pte_t (*pte_mkwrite)(pte_t);
extern pte_t (*pte_mkread)(pte_t);
extern pte_t (*pte_mkexec)(pte_t);
extern pte_t (*pte_mkdirty)(pte_t);
extern pte_t (*pte_mkyoung)(pte_t);
extern pte_t (*pte_mkcow)(pte_t);

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern pte_t (*mk_pte)(unsigned long, pgprot_t);

extern void (*pgd_set)(pgd_t *, pte_t *);

extern pte_t (*pte_modify)(pte_t, pgprot_t);

/* to find an entry in a page-table-directory */
extern pgd_t * (*pgd_offset)(struct task_struct *, unsigned long);

/* Find an entry in the second-level page table.. */
extern pmd_t * (*pmd_offset)(pgd_t *, unsigned long);

/* Find an entry in the third-level page table.. */ 
extern pte_t * (*pte_offset)(pmd_t *, unsigned long);

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
extern void (*pte_free_kernel)(pte_t *);

extern pte_t * (*pte_alloc_kernel)(pmd_t *, unsigned long);

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
extern void (*pmd_free_kernel)(pmd_t *);

extern pmd_t * (*pmd_alloc_kernel)(pgd_t *, unsigned long);

extern void (*pte_free)(pte_t *);

extern pte_t * (*pte_alloc)(pmd_t *, unsigned long);

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
extern void (*pmd_free)(pmd_t *);

extern pmd_t * (*pmd_alloc)(pgd_t *, unsigned long);

extern void (*pgd_free)(pgd_t *);

/* A page directory on the sun4c needs 16k, thus we request an order of
 * two.
 *
 * I need 16k for a sun4c page table, so I use kmalloc since kmalloc_init()
 * is called before pgd_alloc ever is (I think).
 */

extern pgd_t * (*pgd_alloc)(void);

extern int invalid_segment;

/* Sun4c specific routines.  They can stay inlined. */
extern inline int alloc_sun4c_pseg(void)
{
	int oldseg, i;
	/* First see if any are free already */
	for(i=0; i<PSEG_ENTRIES; i++)
		if(phys_seg_map[i]==PSEG_AVL) return i;

	/* Uh-oh, gotta unallocate a TLB pseg */
	oldseg=0;
	for(i=0; i<PSEG_ENTRIES; i++) {
		/* Can not touch PSEG_KERNEL and PSEG_RSV segmaps */
		if(phys_seg_map[i]!=PSEG_USED) continue;
		/* Ok, take a look at it's lifespan */
		oldseg = (phys_seg_life[i]>oldseg) ? phys_seg_life[i] : oldseg;
	}
	phys_seg_life[oldseg]=PSEG_BORN;
	return oldseg;
}

/* Age all psegs except pseg_skip */
extern inline void age_sun4c_psegs(int pseg_skip)
{
	int i;

	for(i=0; i<pseg_skip; i++) phys_seg_life[i]++;
	i++;
	while(i<PSEG_ENTRIES) phys_seg_life[i++]++;
	return;
}

/*
 * This is only ever called when the sun4c page fault routines run
 * so we can keep this here as the srmmu code will never get to it.
 */
extern inline void update_mmu_cache(struct vm_area_struct * vma,
	unsigned long address, pte_t pte)
{
  unsigned long clr_addr;
  int segmap;

  segmap = (int) get_segmap(address & SUN4C_REAL_PGDIR_MASK);
  if(segmap == invalid_segment) {
    segmap = alloc_sun4c_pseg();
    put_segmap((address & SUN4C_REAL_PGDIR_MASK), segmap);
    phys_seg_map[segmap] = PSEG_USED;

    /* We got a segmap, clear all the pte's in it. */
    for(clr_addr=(address&SUN4C_REAL_PGDIR_MASK); clr_addr<((address&SUN4C_REAL_PGDIR_MASK) + SUN4C_REAL_PGDIR_SIZE); 
	clr_addr+=PAGE_SIZE)
	    put_pte(clr_addr, 0);
  }

  /* Do aging */
  age_sun4c_psegs(segmap);
  put_pte((address & PAGE_MASK), pte_val(pte));
  return;

}

#endif /* !(_SPARC_PGTABLE_H) */

/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Copyright 2003 PathScale, Inc.
 * Derived from include/asm-i386/pgtable.h
 * Licensed under the GPL
 */

#ifndef __UM_PGTABLE_H
#define __UM_PGTABLE_H

#include "linux/sched.h"
#include "linux/linkage.h"
#include "asm/processor.h"
#include "asm/page.h"
#include "asm/fixmap.h"

#define _PAGE_PRESENT	0x001
#define _PAGE_NEWPAGE	0x002
#define _PAGE_NEWPROT   0x004
#define _PAGE_FILE	0x008   /* set:pagecache unset:swap */
#define _PAGE_PROTNONE	0x010	/* If not present */
#define _PAGE_RW	0x020
#define _PAGE_USER	0x040
#define _PAGE_ACCESSED	0x080
#define _PAGE_DIRTY	0x100

#ifdef CONFIG_3_LEVEL_PGTABLES
#include "asm/pgtable-3level.h"
#else
#include "asm/pgtable-2level.h"
#endif

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

extern void *um_virt_to_phys(struct task_struct *task, unsigned long virt,
			     pte_t *pte_out);

/* zero page used for uninitialized stuff */
extern unsigned long *empty_zero_page;

#define pgtable_cache_init() do ; while (0)

/*
 * pgd entries used up by user/kernel:
 */

#define USER_PGD_PTRS (TASK_SIZE >> PGDIR_SHIFT)
#define KERNEL_PGD_PTRS (PTRS_PER_PGD-USER_PGD_PTRS)

#ifndef __ASSEMBLY__
/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */

extern unsigned long end_iomem;

#define VMALLOC_OFFSET	(__va_space)
#define VMALLOC_START ((end_iomem + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))

#ifdef CONFIG_HIGHMEM
# define VMALLOC_END	(PKMAP_BASE-2*PAGE_SIZE)
#else
# define VMALLOC_END	(FIXADDR_START-2*PAGE_SIZE)
#endif

#define REGION_SHIFT	(sizeof(pte_t) * 8 - 4)
#define REGION_MASK	(((unsigned long) 0xf) << REGION_SHIFT)

#define _PAGE_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _KERNPG_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

#define PAGE_NONE	__pgprot(_PAGE_PROTNONE | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)
#define PAGE_KERNEL_RO	__pgprot(_PAGE_PRESENT | _PAGE_DIRTY | _PAGE_ACCESSED)

/*
 * The i386 can't do page protection for execute, and considers that the same are read.
 * Also, write permissions imply read permissions. This is the closest we can get..
 */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY
#define __P101	PAGE_READONLY
#define __P110	PAGE_COPY
#define __P111	PAGE_COPY

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY
#define __S101	PAGE_READONLY
#define __S110	PAGE_SHARED
#define __S111	PAGE_SHARED

/*
 * Define this if things work differently on an i386 and an i486:
 * it will (on an i486) warn about kernel memory accesses that are
 * done without a 'verify_area(VERIFY_WRITE,..)'
 */
#undef TEST_VERIFY_AREA

/* page table for 0-4MB for everybody */
extern unsigned long pg0[1024];

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern pte_t * __bad_pagetable(void);

#define BAD_PAGETABLE __bad_pagetable()
#define BAD_PAGE __bad_page()

#define ZERO_PAGE(vaddr) virt_to_page(empty_zero_page)

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR			(8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK			(~(sizeof(void*)-1))

/* sizeof(void*)==1<<SIZEOF_PTR_LOG2 */
/* 64-bit machines, beware!  SRB. */
#define SIZEOF_PTR_LOG2			3

/* to find an entry in a page-table */
#define PAGE_PTR(address) \
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

#define pte_clear(xp) pte_set_val(*(xp), (phys_t) 0, __pgprot(_PAGE_NEWPAGE))

#define pmd_none(x)	(!(pmd_val(x) & ~_PAGE_NEWPAGE))
#define	pmd_bad(x)	((pmd_val(x) & (~PAGE_MASK & ~_PAGE_USER)) != _KERNPG_TABLE)
#define pmd_present(x)	(pmd_val(x) & _PAGE_PRESENT)
#define pmd_clear(xp)	do { pmd_val(*(xp)) = _PAGE_NEWPAGE; } while (0)

#define pmd_newpage(x)  (pmd_val(x) & _PAGE_NEWPAGE)
#define pmd_mkuptodate(x) (pmd_val(x) &= ~_PAGE_NEWPAGE)

#define pud_newpage(x)  (pud_val(x) & _PAGE_NEWPAGE)
#define pud_mkuptodate(x) (pud_val(x) &= ~_PAGE_NEWPAGE)

#define pages_to_mb(x) ((x) >> (20-PAGE_SHIFT))

#define pmd_page(pmd) phys_to_page(pmd_val(pmd) & PAGE_MASK)

#define pte_address(x) (__va(pte_val(x) & PAGE_MASK))
#define mk_phys(a, r) ((a) + (((unsigned long) r) << REGION_SHIFT))
#define phys_addr(p) ((p) & ~REGION_MASK)

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_user(pte_t pte)
{
	return((pte_get_bits(pte, _PAGE_USER)) &&
	       !(pte_get_bits(pte, _PAGE_PROTNONE)));
}

static inline int pte_read(pte_t pte)
{ 
	return((pte_get_bits(pte, _PAGE_USER)) &&
	       !(pte_get_bits(pte, _PAGE_PROTNONE)));
}

static inline int pte_exec(pte_t pte){
	return((pte_get_bits(pte, _PAGE_USER)) &&
	       !(pte_get_bits(pte, _PAGE_PROTNONE)));
}

static inline int pte_write(pte_t pte)
{
	return((pte_get_bits(pte, _PAGE_RW)) &&
	       !(pte_get_bits(pte, _PAGE_PROTNONE)));
}

/*
 * The following only works if pte_present() is not true.
 */
static inline int pte_file(pte_t pte)
{
	return pte_get_bits(pte, _PAGE_FILE);
}

static inline int pte_dirty(pte_t pte)
{
	return pte_get_bits(pte, _PAGE_DIRTY);
}

static inline int pte_young(pte_t pte)
{
	return pte_get_bits(pte, _PAGE_ACCESSED);
}

static inline int pte_newpage(pte_t pte)
{
	return pte_get_bits(pte, _PAGE_NEWPAGE);
}

static inline int pte_newprot(pte_t pte)
{ 
	return(pte_present(pte) && (pte_get_bits(pte, _PAGE_NEWPROT)));
}

static inline pte_t pte_rdprotect(pte_t pte)
{ 
	pte_clear_bits(pte, _PAGE_USER);
	return(pte_mknewprot(pte));
}

static inline pte_t pte_exprotect(pte_t pte)
{ 
	pte_clear_bits(pte, _PAGE_USER);
	return(pte_mknewprot(pte));
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_clear_bits(pte, _PAGE_DIRTY);
	return(pte);
}

static inline pte_t pte_mkold(pte_t pte)	
{ 
	pte_clear_bits(pte, _PAGE_ACCESSED);
	return(pte);
}

static inline pte_t pte_wrprotect(pte_t pte)
{ 
	pte_clear_bits(pte, _PAGE_RW);
	return(pte_mknewprot(pte)); 
}

static inline pte_t pte_mkread(pte_t pte)
{ 
	pte_set_bits(pte, _PAGE_RW);
	return(pte_mknewprot(pte)); 
}

static inline pte_t pte_mkexec(pte_t pte)
{ 
	pte_set_bits(pte, _PAGE_USER);
	return(pte_mknewprot(pte)); 
}

static inline pte_t pte_mkdirty(pte_t pte)
{ 
	pte_set_bits(pte, _PAGE_DIRTY);
	return(pte);
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_set_bits(pte, _PAGE_ACCESSED);
	return(pte);
}

static inline pte_t pte_mkwrite(pte_t pte)	
{
	pte_set_bits(pte, _PAGE_RW);
	return(pte_mknewprot(pte)); 
}

static inline pte_t pte_mkuptodate(pte_t pte)	
{
	pte_clear_bits(pte, _PAGE_NEWPAGE);
	if(pte_present(pte))
		pte_clear_bits(pte, _PAGE_NEWPROT);
	return(pte); 
}

extern phys_t page_to_phys(struct page *page);

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

extern pte_t mk_pte(struct page *page, pgprot_t pgprot);

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_set_val(pte, (pte_val(pte) & _PAGE_CHG_MASK), newprot);
	if(pte_present(pte)) pte = pte_mknewpage(pte_mknewprot(pte));
	return pte; 
}

#define pmd_page_kernel(pmd) ((unsigned long) __va(pmd_val(pmd) & PAGE_MASK))

/*
 * the pgd page can be thought of an array like this: pgd_t[PTRS_PER_PGD]
 *
 * this macro returns the index of the entry in the pgd page which would
 * control the given virtual address
 */
#define pgd_index(address) (((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD-1))

#define pgd_index_k(addr) pgd_index(addr)

/*
 * pgd_offset() returns a (pgd_t *)
 * pgd_index() is used get the offset into the pgd page's array of pgd_t's;
 */
#define pgd_offset(mm, address) ((mm)->pgd+pgd_index(address))

/*
 * a shortcut which implies the use of the kernel's pgd, instead
 * of a process's
 */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/*
 * the pmd page can be thought of an array like this: pmd_t[PTRS_PER_PMD]
 *
 * this macro returns the index of the entry in the pmd page which would
 * control the given virtual address
 */
#define pmd_index(address) (((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))

/*
 * the pte page can be thought of an array like this: pte_t[PTRS_PER_PTE]
 *
 * this macro returns the index of the entry in the pte page which would
 * control the given virtual address
 */
#define pte_index(address) (((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset_kernel(dir, address) \
	((pte_t *) pmd_page_kernel(*(dir)) +  pte_index(address))
#define pte_offset_map(dir, address) \
	((pte_t *)page_address(pmd_page(*(dir))) + pte_index(address))
#define pte_offset_map_nested(dir, address) pte_offset_map(dir, address)
#define pte_unmap(pte) do { } while (0)
#define pte_unmap_nested(pte) do { } while (0)

#define update_mmu_cache(vma,address,pte) do ; while (0)

/* Encode and de-code a swap entry */
#define __swp_type(x)			(((x).val >> 4) & 0x3f)
#define __swp_offset(x)			((x).val >> 11)

#define __swp_entry(type, offset) \
	((swp_entry_t) { ((type) << 4) | ((offset) << 11) })
#define __pte_to_swp_entry(pte) \
	((swp_entry_t) { pte_val(pte_mkuptodate(pte)) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

#define kern_addr_valid(addr) (1)

#include <asm-generic/pgtable.h>

#include <asm-generic/pgtable-nopud.h>

#endif
#endif

extern struct page *phys_to_page(const unsigned long phys);
extern struct page *__virt_to_page(const unsigned long virt);
#define virt_to_page(addr) __virt_to_page((const unsigned long) addr)

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */

/* * Last edited: Nov  7 23:44 1995 (cort) */
#ifndef _PPC_PGTABLE_H
#define _PPC_PGTABLE_H

#include <asm/page.h>
#include <asm/mmu.h>

/*
 * Memory management on the PowerPC is a software emulation of the i386
 * MMU folded onto the PowerPC hardware MMU.  The emulated version looks
 * and behaves like the two-level i386 MMU.  Entries from these tables
 * are merged into the PowerPC hashed MMU tables, on demand, treating the
 * hashed tables like a special cache.
 *
 * Since the PowerPC does not have separate kernel and user address spaces,
 * the user virtual address space must be a [proper] subset of the kernel
 * space.  Thus, all tasks will have a specific virtual mapping for the
 * user virtual space and a common mapping for the kernel space.  The
 * simplest way to split this was literally in half.  Also, life is so
 * much simpler for the kernel if the machine hardware resources are
 * always mapped in.  Thus, some additional space is given up to the
 * kernel space to accommodate this.
 *
 * CAUTION! Some of the trade-offs make sense for the PreP platform on
 * which this code was originally developed.  When it migrates to other
 * PowerPC environments, some of the assumptions may fail and the whole
 * setup may need to be reevaluated.
 *
 * On the PowerPC, page translations are kept in a hashed table.  There
 * is exactly one of these tables [although the architecture supports
 * an arbitrary number].  Page table entries move in/out of this hashed
 * structure on demand, with the kernel filling in entries as they are
 * needed.  Just where a page table entry hits in the hashed table is a
 * function of the hashing which is in turn based on the upper 4 bits
 * of the logical address.  These 4 bits address a "virtual segment id"
 * which is unique per task/page combination for user addresses and
 * fixed for the kernel addresses.  Thus, the kernel space can be simply
 * shared [indeed at low overhead] among all tasks.
 *
 * The basic virtual address space is thus:
 *
 * 0x0XXXXXX  --+
 * 0x1XXXXXX    |
 * 0x2XXXXXX    |  User address space. 
 * 0x3XXXXXX    |
 * 0x4XXXXXX    |
 * 0x5XXXXXX    |
 * 0x6XXXXXX    |
 * 0x7XXXXXX  --+
 * 0x8XXXXXX       PCI/ISA I/O space
 * 0x9XXXXXX  --+
 * 0xAXXXXXX    |  Kernel virtual memory
 * 0xBXXXXXX  --+
 * 0xCXXXXXX       PCI/ISA Memory space
 * 0xDXXXXXX
 * 0xEXXXXXX
 * 0xFXXXXXX       Board I/O space
 *
 * CAUTION!  One of the real problems here is keeping the software
 * managed tables coherent with the hardware hashed tables.  When
 * the software decides to update the table, it's normally easy to
 * update the hardware table.  But when the hardware tables need
 * changed, e.g. as the result of a page fault, it's more difficult
 * to reflect those changes back into the software entries.  Currently,
 * this process is quite crude, with updates causing the entire set
 * of tables to become invalidated.  Some performance could certainly
 * be regained by improving this.
 *
 * The Linux memory management assumes a three-level page table setup. On
 * the i386, we use that, but "fold" the mid level into the top-level page
 * table, so that we physically have the same two-level page table as the
 * i386 mmu expects.
 *
 * This file contains the functions and defines necessary to modify and use
 * the i386 page table tree.
 */

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT	22
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	22
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * entries per page directory level: the i386 is two-level, so
 * we don't really have any PMD directory physically.
 */
#define PTRS_PER_PTE	1024
#define PTRS_PER_PMD	1
#define PTRS_PER_PGD	1024

/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_OFFSET	(8*1024*1024)
#define VMALLOC_START ((high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
#define VMALLOC_VMADDR(x) ((unsigned long)(x))

#define _PAGE_PRESENT	0x001
#define _PAGE_RW	0x002
#define _PAGE_USER	0x004
#define _PAGE_PCD	0x010
#define _PAGE_ACCESSED	0x020
#define _PAGE_DIRTY	0x040
#define _PAGE_COW	0x200	/* implemented in software (one of the AVL bits) */
#define _PAGE_NO_CACHE	0x400

#define _PAGE_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | _PAGE_COW)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)
#define PAGE_KERNEL_NO_CACHE	__pgprot(_PAGE_NO_CACHE | _PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)

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
 * TLB invalidation:
 *
 *  - invalidate() invalidates the current mm struct TLBs
 *  - invalidate_all() invalidates all processes TLBs
 *  - invalidate_mm(mm) invalidates the specified mm context TLB's
 *  - invalidate_page(mm, vmaddr) invalidates one page
 *  - invalidate_range(mm, start, end) invalidates a range of pages
 *
 * FIXME: This could be done much better!
 */

#define invalidate_all() printk("invalidate_all()\n");invalidate()
#if 0
#define invalidate_mm(mm_struct) \
do { if ((mm_struct) == current->mm) invalidate(); else printk("Can't invalidate_mm(%x)\n", mm_struct);} while (0)
#define invalidate_page(mm_struct,addr) \
do { if ((mm_struct) == current->mm) invalidate(); else printk("Can't invalidate_page(%x,%x)\n", mm_struct, addr);} while (0)
#define invalidate_range(mm_struct,start,end) \
do { if ((mm_struct) == current->mm) invalidate(); else printk("Can't invalidate_range(%x,%x,%x)\n", mm_struct, start, end);} while (0)
#endif

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
extern pte_t __bad_page(void);
extern pte_t * __bad_pagetable(void);

extern unsigned long __zero_page(void);

#define BAD_PAGETABLE __bad_pagetable()
#define BAD_PAGE __bad_page()
#define ZERO_PAGE __zero_page()

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR			(8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK			(~(sizeof(void*)-1))

/* sizeof(void*)==1<<SIZEOF_PTR_LOG2 */
/* 64-bit machines, beware!  SRB. */
#define SIZEOF_PTR_LOG2			2

/* to find an entry in a page-table */
#define PAGE_PTR(address) \
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

/* to set the page-dir */
/* tsk is a task_struct and pgdir is a pte_t */
#define SET_PAGE_DIR(tsk,pgdir) \
do { \
	(tsk)->tss.pg_tables = (unsigned long *)(pgdir); \
	if ((tsk) == current) \
	{ \
/*_printk("Change page tables = %x\n", pgdir);*/ \
	} \
} while (0)

extern unsigned long high_memory;

extern inline int pte_none(pte_t pte)		{ return !pte_val(pte); }
extern inline int pte_present(pte_t pte)	{ return pte_val(pte) & _PAGE_PRESENT; }
#if 0
extern inline int pte_inuse(pte_t *ptep)	{ return mem_map[MAP_NR(ptep)].reserved; }
/*extern inline int pte_inuse(pte_t *ptep)	{ return mem_map[MAP_NR(ptep)] != 1; }*/
#endif
extern inline void pte_clear(pte_t *ptep)	{ pte_val(*ptep) = 0; }
#if 0
extern inline void pte_reuse(pte_t * ptep)
{
	if (!mem_map[MAP_NR(ptep)].reserved)
		mem_map[MAP_NR(ptep)].count++;
}
#endif
/*
   extern inline void pte_reuse(pte_t * ptep)
{
	if (!(mem_map[MAP_NR(ptep)] & MAP_PAGE_RESERVED))
		mem_map[MAP_NR(ptep)]++;
}
*/
extern inline int pmd_none(pmd_t pmd)		{ return !pmd_val(pmd); }
extern inline int pmd_bad(pmd_t pmd)		{ return (pmd_val(pmd) & ~PAGE_MASK) != _PAGE_TABLE; }
extern inline int pmd_present(pmd_t pmd)	{ return pmd_val(pmd) & _PAGE_PRESENT; }
extern inline int pmd_inuse(pmd_t *pmdp)	{ return 0; }
extern inline void pmd_clear(pmd_t * pmdp)	{ pmd_val(*pmdp) = 0; }
extern inline void pmd_reuse(pmd_t * pmdp)	{ }

/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
extern inline int pgd_none(pgd_t pgd)		{ return 0; }
extern inline int pgd_bad(pgd_t pgd)		{ return 0; }
extern inline int pgd_present(pgd_t pgd)	{ return 1; }
#if 0
/*extern inline int pgd_inuse(pgd_t * pgdp)	{ return mem_map[MAP_NR(pgdp)] != 1; }*/
extern inline int pgd_inuse(pgd_t *pgdp)	{ return mem_map[MAP_NR(pgdp)].reserved;  }
#endif
extern inline void pgd_clear(pgd_t * pgdp)	{ }

/*
extern inline void pgd_reuse(pgd_t * pgdp)
{
	if (!mem_map[MAP_NR(pgdp)].reserved)
		mem_map[MAP_NR(pgdp)].count++;
}
*/

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)		{ return pte_val(pte) & _PAGE_USER; }
extern inline int pte_write(pte_t pte)		{ return pte_val(pte) & _PAGE_RW; }
extern inline int pte_exec(pte_t pte)		{ return pte_val(pte) & _PAGE_USER; }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }
extern inline int pte_cow(pte_t pte)		{ return pte_val(pte) & _PAGE_COW; }

extern inline pte_t pte_wrprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_RW; return pte; }
extern inline pte_t pte_rdprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_exprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~_PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
extern inline pte_t pte_uncow(pte_t pte)	{ pte_val(pte) &= ~_PAGE_COW; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)	{ pte_val(pte) |= _PAGE_RW; return pte; }
extern inline pte_t pte_mkread(pte_t pte)	{ pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkexec(pte_t pte)	{ pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)	{ pte_val(pte) |= _PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)	{ pte_val(pte) |= _PAGE_ACCESSED; return pte; }
extern inline pte_t pte_mkcow(pte_t pte)	{ pte_val(pte) |= _PAGE_COW; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern inline pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = page | pgprot_val(pgprot); return pte; }

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

/*extern inline void pmd_set(pmd_t * pmdp, pte_t * ptep)
{ pmd_val(*pmdp) = _PAGE_TABLE | ((((unsigned long) ptep) - PAGE_OFFSET) << (32-PAGE_SHIFT)); }
*/
extern inline unsigned long pte_page(pte_t pte)
{ return pte_val(pte) & PAGE_MASK; }

extern inline unsigned long pmd_page(pmd_t pmd)
{ return pmd_val(pmd) & PAGE_MASK; }


/* to find an entry in a page-table-directory */
extern inline pgd_t * pgd_offset(struct mm_struct * mm, unsigned long address)
{
	return mm->pgd + (address >> PGDIR_SHIFT);
}

/* Find an entry in the second-level page table.. */
extern inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) dir;
}

/* Find an entry in the third-level page table.. */ 
extern inline pte_t * pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) pmd_page(*dir) + ((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}


/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
extern inline void pte_free_kernel(pte_t * pte)
{
	free_page((unsigned long) pte);
}
/*extern inline void pte_free_kernel(pte_t * pte)
{
	mem_map[MAP_NR(pte)] = 1;
	free_page((unsigned long) pte);
}
*/

/*
extern inline pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t * page = (pte_t *) get_free_page(GFP_KERNEL);
		if (pmd_none(*pmd)) {
			if (page) {
				pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) page;
				mem_map[MAP_NR(page)] = MAP_PAGE_RESERVED;
				return page + address;
			}
			pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) BAD_PAGETABLE;
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) BAD_PAGETABLE;
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}*/
/*
extern inline pte_t * pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
printk("pte_alloc_kernel pmd = %08X, address = %08X\n", pmd, address);
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
printk("address now = %08X\n", address);
	if (pmd_none(*pmd)) {
		pte_t *page;
printk("pmd_none(*pmd) true\n");
		page = (pte_t *) get_free_page(GFP_KERNEL);
printk("page = %08X after get_free_page(%08X)\n",page,GFP_KERNEL);
		if (pmd_none(*pmd)) {
printk("pmd_none(*pmd=%08X) still\n",*pmd);		  
			if (page) {
printk("page true = %08X\n",page);			  
				pmd_set(pmd, page);
printk("pmd_set(%08X,%08X)\n",pmd,page);			  
				mem_map[MAP_NR(page)].reserved = 1;
printk("did mem_map\n",pmd,page);			  
				return page + address;
			}
printk("did pmd_set(%08X, %08X\n",pmd,BAD_PAGETABLE);			  
			pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
			return NULL;
		}
printk("did free_page(%08X)\n",page);			  		
		free_page((unsigned long) page);
	}
	if (pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
		return NULL;
	}
printk("returning pmd_page(%08X) + %08X\n",pmd_page(*pmd) , address);	  

	return (pte_t *) pmd_page(*pmd) + address;
}
*/
extern inline pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t * page = (pte_t *) get_free_page(GFP_KERNEL);
		if (pmd_none(*pmd)) {
			if (page) {
/*                                pmd_set(pmd,page);*/
			pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) page;
				return page + address;
			}
/*			pmd_set(pmd, BAD_PAGETABLE);*/
			pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) BAD_PAGETABLE;
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
/*		pmd_set(pmd, (pte_t *) BAD_PAGETABLE);		*/
		pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) BAD_PAGETABLE;
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
extern inline void pmd_free_kernel(pmd_t * pmd)
{
}

extern inline pmd_t * pmd_alloc_kernel(pgd_t * pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

extern inline void pte_free(pte_t * pte)
{
	free_page((unsigned long) pte);
}

extern inline pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t * page = (pte_t *) get_free_page(GFP_KERNEL);
		if (pmd_none(*pmd)) {
			if (page) {
				pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) page;
				return page + address;
			}
			pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) BAD_PAGETABLE;
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) BAD_PAGETABLE;
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
extern inline void pmd_free(pmd_t * pmd)
{
}

extern inline pmd_t * pmd_alloc(pgd_t * pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

extern inline void pgd_free(pgd_t * pgd)
{
	free_page((unsigned long) pgd);
}

extern inline pgd_t * pgd_alloc(void)
{
	return (pgd_t *) get_free_page(GFP_KERNEL);
}

extern pgd_t swapper_pg_dir[1024*8];
/*extern pgd_t *swapper_pg_dir;*/

/*
 * Software maintained MMU tables may have changed -- update the
 * hardware [aka cache]
 */
extern inline void update_mmu_cache(struct vm_area_struct * vma,
	unsigned long address, pte_t _pte)
{
#if 0
	printk("Update MMU cache - VMA: %x, Addr: %x, PTE: %x\n", vma, address, *(long *)&_pte);
	_printk("Update MMU cache - VMA: %x, Addr: %x, PTE: %x\n", vma, address, *(long *)&_pte);
/*	MMU_hash_page(&(vma->vm_task)->tss, address & PAGE_MASK, (pte *)&_pte);*/
#endif	
	MMU_hash_page(&(current)->tss, address & PAGE_MASK, (pte *)&_pte);

}


#ifdef _SCHED_INIT_
#define INIT_MMAP { &init_task, 0, 0x40000000, PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC }

#endif	

#define SWP_TYPE(entry) (((entry) >> 1) & 0x7f)
#define SWP_OFFSET(entry) ((entry) >> 8)
#define SWP_ENTRY(type,offset) (((type) << 1) | ((offset) << 8))

#endif /* _PPC_PAGE_H */

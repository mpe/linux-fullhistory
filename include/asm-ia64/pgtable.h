#ifndef _ASM_IA64_PGTABLE_H
#define _ASM_IA64_PGTABLE_H

/*
 * This file contains the functions and defines necessary to modify and use
 * the ia-64 page table tree.
 *
 * This hopefully works with any (fixed) ia-64 page-size, as defined
 * in <asm/page.h> (currently 8192).
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <asm/mman.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/types.h>

#define IA64_MAX_PHYS_BITS	50	/* max. number of physical address bits (architected) */

/* Is ADDR a valid kernel address? */
#define kern_addr_valid(addr)	((addr) >= TASK_SIZE)

/* Is ADDR a valid physical address? */
#define phys_addr_valid(addr)	(((addr) & my_cpu_data.unimpl_pa_mask) == 0)

/*
 * First, define the various bits in a PTE.  Note that the PTE format
 * matches the VHPT short format, the firt doubleword of the VHPD long
 * format, and the first doubleword of the TLB insertion format.
 */
#define _PAGE_P			(1 <<  0)	/* page present bit */
#define _PAGE_MA_WB		(0x0 <<  2)	/* write back memory attribute */
#define _PAGE_MA_UC		(0x4 <<  2)	/* uncacheable memory attribute */
#define _PAGE_MA_UCE		(0x5 <<  2)	/* UC exported attribute */
#define _PAGE_MA_WC		(0x6 <<  2)	/* write coalescing memory attribute */
#define _PAGE_MA_NAT		(0x7 <<  2)	/* not-a-thing attribute */
#define _PAGE_MA_MASK		(0x7 <<  2)
#define _PAGE_PL_0		(0 <<  7)	/* privilege level 0 (kernel) */
#define _PAGE_PL_1		(1 <<  7)	/* privilege level 1 (unused) */
#define _PAGE_PL_2		(2 <<  7)	/* privilege level 2 (unused) */
#define _PAGE_PL_3		(3 <<  7)	/* privilege level 3 (user) */
#define _PAGE_PL_MASK		(3 <<  7)
#define _PAGE_AR_R		(0 <<  9)	/* read only */
#define _PAGE_AR_RX		(1 <<  9)	/* read & execute */
#define _PAGE_AR_RW		(2 <<  9)	/* read & write */
#define _PAGE_AR_RWX		(3 <<  9)	/* read, write & execute */
#define _PAGE_AR_R_RW		(4 <<  9)	/* read / read & write */
#define _PAGE_AR_RX_RWX		(5 <<  9)	/* read & exec / read, write & exec */
#define _PAGE_AR_RWX_RW		(6 <<  9)	/* read, write & exec / read & write */
#define _PAGE_AR_X_RX		(7 <<  9)	/* exec & promote / read & exec */
#define _PAGE_AR_MASK		(7 <<  9)
#define _PAGE_AR_SHIFT		9
#define _PAGE_A			(1 <<  5)	/* page accessed bit */
#define _PAGE_D			(1 <<  6)	/* page dirty bit */
#define _PAGE_PPN_MASK		(((__IA64_UL(1) << IA64_MAX_PHYS_BITS) - 1) & ~0xfffUL)
#define _PAGE_ED		(__IA64_UL(1) << 52)	/* exception deferral */
#define _PAGE_PROTNONE		(__IA64_UL(1) << 63)

#define _PFN_MASK		_PAGE_PPN_MASK
#define _PAGE_CHG_MASK		(_PFN_MASK | _PAGE_A | _PAGE_D)

#define _PAGE_SIZE_4K	12
#define _PAGE_SIZE_8K	13
#define _PAGE_SIZE_16K	14
#define _PAGE_SIZE_64K	16
#define _PAGE_SIZE_256K	18
#define _PAGE_SIZE_1M	20
#define _PAGE_SIZE_4M	22
#define _PAGE_SIZE_16M	24
#define _PAGE_SIZE_64M	26
#define _PAGE_SIZE_256M	28

#define __ACCESS_BITS		_PAGE_ED | _PAGE_A | _PAGE_P | _PAGE_MA_WB
#define __DIRTY_BITS_NO_ED	_PAGE_A | _PAGE_P | _PAGE_D | _PAGE_MA_WB
#define __DIRTY_BITS		_PAGE_ED | __DIRTY_BITS_NO_ED

/*
 * Definitions for first level:
 *
 * PGDIR_SHIFT determines what a first-level page table entry can map.
 */
#define PGDIR_SHIFT		(PAGE_SHIFT + 2*(PAGE_SHIFT-3))
#define PGDIR_SIZE		(__IA64_UL(1) << PGDIR_SHIFT)
#define PGDIR_MASK		(~(PGDIR_SIZE-1))
#define PTRS_PER_PGD		(__IA64_UL(1) << (PAGE_SHIFT-3))
#define USER_PTRS_PER_PGD	PTRS_PER_PGD
#define FIRST_USER_PGD_NR	0

/*
 * Definitions for second level:
 *
 * PMD_SHIFT determines the size of the area a second-level page table
 * can map.
 */
#define PMD_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT-3))
#define PMD_SIZE	(__IA64_UL(1) << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))
#define PTRS_PER_PMD	(__IA64_UL(1) << (PAGE_SHIFT-3))

/*
 * Definitions for third level:
 */
#define PTRS_PER_PTE	(__IA64_UL(1) << (PAGE_SHIFT-3))

/* Number of pointers that fit on a page:  this will go away. */
#define PTRS_PER_PAGE	(__IA64_UL(1) << (PAGE_SHIFT-3))

# ifndef __ASSEMBLY__

#include <asm/bitops.h>
#include <asm/mmu_context.h>
#include <asm/system.h>

/*
 * All the normal masks have the "page accessed" bits on, as any time
 * they are used, the page is accessed. They are cleared only by the
 * page-out routines
 */
#define PAGE_NONE	__pgprot(_PAGE_PROTNONE | _PAGE_A)
#define PAGE_SHARED	__pgprot(__ACCESS_BITS | _PAGE_PL_3 | _PAGE_AR_RW)
#define PAGE_READONLY	__pgprot(__ACCESS_BITS | _PAGE_PL_3 | _PAGE_AR_R)
#define PAGE_COPY	__pgprot(__ACCESS_BITS | _PAGE_PL_3 | _PAGE_AR_RX)
#define PAGE_GATE	__pgprot(__ACCESS_BITS | _PAGE_PL_0 | _PAGE_AR_X_RX)
#define PAGE_KERNEL	__pgprot(__DIRTY_BITS  | _PAGE_PL_0 | _PAGE_AR_RWX)

/*
 * Next come the mappings that determine how mmap() protection bits
 * (PROT_EXEC, PROT_READ, PROT_WRITE, PROT_NONE) get implemented.  The
 * _P version gets used for a private shared memory segment, the _S
 * version gets used for a shared memory segment with MAP_SHARED on.
 * In a private shared memory segment, we do a copy-on-write if a task
 * attempts to write to the page.
 */
	/* xwr */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_READONLY	/* write to priv pg -> copy & make writable */
#define __P011	PAGE_READONLY	/* ditto */
#define __P100	__pgprot(_PAGE_ED | _PAGE_A | _PAGE_P | _PAGE_PL_3 | _PAGE_AR_X_RX)
#define __P101	__pgprot(_PAGE_ED | _PAGE_A | _PAGE_P | _PAGE_PL_3 | _PAGE_AR_RX)
#define __P110	__pgprot(_PAGE_ED | _PAGE_A | _PAGE_P | _PAGE_PL_3 | _PAGE_AR_RX)
#define __P111	__pgprot(_PAGE_ED | _PAGE_A | _PAGE_P | _PAGE_PL_3 | _PAGE_AR_RX)

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED	/* we don't have (and don't need) write-only */
#define __S011	PAGE_SHARED
#define __S100	__pgprot(_PAGE_ED | _PAGE_A | _PAGE_P | _PAGE_PL_3 | _PAGE_AR_X_RX)
#define __S101	__pgprot(_PAGE_ED | _PAGE_A | _PAGE_P | _PAGE_PL_3 | _PAGE_AR_RX)
#define __S110	__pgprot(_PAGE_ED | _PAGE_A | _PAGE_P | _PAGE_PL_3 | _PAGE_AR_RWX)
#define __S111	__pgprot(_PAGE_ED | _PAGE_A | _PAGE_P | _PAGE_PL_3 | _PAGE_AR_RWX)

#define pgd_ERROR(e)	printk("%s:%d: bad pgd %016lx.\n", __FILE__, __LINE__, pgd_val(e))
#define pmd_ERROR(e)	printk("%s:%d: bad pmd %016lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pte_ERROR(e)	printk("%s:%d: bad pte %016lx.\n", __FILE__, __LINE__, pte_val(e))


/*
 * Some definitions to translate between mem_map, PTEs, and page
 * addresses:
 */

/*
 * Given a pointer to an mem_map[] entry, return the kernel virtual
 * address corresponding to that page.
 */
#define page_address(page)	((void *) (PAGE_OFFSET + (((page) - mem_map) << PAGE_SHIFT)))

/*
 * Now for some cache flushing routines.  This is the kind of stuff
 * that can be very expensive, so try to avoid them whenever possible.
 */

/* Caches aren't brain-dead on the ia-64. */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(mm, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr)		do { } while (0)
#define flush_page_to_ram(page)			do { } while (0)
#define flush_dcache_page(page)			do { } while (0)
#define flush_icache_range(start, end)		do { } while (0)

extern void ia64_flush_icache_page (unsigned long addr);

#define flush_icache_page(vma,pg)				\
do {								\
	if ((vma)->vm_flags & PROT_EXEC)			\
		ia64_flush_icache_page((unsigned long) page_address(pg));	\
} while (0)

/*
 * Now come the defines and routines to manage and access the three-level
 * page table.
 */

/*
 * On some architectures, special things need to be done when setting
 * the PTE in a page table.  Nothing special needs to be on ia-64.
 */
#define set_pte(ptep, pteval)	(*(ptep) = (pteval))

#define VMALLOC_START		(0xa000000000000000+2*PAGE_SIZE)
#define VMALLOC_VMADDR(x)	((unsigned long)(x))
#define VMALLOC_END		0xbfffffffffffffff

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero:  used
 * for zero-mapped memory areas etc..
 */
extern pte_t ia64_bad_page (void);
extern pmd_t *ia64_bad_pagetable (void);

#define BAD_PAGETABLE	ia64_bad_pagetable()
#define BAD_PAGE	ia64_bad_page()

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page,pgprot)							\
({										\
	pte_t __pte;								\
										\
	pte_val(__pte) = ((page - mem_map) << PAGE_SHIFT) | pgprot_val(pgprot);	\
	__pte;									\
})

/* This takes a physical page address that is used by the remapping functions */
#define mk_pte_phys(physpage, pgprot) \
({ pte_t __pte; pte_val(__pte) = physpage + pgprot_val(pgprot); __pte; })

#define pte_modify(_pte, newprot) \
	(__pte((pte_val(_pte) & _PAGE_CHG_MASK) | pgprot_val(newprot)))

#define page_pte_prot(page,prot)	mk_pte(page, prot)
#define page_pte(page)			page_pte_prot(page, __pgprot(0))

#define pte_none(pte) 			(!pte_val(pte))
#define pte_present(pte)		(pte_val(pte) & (_PAGE_P | _PAGE_PROTNONE))
#define pte_clear(pte)			(pte_val(*(pte)) = 0UL)
/* pte_page() returns the "struct page *" corresponding to the PTE: */
#define pte_page(pte)			(mem_map + (unsigned long) ((pte_val(pte) & _PFN_MASK) >> PAGE_SHIFT))

#define pmd_set(pmdp, ptep) 		(pmd_val(*(pmdp)) = __pa(ptep))
#define pmd_none(pmd)			(!pmd_val(pmd))
#define pmd_bad(pmd)			(!phys_addr_valid(pmd_val(pmd)))
#define pmd_present(pmd)		(pmd_val(pmd) != 0UL)
#define pmd_clear(pmdp)			(pmd_val(*(pmdp)) = 0UL)
#define pmd_page(pmd)			((unsigned long) __va(pmd_val(pmd) & _PFN_MASK))

#define pgd_set(pgdp, pmdp)		(pgd_val(*(pgdp)) = __pa(pmdp))
#define pgd_none(pgd)			(!pgd_val(pgd))
#define pgd_bad(pgd)			(!phys_addr_valid(pgd_val(pgd)))
#define pgd_present(pgd)		(pgd_val(pgd) != 0UL)
#define pgd_clear(pgdp)			(pgd_val(*(pgdp)) = 0UL)
#define pgd_page(pgd)			((unsigned long) __va(pgd_val(pgd) & _PFN_MASK))

/*
 * The following have defined behavior only work if pte_present() is true.
 */
#define pte_read(pte)		(((pte_val(pte) & _PAGE_AR_MASK) >> _PAGE_AR_SHIFT) < 6)
#define pte_write(pte)	((unsigned) (((pte_val(pte) & _PAGE_AR_MASK) >> _PAGE_AR_SHIFT) - 2) < 4)
#define pte_dirty(pte)		(pte_val(pte) & _PAGE_D)
#define pte_young(pte)		(pte_val(pte) & _PAGE_A)
/*
 * Note: we convert AR_RWX to AR_RX and AR_RW to AR_R by clearing the
 * 2nd bit in the access rights:
 */
#define pte_wrprotect(pte)	(__pte(pte_val(pte) & ~_PAGE_AR_RW))
#define pte_mkwrite(pte)	(__pte(pte_val(pte) | _PAGE_AR_RW))

#define pte_mkold(pte)		(__pte(pte_val(pte) & ~_PAGE_A))
#define pte_mkyoung(pte)	(__pte(pte_val(pte) | _PAGE_A))

#define pte_mkclean(pte)	(__pte(pte_val(pte) & ~_PAGE_D))
#define pte_mkdirty(pte)	(__pte(pte_val(pte) | _PAGE_D))

/*
 * Macro to make mark a page protection value as "uncacheable".  Note
 * that "protection" is really a misnomer here as the protection value
 * contains the memory attribute bits, dirty bits, and various other
 * bits as well.
 */
#define pgprot_noncached(prot)	__pgprot((pgprot_val(prot) & ~_PAGE_MA_MASK) | _PAGE_MA_UC)

/*
 * Return the region index for virtual address ADDRESS.
 */
extern __inline__ unsigned long
rgn_index (unsigned long address)
{
	ia64_va a;

	a.l = address;
	return a.f.reg;
}

/*
 * Return the region offset for virtual address ADDRESS.
 */
extern __inline__ unsigned long
rgn_offset (unsigned long address)
{
	ia64_va a;

	a.l = address;
	return a.f.off;
}

#define RGN_SIZE	(1UL << 61)
#define RGN_KERNEL	7

extern __inline__ unsigned long
pgd_index (unsigned long address)
{
	unsigned long region = address >> 61;
	unsigned long l1index = (address >> PGDIR_SHIFT) & ((PTRS_PER_PGD >> 3) - 1);

	return (region << (PAGE_SHIFT - 6)) | l1index;
}

/* The offset in the 1-level directory is given by the 3 region bits
   (61..63) and the seven level-1 bits (33-39).  */
extern __inline__ pgd_t*
pgd_offset (struct mm_struct *mm, unsigned long address)
{
	return mm->pgd + pgd_index(address);
}

/* In the kernel's mapped region we have a full 43 bit space available and completely
   ignore the region number (since we know its in region number 5). */
#define pgd_offset_k(addr) \
	(init_mm.pgd + (((addr) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1)))

/* Find an entry in the second-level page table.. */
#define pmd_offset(dir,addr) \
	((pmd_t *) pgd_page(*(dir)) + (((addr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1)))

/* Find an entry in the third-level page table.. */
#define pte_offset(dir,addr) \
	((pte_t *) pmd_page(*(dir)) + (((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1)))


extern pgd_t swapper_pg_dir[PTRS_PER_PGD];
extern void paging_init (void);

/*
 * IA-64 doesn't have any external MMU info: the page tables contain
 * all the necessary information.  However, we can use this macro
 * to pre-install (override) a PTE that we know is needed anyhow.
 *
 * Asit says that on Itanium, it is generally faster to let the VHPT
 * walker pick up a newly installed PTE (and VHPT misses should be
 * extremely rare compared to normal misses).  Also, since
 * pre-installing the PTE has the problem that we may evict another
 * TLB entry needlessly because we don't know for sure whether we need
 * to update the iTLB or dTLB, I tend to prefer this solution, too.
 * Also, this avoids nasty issues with forward progress (what if the
 * newly installed PTE gets replaced before we return to the previous
 * execution context?).
 *
 */
#if 1
# define update_mmu_cache(vma,address,pte)
#else
# define update_mmu_cache(vma,address,pte)							\
do {												\
	/*											\
	 * XXX fix me!!										\
	 *											\
	 * It's not clear this is a win.  We may end up pollute the				\
	 * dtlb with itlb entries and vice versa (e.g., consider stack				\
	 * pages that are normally marked executable).  It would be				\
	 * better to insert the TLB entry for the TLB cache that we				\
	 * know needs the new entry.  However, the update_mmu_cache()				\
	 * arguments don't tell us whether we got here through a data				\
	 * access or through an instruction fetch.  Talk to Linus to				\
	 * fix this.										\
	 *											\
	 * If you re-enable this code, you must disable the ptc code in				\
	 * Entry 20 of the ivt.									\
	 */											\
	unsigned long flags;									\
												\
	ia64_clear_ic(flags);									\
	ia64_itc((vma->vm_flags & PROT_EXEC) ? 0x3 : 0x2, address, pte_val(pte), PAGE_SHIFT);	\
	__restore_flags(flags);									\
} while (0)
#endif

#define SWP_TYPE(entry)			(((entry).val >> 1) & 0xff)
#define SWP_OFFSET(entry)		((entry).val >> 9)
#define SWP_ENTRY(type,offset)		((swp_entry_t) { ((type) << 1) | ((offset) << 9) })
#define pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define swp_entry_to_pte(x)		((pte_t) { (x).val })

#define module_map	vmalloc
#define module_unmap	vfree

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define PageSkip(page)		(0)

#define io_remap_page_range remap_page_range	/* XXX is this right? */

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[1024];
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

# endif /* !__ASSEMBLY__ */

#endif /* _ASM_IA64_PGTABLE_H */

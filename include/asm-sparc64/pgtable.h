/* $Id: pgtable.h,v 1.156 2002/02/09 19:49:31 davem Exp $
 * pgtable.h: SpitFire page table operations.
 *
 * Copyright 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#ifndef _SPARC64_PGTABLE_H
#define _SPARC64_PGTABLE_H

/* This file contains the functions and defines necessary to modify and use
 * the SpitFire page tables.
 */

#include <linux/config.h>
#include <asm/spitfire.h>
#include <asm/asi.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/processor.h>

/* The kernel image occupies 0x4000000 to 0x1000000 (4MB --> 16MB).
 * The page copy blockops use 0x1000000 to 0x18000000 (16MB --> 24MB).
 * The PROM resides in an area spanning 0xf0000000 to 0x100000000.
 * The vmalloc area spans 0x140000000 to 0x200000000.
 * There is a single static kernel PMD which maps from 0x0 to address
 * 0x400000000.
 */
#define	TLBTEMP_BASE		0x0000000001000000
#define MODULES_VADDR		0x0000000002000000
#define MODULES_LEN		0x000000007e000000
#define MODULES_END		0x0000000080000000
#define VMALLOC_START		0x0000000140000000
#define VMALLOC_END		0x0000000200000000
#define LOW_OBP_ADDRESS		0x00000000f0000000
#define HI_OBP_ADDRESS		0x0000000100000000

/* XXX All of this needs to be rethought so we can take advantage
 * XXX cheetah's full 64-bit virtual address space, ie. no more hole
 * XXX in the middle like on spitfire. -DaveM
 */
/*
 * Given a virtual address, the lowest PAGE_SHIFT bits determine offset
 * into the page; the next higher PAGE_SHIFT-3 bits determine the pte#
 * in the proper pagetable (the -3 is from the 8 byte ptes, and each page
 * table is a single page long). The next higher PMD_BITS determine pmd# 
 * in the proper pmdtable (where we must have PMD_BITS <= (PAGE_SHIFT-2) 
 * since the pmd entries are 4 bytes, and each pmd page is a single page 
 * long). Finally, the higher few bits determine pgde#.
 */

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT-3))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))
#define PMD_BITS	11

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT-3) + PMD_BITS)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

#ifndef __ASSEMBLY__

#include <linux/sched.h>

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

/* Entries per page directory level. */
#define PTRS_PER_PTE		(1UL << (PAGE_SHIFT-3))

/* We the first one in this file, what we export to the kernel
 * is different so we can optimize correctly for 32-bit tasks.
 */
#define REAL_PTRS_PER_PMD	(1UL << PMD_BITS)
#define PTRS_PER_PMD		((const int)(test_thread_flag(TIF_32BIT) ? \
				 (1UL << (32 - (PAGE_SHIFT-3) - PAGE_SHIFT)) : (REAL_PTRS_PER_PMD)))

/*
 * We cannot use the top address range because VPTE table lives there. This
 * formula finds the total legal virtual space in the processor, subtracts the
 * vpte size, then aligns it to the number of bytes mapped by one pgde, and
 * thus calculates the number of pgdes needed.
 */
#define PTRS_PER_PGD	(((1UL << VA_BITS) - VPTE_SIZE + (1UL << (PAGE_SHIFT + \
			(PAGE_SHIFT-3) + PMD_BITS)) - 1) / (1UL << (PAGE_SHIFT + \
			(PAGE_SHIFT-3) + PMD_BITS)))

/* Kernel has a separate 44bit address space. */
#define USER_PTRS_PER_PGD	((const int)(test_thread_flag(TIF_32BIT)) ? \
				 (1) : (PTRS_PER_PGD))
#define FIRST_USER_PGD_NR	0

#define pte_ERROR(e)	__builtin_trap()
#define pmd_ERROR(e)	__builtin_trap()
#define pgd_ERROR(e)	__builtin_trap()

#endif /* !(__ASSEMBLY__) */

/* Spitfire/Cheetah TTE bits. */
#define _PAGE_VALID	0x8000000000000000	/* Valid TTE                          */
#define _PAGE_R		0x8000000000000000	/* Used to keep ref bit up to date    */
#define _PAGE_SZ4MB	0x6000000000000000	/* 4MB Page                           */
#define _PAGE_SZ512K	0x4000000000000000	/* 512K Page                          */
#define _PAGE_SZ64K	0x2000000000000000	/* 64K Page                           */
#define _PAGE_SZ8K	0x0000000000000000	/* 8K Page                            */
#define _PAGE_NFO	0x1000000000000000	/* No Fault Only                      */
#define _PAGE_IE	0x0800000000000000	/* Invert Endianness                  */
#define _PAGE_SN	0x0000800000000000	/* (Cheetah) Snoop                    */
#define _PAGE_PADDR_SF	0x000001FFFFFFE000	/* (Spitfire) Phys Address [40:13]    */
#define _PAGE_PADDR	0x000007FFFFFFE000	/* (Cheetah) Phys Address [42:13]     */
#define _PAGE_SOFT	0x0000000000001F80	/* Software bits                      */
#define _PAGE_L		0x0000000000000040	/* Locked TTE                         */
#define _PAGE_CP	0x0000000000000020	/* Cacheable in Physical Cache        */
#define _PAGE_CV	0x0000000000000010	/* Cacheable in Virtual Cache         */
#define _PAGE_E		0x0000000000000008	/* side-Effect                        */
#define _PAGE_P		0x0000000000000004	/* Privileged Page                    */
#define _PAGE_W		0x0000000000000002	/* Writable                           */
#define _PAGE_G		0x0000000000000001	/* Global                             */

/* Here are the SpitFire software bits we use in the TTE's. */
#define _PAGE_FILE	0x0000000000001000	/* Pagecache page                     */
#define _PAGE_MODIFIED	0x0000000000000800	/* Modified Page (ie. dirty)          */
#define _PAGE_ACCESSED	0x0000000000000400	/* Accessed Page (ie. referenced)     */
#define _PAGE_READ	0x0000000000000200	/* Readable SW Bit                    */
#define _PAGE_WRITE	0x0000000000000100	/* Writable SW Bit                    */
#define _PAGE_PRESENT	0x0000000000000080	/* Present Page (ie. not swapped out) */

#if PAGE_SHIFT == 13
#define _PAGE_SZBITS	_PAGE_SZ8K
#elif PAGE_SHIFT == 16
#define _PAGE_SZBITS	_PAGE_SZ64K
#elif PAGE_SHIFT == 19
#define _PAGE_SZBITS	_PAGE_SZ512K
#elif PAGE_SHIFT == 22
#define _PAGE_SZBITS	_PAGE_SZ4MB
#else
#error Wrong PAGE_SHIFT specified
#endif

#if defined(CONFIG_HUGETLB_PAGE_SIZE_4MB)
#define _PAGE_SZHUGE	_PAGE_SZ4MB
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_512K)
#define _PAGE_SZHUGE	_PAGE_SZ512K
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_64K)
#define _PAGE_SZHUGE	_PAGE_SZ64K
#endif

#define _PAGE_CACHE	(_PAGE_CP | _PAGE_CV)

#define __DIRTY_BITS	(_PAGE_MODIFIED | _PAGE_WRITE | _PAGE_W)
#define __ACCESS_BITS	(_PAGE_ACCESSED | _PAGE_READ | _PAGE_R)
#define __PRIV_BITS	_PAGE_P

#define PAGE_NONE	__pgprot (_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_CACHE)

/* Don't set the TTE _PAGE_W bit here, else the dirty bit never gets set. */
#define PAGE_SHARED	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __ACCESS_BITS | _PAGE_WRITE)

#define PAGE_COPY	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __ACCESS_BITS)

#define PAGE_READONLY	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __ACCESS_BITS)

#define PAGE_KERNEL	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __PRIV_BITS | __ACCESS_BITS | __DIRTY_BITS)

#define _PFN_MASK	_PAGE_PADDR

#define pg_iobits (_PAGE_VALID | _PAGE_PRESENT | __DIRTY_BITS | __ACCESS_BITS | _PAGE_E)

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

#ifndef __ASSEMBLY__

extern unsigned long phys_base;
extern unsigned long pfn_base;

extern struct page *mem_map_zero;
#define ZERO_PAGE(vaddr)	(mem_map_zero)

/* PFNs are real physical page numbers.  However, mem_map only begins to record
 * per-page information starting at pfn_base.  This is to handle systems where
 * the first physical page in the machine is at some huge physical address, such
 * as 4GB.   This is common on a partitioned E10000, for example.
 */

#define pfn_pte(pfn, prot)	\
	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot) | _PAGE_SZBITS)
#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))

#define pte_pfn(x)		((pte_val(x) & _PAGE_PADDR)>>PAGE_SHIFT)
#define pte_page(x)		pfn_to_page(pte_pfn(x))

#define page_pte_prot(page, prot)	mk_pte(page, prot)
#define page_pte(page)			page_pte_prot(page, __pgprot(0))

static inline pte_t pte_modify(pte_t orig_pte, pgprot_t new_prot)
{
	pte_t __pte;
	const unsigned long preserve_mask = (_PFN_MASK |
					     _PAGE_MODIFIED | _PAGE_ACCESSED |
					     _PAGE_CACHE | _PAGE_E |
					     _PAGE_PRESENT | _PAGE_SZBITS);

	pte_val(__pte) = (pte_val(orig_pte) & preserve_mask) |
		(pgprot_val(new_prot) & ~preserve_mask);

	return __pte;
}
#define pmd_set(pmdp, ptep)	\
	(pmd_val(*(pmdp)) = (__pa((unsigned long) (ptep)) >> 11UL))
#define pgd_set(pgdp, pmdp)	\
	(pgd_val(*(pgdp)) = (__pa((unsigned long) (pmdp)) >> 11UL))
#define __pmd_page(pmd)			((unsigned long) __va((pmd_val(pmd)<<11UL)))
#define pmd_page(pmd) 			virt_to_page((void *)__pmd_page(pmd))
#define pgd_page(pgd)			((unsigned long) __va((pgd_val(pgd)<<11UL)))
#define pte_none(pte) 			(!pte_val(pte))
#define pte_present(pte)		(pte_val(pte) & _PAGE_PRESENT)
#define pte_clear(pte)			(pte_val(*(pte)) = 0UL)
#define pmd_none(pmd)			(!pmd_val(pmd))
#define pmd_bad(pmd)			(0)
#define pmd_present(pmd)		(pmd_val(pmd) != 0UL)
#define pmd_clear(pmdp)			(pmd_val(*(pmdp)) = 0UL)
#define pgd_none(pgd)			(!pgd_val(pgd))
#define pgd_bad(pgd)			(0)
#define pgd_present(pgd)		(pgd_val(pgd) != 0UL)
#define pgd_clear(pgdp)			(pgd_val(*(pgdp)) = 0UL)

/* The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
#define pte_read(pte)		(pte_val(pte) & _PAGE_READ)
#define pte_exec(pte)		pte_read(pte)
#define pte_write(pte)		(pte_val(pte) & _PAGE_WRITE)
#define pte_dirty(pte)		(pte_val(pte) & _PAGE_MODIFIED)
#define pte_young(pte)		(pte_val(pte) & _PAGE_ACCESSED)
#define pte_wrprotect(pte)	(__pte(pte_val(pte) & ~(_PAGE_WRITE|_PAGE_W)))
#define pte_rdprotect(pte)	(__pte(((pte_val(pte)<<1UL)>>1UL) & ~_PAGE_READ))
#define pte_mkclean(pte)	(__pte(pte_val(pte) & ~(_PAGE_MODIFIED|_PAGE_W)))
#define pte_mkold(pte)		(__pte(((pte_val(pte)<<1UL)>>1UL) & ~_PAGE_ACCESSED))

/* Permanent address of a page. */
#define __page_address(page)	page_address(page)

/* Be very careful when you change these three, they are delicate. */
#define pte_mkyoung(pte)	(__pte(pte_val(pte) | _PAGE_ACCESSED | _PAGE_R))
#define pte_mkwrite(pte)	(__pte(pte_val(pte) | _PAGE_WRITE))
#define pte_mkdirty(pte)	(__pte(pte_val(pte) | _PAGE_MODIFIED | _PAGE_W))

/* to find an entry in a page-table-directory. */
#define pgd_index(address)	(((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD))
#define pgd_offset(mm, address)	((mm)->pgd + pgd_index(address))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* Find an entry in the second-level page table.. */
#define pmd_offset(dir, address)	((pmd_t *) pgd_page(*(dir)) + \
					((address >> PMD_SHIFT) & (REAL_PTRS_PER_PMD-1)))

/* Find an entry in the third-level page table.. */
#define pte_index(dir, address)	((pte_t *) __pmd_page(*(dir)) + \
					((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1)))
#define pte_offset_kernel		pte_index
#define pte_offset_map			pte_index
#define pte_offset_map_nested		pte_index
#define pte_unmap(pte)			do { } while (0)
#define pte_unmap_nested(pte)		do { } while (0)

extern pgd_t swapper_pg_dir[1];

/* These do nothing with the way I have things setup. */
#define mmu_lockarea(vaddr, len)		(vaddr)
#define mmu_unlockarea(vaddr, len)		do { } while(0)

struct vm_area_struct;
extern void update_mmu_cache(struct vm_area_struct *, unsigned long, pte_t);

/* Make a non-present pseudo-TTE. */
static inline pte_t mk_pte_io(unsigned long page, pgprot_t prot, int space)
{
	pte_t pte;
	pte_val(pte) = ((page) | pgprot_val(prot) | _PAGE_E) & ~(unsigned long)_PAGE_CACHE;
	pte_val(pte) |= (((unsigned long)space) << 32);
	return pte;
}

/* Encode and de-code a swap entry */
#define __swp_type(entry)	(((entry).val >> PAGE_SHIFT) & 0xffUL)
#define __swp_offset(entry)	((entry).val >> (PAGE_SHIFT + 8UL))
#define __swp_entry(type, offset)	\
	( (swp_entry_t) \
	  { \
		(((long)(type) << PAGE_SHIFT) | \
                 ((long)(offset) << (PAGE_SHIFT + 8UL))) \
	  } )
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

/* File offset in PTE support. */
#define pte_file(pte)		(pte_val(pte) & _PAGE_FILE)
#define pte_to_pgoff(pte)	(pte_val(pte) >> PAGE_SHIFT)
#define pgoff_to_pte(off)	(__pte(((off) << PAGE_SHIFT) | _PAGE_FILE))
#define PTE_FILE_MAX_BITS	(64UL - PAGE_SHIFT - 1UL)

extern unsigned long prom_virt_to_phys(unsigned long, int *);

static __inline__ unsigned long
sun4u_get_pte (unsigned long addr)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	if (addr >= PAGE_OFFSET)
		return addr & _PAGE_PADDR;
	if ((addr >= LOW_OBP_ADDRESS) && (addr < HI_OBP_ADDRESS))
		return prom_virt_to_phys(addr, 0);
	pgdp = pgd_offset_k(addr);
	pmdp = pmd_offset(pgdp, addr);
	ptep = pte_offset_kernel(pmdp, addr);
	return pte_val(*ptep) & _PAGE_PADDR;
}

static __inline__ unsigned long
__get_phys (unsigned long addr)
{
	return sun4u_get_pte (addr);
}

static __inline__ int
__get_iospace (unsigned long addr)
{
	return ((sun4u_get_pte (addr) & 0xf0000000) >> 28);
}

extern unsigned long *sparc64_valid_addr_bitmap;

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define kern_addr_valid(addr)	\
	(test_bit(__pa((unsigned long)(addr))>>22, sparc64_valid_addr_bitmap))

extern int io_remap_page_range(struct vm_area_struct *vma, unsigned long from, unsigned long offset,
			       unsigned long size, pgprot_t prot, int space);

#include <asm-generic/pgtable.h>

/* We provide our own get_unmapped_area to cope with VA holes for userland */
#define HAVE_ARCH_UNMAPPED_AREA

/* We provide a special get_unmapped_area for framebuffer mmaps to try and use
 * the largest alignment possible such that larget PTEs can be used.
 */
extern unsigned long get_fb_unmapped_area(struct file *filp, unsigned long, unsigned long, unsigned long, unsigned long);
#define HAVE_ARCH_FB_UNMAPPED_AREA

/*
 * No page table caches to initialise
 */
#define pgtable_cache_init()	do { } while (0)

extern void check_pgt_cache(void);

typedef pte_t *pte_addr_t;

#endif /* !(__ASSEMBLY__) */

#endif /* !(_SPARC64_PGTABLE_H) */

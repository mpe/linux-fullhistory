/*
 *  include/asm-s390/pgtable.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *               Ulrich Weigand (weigand@de.ibm.com)
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/pgtable.h"
 */

#ifndef _ASM_S390_PGTABLE_H
#define _ASM_S390_PGTABLE_H

/*
 * The Linux memory management assumes a three-level page table setup. On
 * the S390, we use that, but "fold" the mid level into the top-level page
 * table, so that we physically have the same two-level page table as the
 * S390 mmu expects.
 *
 * The "pgd_xxx()" functions are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 *
 * This file contains the functions and defines necessary to modify and use
 * the S390 page table tree.
 */
#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <linux/threads.h>

extern pgd_t swapper_pg_dir[] __attribute__ ((aligned (4096)));
extern void paging_init(void);

/* Caches aren't brain-dead on S390. */
#define flush_cache_all()                       do { } while (0)
#define flush_cache_mm(mm)                      do { } while (0)
#define flush_cache_range(mm, start, end)       do { } while (0)
#define flush_cache_page(vma, vmaddr)           do { } while (0)
#define flush_page_to_ram(page)                 do { } while (0)
#define flush_dcache_page(page)			do { } while (0)
#define flush_icache_range(start, end)          do { } while (0)
#define flush_icache_page(vma,pg)               do { } while (0)

/*
 * The S390 doesn't have any external MMU info: the kernel page
 * tables contain all the necessary information.
 */
#define update_mmu_cache(vma, address, pte)     do { } while (0)

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern char empty_zero_page[PAGE_SIZE];
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))
#endif /* !__ASSEMBLY__ */

/*
 * PMD_SHIFT determines the size of the area a second-level page
 * table can map
 */
#define PMD_SHIFT       22
#define PMD_SIZE        (1UL << PMD_SHIFT)
#define PMD_MASK        (~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT     22
#define PGDIR_SIZE      (1UL << PGDIR_SHIFT)
#define PGDIR_MASK      (~(PGDIR_SIZE-1))

/*
 * entries per page directory level: the S390 is two-level, so
 * we don't really have any PMD directory physically.
 * for S390 segment-table entries are combined to one PGD
 * that leads to 1024 pte per pgd
 */
#define PTRS_PER_PTE    1024
#define PTRS_PER_PMD    1
#define PTRS_PER_PGD    512

/*
 * pgd entries used up by user/kernel:
 */
#define USER_PTRS_PER_PGD  512
#define USER_PGD_PTRS      512
#define KERNEL_PGD_PTRS    512
#define FIRST_USER_PGD_NR  0

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

#ifndef __ASSEMBLY__
/*
 * Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_OFFSET  (8*1024*1024)
#define VMALLOC_START   (((unsigned long) high_memory + VMALLOC_OFFSET) \
			 & ~(VMALLOC_OFFSET-1))
#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_END     (0x7fffffffL)


/*
 * A pagetable entry of S390 has following format:
 *  |   PFRA          |    |  OS  |
 * 0                   0IP0
 * 00000000001111111111222222222233
 * 01234567890123456789012345678901
 *
 * I Page-Invalid Bit:    Page is not available for address-translation
 * P Page-Protection Bit: Store access not possible for page
 *
 * A segmenttable entry of S390 has following format:
 *  |   P-table origin      |  |PTL
 * 0                         IC
 * 00000000001111111111222222222233
 * 01234567890123456789012345678901
 *
 * I Segment-Invalid Bit:    Segment is not available for address-translation
 * C Common-Segment Bit:     Segment is not private (PoP 3-30)
 * PTL Page-Table-Length:    Page-table length (PTL+1*16 entries -> up to 256)
 *
 * The segmenttable origin of S390 has following format:
 *
 *  |S-table origin   |     | STL |
 * X                   **GPS
 * 00000000001111111111222222222233
 * 01234567890123456789012345678901
 *
 * X Space-Switch event:
 * G Segment-Invalid Bit:     *
 * P Private-Space Bit:       Segment is not private (PoP 3-30)
 * S Storage-Alteration:
 * STL Segment-Table-Length:  Segment-table length (STL+1*16 entries -> up to 2048)
 *
 * A storage key has the following format:
 * | ACC |F|R|C|0|
 *  0   3 4 5 6 7
 * ACC: access key
 * F  : fetch protection bit
 * R  : referenced bit
 * C  : changed bit
 */

/* Bits in the page table entry */
#define _PAGE_PRESENT   0x001          /* Software                         */
#define _PAGE_MKCLEAR   0x002          /* Software                         */
#define _PAGE_RO        0x200          /* HW read-only                     */
#define _PAGE_INVALID   0x400          /* HW invalid                       */

/* Bits in the segment table entry */
#define _PAGE_TABLE_LEN 0xf            /* only full page-tables            */
#define _PAGE_TABLE_COM 0x10           /* common page-table                */
#define _PAGE_TABLE_INV 0x20           /* invalid page-table               */
#define _SEG_PRESENT    0x001          /* Software (overlap with PTL)      */

/* Bits int the storage key */
#define _PAGE_CHANGED    0x02          /* HW changed bit                   */
#define _PAGE_REFERENCED 0x04          /* HW referenced bit                */

#define _USER_SEG_TABLE_LEN    0x7f    /* user-segment-table up to 2 GB    */
#define _KERNEL_SEG_TABLE_LEN  0x7f    /* kernel-segment-table up to 2 GB  */

/*
 * User and Kernel pagetables are identical
 */
#define _PAGE_TABLE     (_PAGE_TABLE_LEN )
#define _KERNPG_TABLE   (_PAGE_TABLE_LEN )

/*
 * The Kernel segment-tables includes the User segment-table
 */

#define _SEGMENT_TABLE  (_USER_SEG_TABLE_LEN|0x80000000|0x100)
#define _KERNSEG_TABLE  (_KERNEL_SEG_TABLE_LEN)

/*
 * No mapping available
 */
#define PAGE_INVALID  __pgprot(_PAGE_INVALID)
#define PAGE_NONE     __pgprot(_PAGE_PRESENT | _PAGE_INVALID)
#define PAGE_COPY     __pgprot(_PAGE_PRESENT | _PAGE_RO)
#define PAGE_READONLY __pgprot(_PAGE_PRESENT | _PAGE_RO)
#define PAGE_SHARED   __pgprot(_PAGE_PRESENT)
#define PAGE_KERNEL   __pgprot(_PAGE_PRESENT)

/*
 * The S390 can't do page protection for execute, and considers that the
 * same are read. Also, write permissions imply read permissions. This is
 * the closest we can get..
 */
         /*xwr*/
#define __P000  PAGE_NONE
#define __P001  PAGE_READONLY
#define __P010  PAGE_COPY
#define __P011  PAGE_COPY
#define __P100  PAGE_READONLY
#define __P101  PAGE_READONLY
#define __P110  PAGE_COPY
#define __P111  PAGE_COPY

#define __S000  PAGE_NONE
#define __S001  PAGE_READONLY
#define __S010  PAGE_SHARED
#define __S011  PAGE_SHARED
#define __S100  PAGE_READONLY
#define __S101  PAGE_READONLY
#define __S110  PAGE_SHARED
#define __S111  PAGE_SHARED

/*
 * Certain architectures need to do special things when PTEs
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
extern inline void set_pte(pte_t *pteptr, pte_t pteval)
{
	if ((pte_val(pteval) & (_PAGE_MKCLEAR|_PAGE_INVALID))
	    == _PAGE_MKCLEAR) 
	{
		pte_val(pteval) &= ~_PAGE_MKCLEAR;
               
		asm volatile ("sske %0,%1" 
				: : "d" (0), "a" (pte_val(pteval)));
	}

	*pteptr = pteval;
}

/*
 * Permanent address of a page.
 */
#define page_address(page) ((page)->virtual)
#define pages_to_mb(x) ((x) >> (20-PAGE_SHIFT))

/*
 * pgd/pmd/pte query functions
 */
extern inline int pgd_present(pgd_t pgd) { return 1; }
extern inline int pgd_none(pgd_t pgd)    { return 0; }
extern inline int pgd_bad(pgd_t pgd)     { return 0; }

extern inline int pmd_present(pmd_t pmd) { return pmd_val(pmd) & _SEG_PRESENT; }
extern inline int pmd_none(pmd_t pmd)    { return pmd_val(pmd) & _PAGE_TABLE_INV; }
extern inline int pmd_bad(pmd_t pmd)
{
	return (pmd_val(pmd) & (~PAGE_MASK & ~_PAGE_TABLE_INV)) != _PAGE_TABLE;
}

extern inline int pte_present(pte_t pte) { return pte_val(pte) & _PAGE_PRESENT; }
extern inline int pte_none(pte_t pte)
{
	return ((pte_val(pte) & 
                (_PAGE_INVALID | _PAGE_RO | _PAGE_PRESENT)) == _PAGE_INVALID);
}

#define pte_same(a,b)	(pte_val(a) == pte_val(b))

/*
 * query functions pte_write/pte_dirty/pte_young only work if
 * pte_present() is true. Undefined behaviour if not..
 */
extern inline int pte_write(pte_t pte)
{
	return (pte_val(pte) & _PAGE_RO) == 0;
}

extern inline int pte_dirty(pte_t pte)
{
	int skey;

	asm volatile ("iske %0,%1" : "=d" (skey) : "a" (pte_val(pte)));
	return skey & _PAGE_CHANGED;
}

extern inline int pte_young(pte_t pte)
{
	int skey;

	asm volatile ("iske %0,%1" : "=d" (skey) : "a" (pte_val(pte)));
	return skey & _PAGE_REFERENCED;
}

/*
 * pgd/pmd/pte modification functions
 */
extern inline void pgd_clear(pgd_t * pgdp)      { }

extern inline void pmd_clear(pmd_t * pmdp)
{
	pmd_val(pmdp[0]) = _PAGE_TABLE_INV;
	pmd_val(pmdp[1]) = _PAGE_TABLE_INV;
	pmd_val(pmdp[2]) = _PAGE_TABLE_INV;
	pmd_val(pmdp[3]) = _PAGE_TABLE_INV;
}

extern inline void pte_clear(pte_t *ptep)
{
	pte_val(*ptep) = _PAGE_INVALID; 
}

#define PTE_INIT(x) pte_clear(x)

/*
 * The following pte modification functions only work if
 * pte_present() is true. Undefined behaviour if not..
 */
extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & PAGE_MASK) | pgprot_val(newprot);
	return pte;
}

extern inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) |= _PAGE_RO;
	return pte;
}

extern inline pte_t pte_mkwrite(pte_t pte) 
{
	pte_val(pte) &= ~_PAGE_RO;
	return pte;
}

extern inline pte_t pte_mkclean(pte_t pte)
{
	/* The only user of pte_mkclean is the fork() code.
	   We must *not* clear the *physical* page dirty bit
	   just because fork() wants to clear the dirty bit in
	   *one* of the page's mappings.  So we just do nothing. */
	return pte;
}

extern inline pte_t pte_mkdirty(pte_t pte)
{
	/* We can't set the changed bit atomically. For now we
         * set (!) the page referenced bit. */
	asm volatile ("sske %0,%1" 
	              : : "d" (_PAGE_CHANGED|_PAGE_REFERENCED),
		          "a" (pte_val(pte)));

	pte_val(pte) &= ~_PAGE_MKCLEAR;
	return pte;
}

extern inline pte_t pte_mkold(pte_t pte)
{
	asm volatile ("rrbe 0,%0" : : "a" (pte_val(pte)) : "cc" );
	return pte;
}

extern inline pte_t pte_mkyoung(pte_t pte)
{
	/* To set the referenced bit we read the first word from the real
	 * page with a special instruction: load using real address (lura).
	 * Isn't S/390 a nice architecture ?! */
	asm volatile ("lura 0,%0" : : "a" (pte_val(pte) & PAGE_MASK) : "0" );
	return pte;
}

static inline int ptep_test_and_clear_young(pte_t *ptep)
{
	int ccode;

	asm volatile ("rrbe 0,%1\n\t"
		      "ipm  %0\n\t"
		      "srl  %0,28\n\t" 
                      : "=d" (ccode) : "a" (pte_val(*ptep)) : "cc" );
	return ccode & 2;
}

static inline int ptep_test_and_clear_dirty(pte_t *ptep)
{
	int skey;

	asm volatile ("iske %0,%1" : "=d" (skey) : "a" (*ptep));
	if ((skey & _PAGE_CHANGED) == 0)
		return 0;
	/* We can't clear the changed bit atomically. For now we
         * clear (!) the page referenced bit. */
	asm volatile ("sske %0,%1" 
	              : : "d" (0), "a" (*ptep));
	return 1;
}

static inline pte_t ptep_get_and_clear(pte_t *ptep)
{
	pte_t pte = *ptep;
	pte_clear(ptep);
	return pte;
}

static inline void ptep_set_wrprotect(pte_t *ptep)
{
	pte_t old_pte = *ptep;
	set_pte(ptep, pte_wrprotect(old_pte));
}

static inline void ptep_mkdirty(pte_t *ptep)
{
	pte_mkdirty(*ptep);
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern inline pte_t mk_pte_phys(unsigned long physpage, pgprot_t pgprot)
{
	pte_t __pte;
	pte_val(__pte) = physpage + pgprot_val(pgprot);
	return __pte;
}

#define mk_pte(pg, pgprot)                                                \
({                                                                        \
	struct page *__page = (pg);                                       \
	unsigned long __physpage = __pa((__page-mem_map) << PAGE_SHIFT);  \
	pte_t __pte = mk_pte_phys(__physpage, (pgprot));                  \
	                                                                  \
	if (__page != ZERO_PAGE(__physpage)) {                            \
		int __users = page_count(__page);                         \
		__users -= !!__page->buffers + !!__page->mapping;         \
	                                                                  \
		if (__users == 1)                                         \
			pte_val(__pte) |= _PAGE_MKCLEAR;                  \
        }                                                                 \
	                                                                  \
	__pte;                                                            \
})

#define pte_page(x) (mem_map+(unsigned long)((pte_val(x) >> PAGE_SHIFT)))

#define pmd_page(pmd) \
        ((unsigned long) __va(pmd_val(pmd) & PAGE_MASK))

/* to find an entry in a page-table-directory */
#define pgd_index(address) ((address >> PGDIR_SHIFT) & (PTRS_PER_PGD-1))
#define pgd_offset(mm, address) ((mm)->pgd+pgd_index(address))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* Find an entry in the second-level page table.. */
extern inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
        return (pmd_t *) dir;
}

/* Find an entry in the third-level page table.. */
#define pte_offset(pmd, address) \
        ((pte_t *) (pmd_page(*pmd) + ((address>>10) & ((PTRS_PER_PTE-1)<<2))))

/*
 * A page-table entry has some bits we have to treat in a special way.
 * Bits 0, 20 and bit 23 have to be zero, otherwise an specification
 * exception will occur instead of a page translation exception. The
 * specifiation exception has the bad habit not to store necessary
 * information in the lowcore.
 * Bit 21 and bit 22 are the page invalid bit and the page protection
 * bit. We set both to indicate a swapped page.
 * Bit 31 is used as the software page present bit. If a page is
 * swapped this obviously has to be zero.
 * This leaves the bits 1-19 and bits 24-30 to store type and offset.
 * We use the 7 bits from 24-30 for the type and the 19 bits from 1-19
 * for the offset.
 * 0|     offset      |0110|type |0
 * 00000000001111111111222222222233
 * 01234567890123456789012345678901
 */
extern inline pte_t mk_swap_pte(unsigned long type, unsigned long offset)
{
	pte_t pte;
	pte_val(pte) = (type << 1) | (offset << 12) | _PAGE_INVALID | _PAGE_RO;
	pte_val(pte) &= 0x7ffff6fe;  /* better to be paranoid */
	return pte;
}

#define SWP_TYPE(entry)		(((entry).val >> 1) & 0x3f)
#define SWP_OFFSET(entry)	(((entry).val >> 12) & 0x7FFFF )
#define SWP_ENTRY(type,offset)	((swp_entry_t) { pte_val(mk_swap_pte((type),(offset))) })

#define pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define swp_entry_to_pte(x)	((pte_t) { (x).val })

#endif /* !__ASSEMBLY__ */

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define PageSkip(page)          (0)
#define kern_addr_valid(addr)   (1)

/*
 * No page table caches to initialise
 */
#define pgtable_cache_init()	do { } while (0)

#endif /* _S390_PAGE_H */


#ifndef _ALPHA_PGTABLE_H
#define _ALPHA_PGTABLE_H

/*
 * This file contains the functions and defines necessary to modify and use
 * the Alpha page table tree.
 *
 * This hopefully works with any standard Alpha page-size, as defined
 * in <asm/page.h> (currently 8192).
 */
#include <linux/config.h>

#include <asm/system.h>
#include <asm/processor.h>	/* For TASK_SIZE */
#include <asm/mmu_context.h>
#include <asm/machvec.h>
#include <asm/spinlock.h>	/* For the task lock */


/* Caches aren't brain-dead on the Alpha. */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(mm, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr)		do { } while (0)
#define flush_page_to_ram(page)			do { } while (0)
#define flush_icache_range(start, end)		do { } while (0)

/*
 * Use a few helper functions to hide the ugly broken ASN
 * numbers on early Alphas (ev4 and ev45)
 */

#ifndef __EXTERN_INLINE
#define __EXTERN_INLINE extern inline
#define __MMU_EXTERN_INLINE
#endif

__EXTERN_INLINE void
ev4_flush_tlb_current(struct mm_struct *mm)
{
	tbiap();
}

__EXTERN_INLINE void
ev4_flush_tlb_other(struct mm_struct *mm)
{
}

__EXTERN_INLINE void
ev5_flush_tlb_current(struct mm_struct *mm)
{
	mm->context = 0;
	get_new_mmu_context(current, mm);
	reload_context(current);
}

__EXTERN_INLINE void
ev5_flush_tlb_other(struct mm_struct *mm)
{
	mm->context = 0;
}

/*
 * Flush just one page in the current TLB set.
 * We need to be very careful about the icache here, there
 * is no way to invalidate a specific icache page..
 */

__EXTERN_INLINE void
ev4_flush_tlb_current_page(struct mm_struct * mm,
			   struct vm_area_struct *vma,
			   unsigned long addr)
{
	tbi(2 + ((vma->vm_flags & VM_EXEC) != 0), addr);
}

__EXTERN_INLINE void
ev5_flush_tlb_current_page(struct mm_struct * mm,
			   struct vm_area_struct *vma,
			   unsigned long addr)
{
	if (vma->vm_flags & VM_EXEC)
		ev5_flush_tlb_current(mm);
	else
		tbi(2, addr);
}


#ifdef CONFIG_ALPHA_GENERIC
# define flush_tlb_current		alpha_mv.mv_flush_tlb_current
# define flush_tlb_other		alpha_mv.mv_flush_tlb_other
# define flush_tlb_current_page		alpha_mv.mv_flush_tlb_current_page
#else
# ifdef CONFIG_ALPHA_EV4
#  define flush_tlb_current		ev4_flush_tlb_current
#  define flush_tlb_other		ev4_flush_tlb_other
#  define flush_tlb_current_page	ev4_flush_tlb_current_page
# else
#  define flush_tlb_current		ev5_flush_tlb_current
#  define flush_tlb_other		ev5_flush_tlb_other
#  define flush_tlb_current_page	ev5_flush_tlb_current_page
# endif
#endif

#ifdef __MMU_EXTERN_INLINE
#undef __EXTERN_INLINE
#undef __MMU_EXTERN_INLINE
#endif

/*
 * Flush current user mapping.
 */
static inline void flush_tlb(void)
{
	flush_tlb_current(current->mm);
}

#ifndef __SMP__
/*
 * Flush everything (kernel mapping may also have
 * changed due to vmalloc/vfree)
 */
static inline void flush_tlb_all(void)
{
	tbia();
}

/*
 * Flush a specified user mapping
 */
static inline void flush_tlb_mm(struct mm_struct *mm)
{
	if (mm != current->mm)
		flush_tlb_other(mm);
	else
		flush_tlb_current(mm);
}

/*
 * Page-granular tlb flush.
 *
 * do a tbisd (type = 2) normally, and a tbis (type = 3)
 * if it is an executable mapping.  We want to avoid the
 * itlb flush, because that potentially also does a
 * icache flush.
 */
static inline void flush_tlb_page(struct vm_area_struct *vma,
	unsigned long addr)
{
	struct mm_struct * mm = vma->vm_mm;

	if (mm != current->mm)
		flush_tlb_other(mm);
	else
		flush_tlb_current_page(mm, vma, addr);
}

/*
 * Flush a specified range of user mapping:  on the
 * Alpha we flush the whole user tlb.
 */
static inline void flush_tlb_range(struct mm_struct *mm,
	unsigned long start, unsigned long end)
{
	flush_tlb_mm(mm);
}

#else /* __SMP__ */

/* ipi_msg_flush_tb is owned by the holder of the global kernel lock. */
struct ipi_msg_flush_tb_struct {
	volatile unsigned int flush_tb_mask;
	union {
		struct mm_struct *	flush_mm;
		struct vm_area_struct *	flush_vma;
	} p;
	unsigned long flush_addr;
	unsigned long flush_end;
};

extern struct ipi_msg_flush_tb_struct ipi_msg_flush_tb;

extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct *);
extern void flush_tlb_page(struct vm_area_struct *, unsigned long);
extern void flush_tlb_range(struct mm_struct *, unsigned long, unsigned long);

#endif /* __SMP__ */

/* Certain architectures need to do special things when PTEs
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT-3))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	(PAGE_SHIFT + 2*(PAGE_SHIFT-3))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * Entries per page directory level:  the Alpha is three-level, with
 * all levels having a one-page page table.
 *
 * The PGD is special:  the last entry is reserved for self-mapping.
 */
#define PTRS_PER_PTE	(1UL << (PAGE_SHIFT-3))
#define PTRS_PER_PMD	(1UL << (PAGE_SHIFT-3))
#define PTRS_PER_PGD	((1UL << (PAGE_SHIFT-3))-1)
#define USER_PTRS_PER_PGD	(TASK_SIZE / PGDIR_SIZE)

/* Number of pointers that fit on a page:  this will go away. */
#define PTRS_PER_PAGE	(1UL << (PAGE_SHIFT-3))

#define VMALLOC_START		0xFFFFFE0000000000
#define VMALLOC_VMADDR(x)	((unsigned long)(x))
#define VMALLOC_END		(~0UL)

/*
 * OSF/1 PAL-code-imposed page table bits
 */
#define _PAGE_VALID	0x0001
#define _PAGE_FOR	0x0002	/* used for page protection (fault on read) */
#define _PAGE_FOW	0x0004	/* used for page protection (fault on write) */
#define _PAGE_FOE	0x0008	/* used for page protection (fault on exec) */
#define _PAGE_ASM	0x0010
#define _PAGE_KRE	0x0100	/* xxx - see below on the "accessed" bit */
#define _PAGE_URE	0x0200	/* xxx */
#define _PAGE_KWE	0x1000	/* used to do the dirty bit in software */
#define _PAGE_UWE	0x2000	/* used to do the dirty bit in software */

/* .. and these are ours ... */
#define _PAGE_DIRTY	0x20000
#define _PAGE_ACCESSED	0x40000

/*
 * NOTE! The "accessed" bit isn't necessarily exact:  it can be kept exactly
 * by software (use the KRE/URE/KWE/UWE bits appropriately), but I'll fake it.
 * Under Linux/AXP, the "accessed" bit just means "read", and I'll just use
 * the KRE/URE bits to watch for it. That way we don't need to overload the
 * KWE/UWE bits with both handling dirty and accessed.
 *
 * Note that the kernel uses the accessed bit just to check whether to page
 * out a page or not, so it doesn't have to be exact anyway.
 */

#define __DIRTY_BITS	(_PAGE_DIRTY | _PAGE_KWE | _PAGE_UWE)
#define __ACCESS_BITS	(_PAGE_ACCESSED | _PAGE_KRE | _PAGE_URE)

#define _PFN_MASK	0xFFFFFFFF00000000

#define _PAGE_TABLE	(_PAGE_VALID | __DIRTY_BITS | __ACCESS_BITS)
#define _PAGE_CHG_MASK	(_PFN_MASK | __DIRTY_BITS | __ACCESS_BITS)

/*
 * All the normal masks have the "page accessed" bits on, as any time they are used,
 * the page is accessed. They are cleared only by the page-out routines
 */
#define PAGE_NONE	__pgprot(_PAGE_VALID | __ACCESS_BITS | _PAGE_FOR | _PAGE_FOW | _PAGE_FOE)
#define PAGE_SHARED	__pgprot(_PAGE_VALID | __ACCESS_BITS)
#define PAGE_COPY	__pgprot(_PAGE_VALID | __ACCESS_BITS | _PAGE_FOW)
#define PAGE_READONLY	__pgprot(_PAGE_VALID | __ACCESS_BITS | _PAGE_FOW)
#define PAGE_KERNEL	__pgprot(_PAGE_VALID | _PAGE_ASM | _PAGE_KRE | _PAGE_KWE)

#define _PAGE_NORMAL(x) __pgprot(_PAGE_VALID | __ACCESS_BITS | (x))

#define _PAGE_P(x) _PAGE_NORMAL((x) | (((x) & _PAGE_FOW)?0:_PAGE_FOW))
#define _PAGE_S(x) _PAGE_NORMAL(x)

/*
 * The hardware can handle write-only mappings, but as the Alpha
 * architecture does byte-wide writes with a read-modify-write
 * sequence, it's not practical to have write-without-read privs.
 * Thus the "-w- -> rw-" and "-wx -> rwx" mapping here (and in
 * arch/alpha/mm/fault.c)
 */
	/* xwr */
#define __P000	_PAGE_P(_PAGE_FOE | _PAGE_FOW | _PAGE_FOR)
#define __P001	_PAGE_P(_PAGE_FOE | _PAGE_FOW)
#define __P010	_PAGE_P(_PAGE_FOE)
#define __P011	_PAGE_P(_PAGE_FOE)
#define __P100	_PAGE_P(_PAGE_FOW | _PAGE_FOR)
#define __P101	_PAGE_P(_PAGE_FOW)
#define __P110	_PAGE_P(0)
#define __P111	_PAGE_P(0)

#define __S000	_PAGE_S(_PAGE_FOE | _PAGE_FOW | _PAGE_FOR)
#define __S001	_PAGE_S(_PAGE_FOE | _PAGE_FOW)
#define __S010	_PAGE_S(_PAGE_FOE)
#define __S011	_PAGE_S(_PAGE_FOE)
#define __S100	_PAGE_S(_PAGE_FOW | _PAGE_FOR)
#define __S101	_PAGE_S(_PAGE_FOW)
#define __S110	_PAGE_S(0)
#define __S111	_PAGE_S(0)

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero:  used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern pmd_t * __bad_pagetable(void);

extern unsigned long __zero_page(void);

#define BAD_PAGETABLE	__bad_pagetable()
#define BAD_PAGE	__bad_page()
#define ZERO_PAGE	(PAGE_OFFSET+0x30A000)

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR			(8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK			(~(sizeof(void*)-1))

/* sizeof(void*)==1<<SIZEOF_PTR_LOG2 */
#define SIZEOF_PTR_LOG2			3

/* to find an entry in a page-table */
#define PAGE_PTR(address)		\
  ((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

/*
 * On certain platforms whose physical address space can overlap KSEG,
 * namely EV6 and above, we must re-twiddle the physaddr to restore the
 * correct high-order bits.
 */

#if defined(CONFIG_ALPHA_GENERIC) && defined(USE_48_BIT_KSEG)
#error "EV6-only feature in a generic kernel"
#endif
#if defined(CONFIG_ALPHA_GENERIC) || \
    (defined(CONFIG_ALPHA_EV6) && !defined(USE_48_BIT_KSEG))
#define PHYS_TWIDDLE(phys) \
  ((((phys) & 0xc0000000000UL) == 0x40000000000UL) \
  ? ((phys) ^= 0xc0000000000UL) : (phys))
#else
#define PHYS_TWIDDLE(phys) (phys)
#endif

/*
 * Conversion functions:  convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern inline pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = ((page-PAGE_OFFSET) << (32-PAGE_SHIFT)) | pgprot_val(pgprot); return pte; }

extern inline pte_t mk_pte_phys(unsigned long physpage, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = (PHYS_TWIDDLE(physpage) << (32-PAGE_SHIFT)) | pgprot_val(pgprot); return pte; }

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

extern inline void pmd_set(pmd_t * pmdp, pte_t * ptep)
{ pmd_val(*pmdp) = _PAGE_TABLE | ((((unsigned long) ptep) - PAGE_OFFSET) << (32-PAGE_SHIFT)); }

extern inline void pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{ pgd_val(*pgdp) = _PAGE_TABLE | ((((unsigned long) pmdp) - PAGE_OFFSET) << (32-PAGE_SHIFT)); }

extern inline unsigned long pte_page(pte_t pte)
{ return PAGE_OFFSET + ((pte_val(pte) & _PFN_MASK) >> (32-PAGE_SHIFT)); }

extern inline unsigned long pmd_page(pmd_t pmd)
{ return PAGE_OFFSET + ((pmd_val(pmd) & _PFN_MASK) >> (32-PAGE_SHIFT)); }

extern inline unsigned long pgd_page(pgd_t pgd)
{ return PAGE_OFFSET + ((pgd_val(pgd) & _PFN_MASK) >> (32-PAGE_SHIFT)); }

extern inline int pte_none(pte_t pte)		{ return !pte_val(pte); }
extern inline int pte_present(pte_t pte)	{ return pte_val(pte) & _PAGE_VALID; }
extern inline void pte_clear(pte_t *ptep)	{ pte_val(*ptep) = 0; }

extern inline int pmd_none(pmd_t pmd)		{ return !pmd_val(pmd); }
extern inline int pmd_bad(pmd_t pmd)		{ return (pmd_val(pmd) & ~_PFN_MASK) != _PAGE_TABLE; }
extern inline int pmd_present(pmd_t pmd)	{ return pmd_val(pmd) & _PAGE_VALID; }
extern inline void pmd_clear(pmd_t * pmdp)	{ pmd_val(*pmdp) = 0; }

extern inline int pgd_none(pgd_t pgd)		{ return !pgd_val(pgd); }
extern inline int pgd_bad(pgd_t pgd)		{ return (pgd_val(pgd) & ~_PFN_MASK) != _PAGE_TABLE; }
extern inline int pgd_present(pgd_t pgd)	{ return pgd_val(pgd) & _PAGE_VALID; }
extern inline void pgd_clear(pgd_t * pgdp)	{ pgd_val(*pgdp) = 0; }

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)		{ return !(pte_val(pte) & _PAGE_FOR); }
extern inline int pte_write(pte_t pte)		{ return !(pte_val(pte) & _PAGE_FOW); }
extern inline int pte_exec(pte_t pte)		{ return !(pte_val(pte) & _PAGE_FOE); }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }

extern inline pte_t pte_wrprotect(pte_t pte)	{ pte_val(pte) |= _PAGE_FOW; return pte; }
extern inline pte_t pte_rdprotect(pte_t pte)	{ pte_val(pte) |= _PAGE_FOR; return pte; }
extern inline pte_t pte_exprotect(pte_t pte)	{ pte_val(pte) |= _PAGE_FOE; return pte; }
extern inline pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~(__DIRTY_BITS); return pte; }
extern inline pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~(__ACCESS_BITS); return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)	{ pte_val(pte) &= ~_PAGE_FOW; return pte; }
extern inline pte_t pte_mkread(pte_t pte)	{ pte_val(pte) &= ~_PAGE_FOR; return pte; }
extern inline pte_t pte_mkexec(pte_t pte)	{ pte_val(pte) &= ~_PAGE_FOE; return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)	{ pte_val(pte) |= __DIRTY_BITS; return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)	{ pte_val(pte) |= __ACCESS_BITS; return pte; }

/* 
 * To set the page-dir. Note the self-mapping in the last entry
 *
 * Also note that if we update the current process ptbr, we need to
 * update the PAL-cached ptbr value as well.. There doesn't seem to
 * be any "wrptbr" PAL-insn, but we can do a dummy swpctx to ourself
 * instead.
 */
extern inline void SET_PAGE_DIR(struct task_struct * tsk, pgd_t * pgdir)
{
	pgd_val(pgdir[PTRS_PER_PGD]) = pte_val(mk_pte((unsigned long) pgdir, PAGE_KERNEL));
	tsk->tss.ptbr = ((unsigned long) pgdir - PAGE_OFFSET) >> PAGE_SHIFT;
	if (tsk == current)
		reload_context(tsk);
}

#define PAGE_DIR_OFFSET(tsk,address) pgd_offset((tsk),(address))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* to find an entry in a page-table-directory. */
extern inline pgd_t * pgd_offset(struct mm_struct * mm, unsigned long address)
{
	return mm->pgd + ((address >> PGDIR_SHIFT) & (PTRS_PER_PAGE - 1));
}

/* Find an entry in the second-level page table.. */
extern inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) pgd_page(*dir) + ((address >> PMD_SHIFT) & (PTRS_PER_PAGE - 1));
}

/* Find an entry in the third-level page table.. */
extern inline pte_t * pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) pmd_page(*dir) + ((address >> PAGE_SHIFT) & (PTRS_PER_PAGE - 1));
}

/*      
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */
#ifndef __SMP__
extern struct pgtable_cache_struct {
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
} quicklists;
#else
#include <asm/smp.h>
#define quicklists cpu_data[smp_processor_id()]
#endif
#define pgd_quicklist (quicklists.pgd_cache)
#define pmd_quicklist ((unsigned long *)0)
#define pte_quicklist (quicklists.pte_cache)
#define pgtable_cache_size (quicklists.pgtable_cache_sz)

extern __inline__ pgd_t *get_pgd_slow(void)
{
	pgd_t *ret = (pgd_t *)__get_free_page(GFP_KERNEL), *init;
	
	if (ret) {
		init = pgd_offset(&init_mm, 0);
		memset (ret, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		memcpy (ret + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
			(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}
	return ret;
}

extern __inline__ pgd_t *get_pgd_fast(void)
{
	unsigned long *ret;

	if((ret = pgd_quicklist) != NULL) {
		pgd_quicklist = (unsigned long *)(*ret);
		ret[0] = ret[1];
		pgtable_cache_size--;
	} else
		ret = (unsigned long *)get_pgd_slow();
	return (pgd_t *)ret;
}

extern __inline__ void free_pgd_fast(pgd_t *pgd)
{
	*(unsigned long *)pgd = (unsigned long) pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	pgtable_cache_size++;
}

extern __inline__ void free_pgd_slow(pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

extern pmd_t *get_pmd_slow(pgd_t *pgd, unsigned long address_premasked);

extern __inline__ pmd_t *get_pmd_fast(void)
{
	unsigned long *ret;

	if((ret = (unsigned long *)pte_quicklist) != NULL) {
		pte_quicklist = (unsigned long *)(*ret);
		ret[0] = ret[1];
		pgtable_cache_size--;
	}
	return (pmd_t *)ret;
}

extern __inline__ void free_pmd_fast(pmd_t *pmd)
{
	*(unsigned long *)pmd = (unsigned long) pte_quicklist;
	pte_quicklist = (unsigned long *) pmd;
	pgtable_cache_size++;
}

extern __inline__ void free_pmd_slow(pmd_t *pmd)
{
	free_page((unsigned long)pmd);
}

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted);

extern __inline__ pte_t *get_pte_fast(void)
{
	unsigned long *ret;

	if((ret = (unsigned long *)pte_quicklist) != NULL) {
		pte_quicklist = (unsigned long *)(*ret);
		ret[0] = ret[1];
		pgtable_cache_size--;
	}
	return (pte_t *)ret;
}

extern __inline__ void free_pte_fast(pte_t *pte)
{
	*(unsigned long *)pte = (unsigned long) pte_quicklist;
	pte_quicklist = (unsigned long *) pte;
	pgtable_cache_size++;
}

extern __inline__ void free_pte_slow(pte_t *pte)
{
	free_page((unsigned long)pte);
}

extern void __bad_pte(pmd_t *pmd);
extern void __bad_pmd(pgd_t *pgd);

#define pte_free_kernel(pte)	free_pte_fast(pte)
#define pte_free(pte)		free_pte_fast(pte)
#define pmd_free_kernel(pmd)	free_pmd_fast(pmd)
#define pmd_free(pmd)		free_pmd_fast(pmd)
#define pgd_free(pgd)		free_pgd_fast(pgd)
#define pgd_alloc()		get_pgd_fast()

extern inline pte_t * pte_alloc(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t *page = get_pte_fast();
		
		if (!page)
			return get_pte_slow(pmd, address);
		pmd_set(pmd, page);
		return page + address;
	}
	if (pmd_bad(*pmd)) {
		__bad_pte(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern inline pmd_t * pmd_alloc(pgd_t *pgd, unsigned long address)
{
	address = (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
	if (pgd_none(*pgd)) {
		pmd_t *page = get_pmd_fast();
		
		if (!page)
			return get_pmd_slow(pgd, address);
		pgd_set(pgd, page);
		return page + address;
	}
	if (pgd_bad(*pgd)) {
		__bad_pmd(pgd);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

#define pte_alloc_kernel	pte_alloc
#define pmd_alloc_kernel	pmd_alloc

extern int do_check_pgt_cache(int, int);

extern inline void set_pgdir(unsigned long address, pgd_t entry)
{
	struct task_struct * p;
	pgd_t *pgd;
        
	read_lock(&tasklist_lock);
	for_each_task(p) {
		if (!p->mm)
			continue;
		*pgd_offset(p->mm,address) = entry;
	}
	read_unlock(&tasklist_lock);
	for (pgd = (pgd_t *)pgd_quicklist; pgd; pgd = (pgd_t *)*(unsigned long *)pgd)
		pgd[(address >> PGDIR_SHIFT) & (PTRS_PER_PAGE - 1)] = entry;
}

extern pgd_t swapper_pg_dir[1024];

/*
 * The Alpha doesn't have any external MMU info:  the kernel page
 * tables contain all the necessary information.
 */
extern inline void update_mmu_cache(struct vm_area_struct * vma,
	unsigned long address, pte_t pte)
{
}

/*
 * Non-present pages:  high 24 bits are offset, next 8 bits type,
 * low 32 bits zero.
 */
extern inline pte_t mk_swap_pte(unsigned long type, unsigned long offset)
{ pte_t pte; pte_val(pte) = (type << 32) | (offset << 40); return pte; }

#define SWP_TYPE(entry) (((entry) >> 32) & 0xff)
#define SWP_OFFSET(entry) ((entry) >> 40)
#define SWP_ENTRY(type,offset) pte_val(mk_swap_pte((type),(offset)))

#define module_map	vmalloc
#define module_unmap	vfree

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define PageSkip(page)		(0)

#endif /* _ALPHA_PGTABLE_H */

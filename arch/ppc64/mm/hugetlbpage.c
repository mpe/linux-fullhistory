/*
 * PPC64 (POWER4) Huge TLB Page Support for Kernel.
 *
 * Copyright (C) 2003 David Gibson, IBM Corporation.
 *
 * Based on the IA-32 version:
 * Copyright (C) 2002, Rohit Seth <rohit.seth@intel.com>
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/sysctl.h>
#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/machdep.h>
#include <asm/cputable.h>
#include <asm/tlb.h>

#include <linux/sysctl.h>

#define	HUGEPGDIR_SHIFT		(HPAGE_SHIFT + PAGE_SHIFT - 3)
#define HUGEPGDIR_SIZE		(1UL << HUGEPGDIR_SHIFT)
#define HUGEPGDIR_MASK		(~(HUGEPGDIR_SIZE-1))

#define HUGEPTE_INDEX_SIZE	9
#define HUGEPGD_INDEX_SIZE	10

#define PTRS_PER_HUGEPTE	(1 << HUGEPTE_INDEX_SIZE)
#define PTRS_PER_HUGEPGD	(1 << HUGEPGD_INDEX_SIZE)

static inline int hugepgd_index(unsigned long addr)
{
	return (addr & ~REGION_MASK) >> HUGEPGDIR_SHIFT;
}

static pgd_t *hugepgd_offset(struct mm_struct *mm, unsigned long addr)
{
	int index;

	if (! mm->context.huge_pgdir)
		return NULL;


	index = hugepgd_index(addr);
	BUG_ON(index >= PTRS_PER_HUGEPGD);
	return mm->context.huge_pgdir + index;
}

static inline pte_t *hugepte_offset(pgd_t *dir, unsigned long addr)
{
	int index;

	if (pgd_none(*dir))
		return NULL;

	index = (addr >> HPAGE_SHIFT) % PTRS_PER_HUGEPTE;
	return (pte_t *)pgd_page(*dir) + index;
}

static pgd_t *hugepgd_alloc(struct mm_struct *mm, unsigned long addr)
{
	BUG_ON(! in_hugepage_area(mm->context, addr));

	if (! mm->context.huge_pgdir) {
		pgd_t *new;
		spin_unlock(&mm->page_table_lock);
		/* Don't use pgd_alloc(), because we want __GFP_REPEAT */
		new = kmem_cache_alloc(zero_cache, GFP_KERNEL | __GFP_REPEAT);
		BUG_ON(memcmp(new, empty_zero_page, PAGE_SIZE));
		spin_lock(&mm->page_table_lock);

		/*
		 * Because we dropped the lock, we should re-check the
		 * entry, as somebody else could have populated it..
		 */
		if (mm->context.huge_pgdir)
			pgd_free(new);
		else
			mm->context.huge_pgdir = new;
	}
	return hugepgd_offset(mm, addr);
}

static pte_t *hugepte_alloc(struct mm_struct *mm, pgd_t *dir,
			    unsigned long addr)
{
	if (! pgd_present(*dir)) {
		pte_t *new;

		spin_unlock(&mm->page_table_lock);
		new = kmem_cache_alloc(zero_cache, GFP_KERNEL | __GFP_REPEAT);
		BUG_ON(memcmp(new, empty_zero_page, PAGE_SIZE));
		spin_lock(&mm->page_table_lock);
		/*
		 * Because we dropped the lock, we should re-check the
		 * entry, as somebody else could have populated it..
		 */
		if (pgd_present(*dir)) {
			if (new)
				kmem_cache_free(zero_cache, new);
		} else {
			struct page *ptepage;

			if (! new)
				return NULL;
			ptepage = virt_to_page(new);
			ptepage->mapping = (void *) mm;
			ptepage->index = addr & HUGEPGDIR_MASK;
			pgd_populate(mm, dir, new);
		}
	}

	return hugepte_offset(dir, addr);
}

static pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;

	BUG_ON(! in_hugepage_area(mm->context, addr));

	pgd = hugepgd_offset(mm, addr);
	if (! pgd)
		return NULL;

	return hugepte_offset(pgd, addr);
}

static pte_t *huge_pte_alloc(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;

	BUG_ON(! in_hugepage_area(mm->context, addr));

	pgd = hugepgd_alloc(mm, addr);
	if (! pgd)
		return NULL;

	return hugepte_alloc(mm, pgd, addr);
}

static void set_huge_pte(struct mm_struct *mm, struct vm_area_struct *vma,
			 struct page *page, pte_t *ptep, int write_access)
{
	pte_t entry;

	mm->rss += (HPAGE_SIZE / PAGE_SIZE);
	if (write_access) {
		entry =
		    pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	} else {
		entry = pte_wrprotect(mk_pte(page, vma->vm_page_prot));
	}
	entry = pte_mkyoung(entry);
	entry = pte_mkhuge(entry);

	set_pte(ptep, entry);
}

/*
 * This function checks for proper alignment of input addr and len parameters.
 */
int is_aligned_hugepage_range(unsigned long addr, unsigned long len)
{
	if (len & ~HPAGE_MASK)
		return -EINVAL;
	if (addr & ~HPAGE_MASK)
		return -EINVAL;
	if (! (within_hugepage_low_range(addr, len)
	       || within_hugepage_high_range(addr, len)) )
		return -EINVAL;
	return 0;
}

static void flush_segments(void *parm)
{
	u16 segs = (unsigned long) parm;
	unsigned long i;

	asm volatile("isync" : : : "memory");

	for (i = 0; i < 16; i++) {
		if (! (segs & (1U << i)))
			continue;
		asm volatile("slbie %0" : : "r" (i << SID_SHIFT));
	}

	asm volatile("isync" : : : "memory");
}

static int prepare_low_seg_for_htlb(struct mm_struct *mm, unsigned long seg)
{
	unsigned long start = seg << SID_SHIFT;
	unsigned long end = (seg+1) << SID_SHIFT;
	struct vm_area_struct *vma;
	unsigned long addr;
	struct mmu_gather *tlb;

	BUG_ON(seg >= 16);

	/* Check no VMAs are in the region */
	vma = find_vma(mm, start);
	if (vma && (vma->vm_start < end))
		return -EBUSY;

	/* Clean up any leftover PTE pages in the region */
	spin_lock(&mm->page_table_lock);
	tlb = tlb_gather_mmu(mm, 0);
	for (addr = start; addr < end; addr += PMD_SIZE) {
		pgd_t *pgd = pgd_offset(mm, addr);
		pmd_t *pmd;
		struct page *page;
		pte_t *pte;
		int i;

		if (pgd_none(*pgd))
			continue;
		pmd = pmd_offset(pgd, addr);
		if (!pmd || pmd_none(*pmd))
			continue;
		if (pmd_bad(*pmd)) {
			pmd_ERROR(*pmd);
			pmd_clear(pmd);
			continue;
		}
		pte = (pte_t *)pmd_page_kernel(*pmd);
		/* No VMAs, so there should be no PTEs, check just in case. */
		for (i = 0; i < PTRS_PER_PTE; i++) {
			BUG_ON(!pte_none(*pte));
			pte++;
		}
		page = pmd_page(*pmd);
		pmd_clear(pmd);
		mm->nr_ptes--;
		dec_page_state(nr_page_table_pages);
		pte_free_tlb(tlb, page);
	}
	tlb_finish_mmu(tlb, start, end);
	spin_unlock(&mm->page_table_lock);

	return 0;
}

static int open_low_hpage_segs(struct mm_struct *mm, u16 newsegs)
{
	unsigned long i;

	newsegs &= ~(mm->context.htlb_segs);
	if (! newsegs)
		return 0; /* The segments we want are already open */

	for (i = 0; i < 16; i++)
		if ((1 << i) & newsegs)
			if (prepare_low_seg_for_htlb(mm, i) != 0)
				return -EBUSY;

	mm->context.htlb_segs |= newsegs;

	/* update the paca copy of the context struct */
	get_paca()->context = mm->context;

	/* the context change must make it to memory before the flush,
	 * so that further SLB misses do the right thing. */
	mb();
	on_each_cpu(flush_segments, (void *)(unsigned long)newsegs, 0, 1);

	return 0;
}

int prepare_hugepage_range(unsigned long addr, unsigned long len)
{
	if (within_hugepage_high_range(addr, len))
		return 0;
	else if ((addr < 0x100000000UL) && ((addr+len) < 0x100000000UL)) {
		int err;
		/* Yes, we need both tests, in case addr+len overflows
		 * 64-bit arithmetic */
		err = open_low_hpage_segs(current->mm,
					  LOW_ESID_MASK(addr, len));
		if (err)
			printk(KERN_DEBUG "prepare_hugepage_range(%lx, %lx)"
			       " failed (segs: 0x%04hx)\n", addr, len,
			       LOW_ESID_MASK(addr, len));
		return err;
	}

	return -EINVAL;
}

int copy_hugetlb_page_range(struct mm_struct *dst, struct mm_struct *src,
			struct vm_area_struct *vma)
{
	pte_t *src_pte, *dst_pte, entry;
	struct page *ptepage;
	unsigned long addr = vma->vm_start;
	unsigned long end = vma->vm_end;
	int err = -ENOMEM;

	while (addr < end) {
		dst_pte = huge_pte_alloc(dst, addr);
		if (!dst_pte)
			goto out;

		src_pte = huge_pte_offset(src, addr);
		entry = *src_pte;
		
		ptepage = pte_page(entry);
		get_page(ptepage);
		dst->rss += (HPAGE_SIZE / PAGE_SIZE);
		set_pte(dst_pte, entry);

		addr += HPAGE_SIZE;
	}

	err = 0;
 out:
	return err;
}

int
follow_hugetlb_page(struct mm_struct *mm, struct vm_area_struct *vma,
		    struct page **pages, struct vm_area_struct **vmas,
		    unsigned long *position, int *length, int i)
{
	unsigned long vpfn, vaddr = *position;
	int remainder = *length;

	WARN_ON(!is_vm_hugetlb_page(vma));

	vpfn = vaddr/PAGE_SIZE;
	while (vaddr < vma->vm_end && remainder) {
		if (pages) {
			pte_t *pte;
			struct page *page;

			pte = huge_pte_offset(mm, vaddr);

			/* hugetlb should be locked, and hence, prefaulted */
			WARN_ON(!pte || pte_none(*pte));

			page = &pte_page(*pte)[vpfn % (HPAGE_SIZE/PAGE_SIZE)];

			WARN_ON(!PageCompound(page));

			get_page(page);
			pages[i] = page;
		}

		if (vmas)
			vmas[i] = vma;

		vaddr += PAGE_SIZE;
		++vpfn;
		--remainder;
		++i;
	}

	*length = remainder;
	*position = vaddr;

	return i;
}

struct page *
follow_huge_addr(struct mm_struct *mm, unsigned long address, int write)
{
	pte_t *ptep;
	struct page *page;

	if (! in_hugepage_area(mm->context, address))
		return ERR_PTR(-EINVAL);

	ptep = huge_pte_offset(mm, address);
	page = pte_page(*ptep);
	if (page)
		page += (address % HPAGE_SIZE) / PAGE_SIZE;

	return page;
}

int pmd_huge(pmd_t pmd)
{
	return 0;
}

struct page *
follow_huge_pmd(struct mm_struct *mm, unsigned long address,
		pmd_t *pmd, int write)
{
	BUG();
	return NULL;
}

void unmap_hugepage_range(struct vm_area_struct *vma,
			  unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long addr;
	pte_t *ptep;
	struct page *page;

	WARN_ON(!is_vm_hugetlb_page(vma));
	BUG_ON((start % HPAGE_SIZE) != 0);
	BUG_ON((end % HPAGE_SIZE) != 0);

	for (addr = start; addr < end; addr += HPAGE_SIZE) {
		pte_t pte;

		ptep = huge_pte_offset(mm, addr);
		if (!ptep || pte_none(*ptep))
			continue;

		pte = *ptep;
		page = pte_page(pte);
		pte_clear(ptep);

		put_page(page);
	}
	mm->rss -= (end - start) >> PAGE_SHIFT;
	flush_tlb_pending();
}

void hugetlb_free_pgtables(struct mmu_gather *tlb, struct vm_area_struct *prev,
			   unsigned long start, unsigned long end)
{
	/* Because the huge pgtables are only 2 level, they can take
	 * at most around 4M, much less than one hugepage which the
	 * process is presumably entitled to use.  So we don't bother
	 * freeing up the pagetables on unmap, and wait until
	 * destroy_context() to clean up the lot. */
}

int hugetlb_prefault(struct address_space *mapping, struct vm_area_struct *vma)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr;
	int ret = 0;

	WARN_ON(!is_vm_hugetlb_page(vma));
	BUG_ON((vma->vm_start % HPAGE_SIZE) != 0);
	BUG_ON((vma->vm_end % HPAGE_SIZE) != 0);

	spin_lock(&mm->page_table_lock);
	for (addr = vma->vm_start; addr < vma->vm_end; addr += HPAGE_SIZE) {
		unsigned long idx;
		pte_t *pte = huge_pte_alloc(mm, addr);
		struct page *page;

		if (!pte) {
			ret = -ENOMEM;
			goto out;
		}
		if (! pte_none(*pte))
			continue;

		idx = ((addr - vma->vm_start) >> HPAGE_SHIFT)
			+ (vma->vm_pgoff >> (HPAGE_SHIFT - PAGE_SHIFT));
		page = find_get_page(mapping, idx);
		if (!page) {
			/* charge the fs quota first */
			if (hugetlb_get_quota(mapping)) {
				ret = -ENOMEM;
				goto out;
			}
			page = alloc_huge_page();
			if (!page) {
				hugetlb_put_quota(mapping);
				ret = -ENOMEM;
				goto out;
			}
			ret = add_to_page_cache(page, mapping, idx, GFP_ATOMIC);
			if (! ret) {
				unlock_page(page);
			} else {
				hugetlb_put_quota(mapping);
				free_huge_page(page);
				goto out;
			}
		}
		set_huge_pte(mm, vma, page, pte, vma->vm_flags & VM_WRITE);
	}
out:
	spin_unlock(&mm->page_table_lock);
	return ret;
}

/* Because we have an exclusive hugepage region which lies within the
 * normal user address space, we have to take special measures to make
 * non-huge mmap()s evade the hugepage reserved regions. */
unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr,
				     unsigned long len, unsigned long pgoff,
				     unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long start_addr;

	if (len > TASK_SIZE)
		return -ENOMEM;

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (((TASK_SIZE - len) >= addr)
		    && (!vma || (addr+len) <= vma->vm_start)
		    && !is_hugepage_only_range(addr,len))
			return addr;
	}
	start_addr = addr = mm->free_area_cache;

full_search:
	vma = find_vma(mm, addr);
	while (TASK_SIZE - len >= addr) {
		BUG_ON(vma && (addr >= vma->vm_end));

		if (touches_hugepage_low_range(addr, len)) {
			addr = ALIGN(addr+1, 1<<SID_SHIFT);
			vma = find_vma(mm, addr);
			continue;
		}
		if (touches_hugepage_high_range(addr, len)) {
			addr = TASK_HPAGE_END;
			vma = find_vma(mm, addr);
			continue;
		}
		if (!vma || addr + len <= vma->vm_start) {
			/*
			 * Remember the place where we stopped the search:
			 */
			mm->free_area_cache = addr + len;
			return addr;
		}
		addr = vma->vm_end;
		vma = vma->vm_next;
	}

	/* Make sure we didn't miss any holes */
	if (start_addr != TASK_UNMAPPED_BASE) {
		start_addr = addr = TASK_UNMAPPED_BASE;
		goto full_search;
	}
	return -ENOMEM;
}

/*
 * This mmap-allocator allocates new areas top-down from below the
 * stack's low limit (the base):
 *
 * Because we have an exclusive hugepage region which lies within the
 * normal user address space, we have to take special measures to make
 * non-huge mmap()s evade the hugepage reserved regions.
 */
unsigned long
arch_get_unmapped_area_topdown(struct file *filp, const unsigned long addr0,
			  const unsigned long len, const unsigned long pgoff,
			  const unsigned long flags)
{
	struct vm_area_struct *vma, *prev_vma;
	struct mm_struct *mm = current->mm;
	unsigned long base = mm->mmap_base, addr = addr0;
	int first_time = 1;

	/* requested length too big for entire address space */
	if (len > TASK_SIZE)
		return -ENOMEM;

	/* dont allow allocations above current base */
	if (mm->free_area_cache > base)
		mm->free_area_cache = base;

	/* requesting a specific address */
	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
				(!vma || addr + len <= vma->vm_start)
				&& !is_hugepage_only_range(addr,len))
			return addr;
	}

try_again:
	/* make sure it can fit in the remaining address space */
	if (mm->free_area_cache < len)
		goto fail;

	/* either no address requested or cant fit in requested address hole */
	addr = (mm->free_area_cache - len) & PAGE_MASK;
	do {
hugepage_recheck:
		if (touches_hugepage_low_range(addr, len)) {
			addr = (addr & ((~0) << SID_SHIFT)) - len;
			goto hugepage_recheck;
		} else if (touches_hugepage_high_range(addr, len)) {
			addr = TASK_HPAGE_BASE - len;
		}

		/*
		 * Lookup failure means no vma is above this address,
		 * i.e. return with success:
		 */
 	 	if (!(vma = find_vma_prev(mm, addr, &prev_vma)))
			return addr;

		/*
		 * new region fits between prev_vma->vm_end and
		 * vma->vm_start, use it:
		 */
		if (addr+len <= vma->vm_start &&
				(!prev_vma || (addr >= prev_vma->vm_end)))
			/* remember the address as a hint for next time */
			return (mm->free_area_cache = addr);
		else
			/* pull free_area_cache down to the first hole */
			if (mm->free_area_cache == vma->vm_end)
				mm->free_area_cache = vma->vm_start;

		/* try just below the current vma->vm_start */
		addr = vma->vm_start-len;
	} while (len <= vma->vm_start);

fail:
	/*
	 * if hint left us with no space for the requested
	 * mapping then try again:
	 */
	if (first_time) {
		mm->free_area_cache = base;
		first_time = 0;
		goto try_again;
	}
	/*
	 * A failed mmap() very likely causes application failure,
	 * so fall back to the bottom-up function here. This scenario
	 * can happen with large stack limits and large mmap()
	 * allocations.
	 */
	mm->free_area_cache = TASK_UNMAPPED_BASE;
	addr = arch_get_unmapped_area(filp, addr0, len, pgoff, flags);
	/*
	 * Restore the topdown base:
	 */
	mm->free_area_cache = base;

	return addr;
}

static unsigned long htlb_get_low_area(unsigned long len, u16 segmask)
{
	unsigned long addr = 0;
	struct vm_area_struct *vma;

	vma = find_vma(current->mm, addr);
	while (addr + len <= 0x100000000UL) {
		BUG_ON(vma && (addr >= vma->vm_end)); /* invariant */

		if (! __within_hugepage_low_range(addr, len, segmask)) {
			addr = ALIGN(addr+1, 1<<SID_SHIFT);
			vma = find_vma(current->mm, addr);
			continue;
		}

		if (!vma || (addr + len) <= vma->vm_start)
			return addr;
		addr = ALIGN(vma->vm_end, HPAGE_SIZE);
		/* Depending on segmask this might not be a confirmed
		 * hugepage region, so the ALIGN could have skipped
		 * some VMAs */
		vma = find_vma(current->mm, addr);
	}

	return -ENOMEM;
}

static unsigned long htlb_get_high_area(unsigned long len)
{
	unsigned long addr = TASK_HPAGE_BASE;
	struct vm_area_struct *vma;

	vma = find_vma(current->mm, addr);
	for (vma = find_vma(current->mm, addr);
	     addr + len <= TASK_HPAGE_END;
	     vma = vma->vm_next) {
		BUG_ON(vma && (addr >= vma->vm_end)); /* invariant */
		BUG_ON(! within_hugepage_high_range(addr, len));

		if (!vma || (addr + len) <= vma->vm_start)
			return addr;
		addr = ALIGN(vma->vm_end, HPAGE_SIZE);
		/* Because we're in a hugepage region, this alignment
		 * should not skip us over any VMAs */
	}

	return -ENOMEM;
}

unsigned long hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
					unsigned long len, unsigned long pgoff,
					unsigned long flags)
{
	if (len & ~HPAGE_MASK)
		return -EINVAL;

	if (!(cur_cpu_spec->cpu_features & CPU_FTR_16M_PAGE))
		return -EINVAL;

	if (test_thread_flag(TIF_32BIT)) {
		int lastshift = 0;
		u16 segmask, cursegs = current->mm->context.htlb_segs;

		/* First see if we can do the mapping in the existing
		 * low hpage segments */
		addr = htlb_get_low_area(len, cursegs);
		if (addr != -ENOMEM)
			return addr;

		for (segmask = LOW_ESID_MASK(0x100000000UL-len, len);
		     ! lastshift; segmask >>=1) {
			if (segmask & 1)
				lastshift = 1;

			addr = htlb_get_low_area(len, cursegs | segmask);
			if ((addr != -ENOMEM)
			    && open_low_hpage_segs(current->mm, segmask) == 0)
				return addr;
		}
		printk(KERN_DEBUG "hugetlb_get_unmapped_area() unable to open"
		       " enough segments\n");
		return -ENOMEM;
	} else {
		return htlb_get_high_area(len);
	}
}

void hugetlb_mm_free_pgd(struct mm_struct *mm)
{
	int i;
	pgd_t *pgdir;

	spin_lock(&mm->page_table_lock);

	pgdir = mm->context.huge_pgdir;
	if (! pgdir)
		goto out;

	mm->context.huge_pgdir = NULL;

	/* cleanup any hugepte pages leftover */
	for (i = 0; i < PTRS_PER_HUGEPGD; i++) {
		pgd_t *pgd = pgdir + i;

		if (! pgd_none(*pgd)) {
			pte_t *pte = (pte_t *)pgd_page(*pgd);
			struct page *ptepage = virt_to_page(pte);

			ptepage->mapping = NULL;

			BUG_ON(memcmp(pte, empty_zero_page, PAGE_SIZE));
			kmem_cache_free(zero_cache, pte);
		}
		pgd_clear(pgd);
	}

	BUG_ON(memcmp(pgdir, empty_zero_page, PAGE_SIZE));
	kmem_cache_free(zero_cache, pgdir);

 out:
	spin_unlock(&mm->page_table_lock);
}

int hash_huge_page(struct mm_struct *mm, unsigned long access,
		   unsigned long ea, unsigned long vsid, int local)
{
	pte_t *ptep;
	unsigned long va, vpn;
	int is_write;
	pte_t old_pte, new_pte;
	unsigned long hpteflags, prpn;
	long slot;
	int err = 1;

	spin_lock(&mm->page_table_lock);

	ptep = huge_pte_offset(mm, ea);

	/* Search the Linux page table for a match with va */
	va = (vsid << 28) | (ea & 0x0fffffff);
	vpn = va >> HPAGE_SHIFT;

	/*
	 * If no pte found or not present, send the problem up to
	 * do_page_fault
	 */
	if (unlikely(!ptep || pte_none(*ptep)))
		goto out;

/* 	BUG_ON(pte_bad(*ptep)); */

	/* 
	 * Check the user's access rights to the page.  If access should be
	 * prevented then send the problem up to do_page_fault.
	 */
	is_write = access & _PAGE_RW;
	if (unlikely(is_write && !(pte_val(*ptep) & _PAGE_RW)))
		goto out;
	/*
	 * At this point, we have a pte (old_pte) which can be used to build
	 * or update an HPTE. There are 2 cases:
	 *
	 * 1. There is a valid (present) pte with no associated HPTE (this is 
	 *	the most common case)
	 * 2. There is a valid (present) pte with an associated HPTE. The
	 *	current values of the pp bits in the HPTE prevent access
	 *	because we are doing software DIRTY bit management and the
	 *	page is currently not DIRTY. 
	 */


	old_pte = *ptep;
	new_pte = old_pte;

	hpteflags = 0x2 | (! (pte_val(new_pte) & _PAGE_RW));

	/* Check if pte already has an hpte (case 2) */
	if (unlikely(pte_val(old_pte) & _PAGE_HASHPTE)) {
		/* There MIGHT be an HPTE for this pte */
		unsigned long hash, slot;

		hash = hpt_hash(vpn, 1);
		if (pte_val(old_pte) & _PAGE_SECONDARY)
			hash = ~hash;
		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		slot += (pte_val(old_pte) & _PAGE_GROUP_IX) >> 12;

		if (ppc_md.hpte_updatepp(slot, hpteflags, va, 1, local) == -1)
			pte_val(old_pte) &= ~_PAGE_HPTEFLAGS;
	}

	if (likely(!(pte_val(old_pte) & _PAGE_HASHPTE))) {
		unsigned long hash = hpt_hash(vpn, 1);
		unsigned long hpte_group;

		prpn = pte_pfn(old_pte);

repeat:
		hpte_group = ((hash & htab_hash_mask) *
			      HPTES_PER_GROUP) & ~0x7UL;

		/* Update the linux pte with the HPTE slot */
		pte_val(new_pte) &= ~_PAGE_HPTEFLAGS;
		pte_val(new_pte) |= _PAGE_HASHPTE;

		/* Add in WIMG bits */
		/* XXX We should store these in the pte */
		hpteflags |= _PAGE_COHERENT;

		slot = ppc_md.hpte_insert(hpte_group, va, prpn, 0,
					  hpteflags, 0, 1);

		/* Primary is full, try the secondary */
		if (unlikely(slot == -1)) {
			pte_val(new_pte) |= _PAGE_SECONDARY;
			hpte_group = ((~hash & htab_hash_mask) *
				      HPTES_PER_GROUP) & ~0x7UL; 
			slot = ppc_md.hpte_insert(hpte_group, va, prpn,
						  1, hpteflags, 0, 1);
			if (slot == -1) {
				if (mftb() & 0x1)
					hpte_group = ((hash & htab_hash_mask) * HPTES_PER_GROUP) & ~0x7UL;

				ppc_md.hpte_remove(hpte_group);
				goto repeat;
                        }
		}

		if (unlikely(slot == -2))
			panic("hash_huge_page: pte_insert failed\n");

		pte_val(new_pte) |= (slot<<12) & _PAGE_GROUP_IX;

		/* 
		 * No need to use ldarx/stdcx here because all who
		 * might be updating the pte will hold the
		 * page_table_lock
		 */
		*ptep = new_pte;
	}

	err = 0;

 out:
	spin_unlock(&mm->page_table_lock);

	return err;
}

/*
 *  linux/mm/memory.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

/*
 * Real VM (paging to/from disk) started 18.12.91. Much more work and
 * thought has to go into this. Oh, well..
 * 19.12.91  -  works, somewhat. Sometimes I get faults, don't know why.
 *		Found it. Everything seems to work now.
 * 20.12.91  -  Ok, making the swap-device changeable like the root.
 */

/*
 * 05.04.94  -  Multi-page memory management added for v1.1.
 * 		Idea by Alex Bligh (alex@cconcepts.co.uk)
 *
 * 16.07.99  -  Support of BIGMEM added by Gerhard Wichert, Siemens AG
 *		(Gerhard.Wichert@pdb.siemens.de)
 */

#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/rmap-locking.h>
#include <linux/module.h>

#include <asm/pgalloc.h>
#include <asm/rmap.h>
#include <asm/uaccess.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>

#include <linux/swapops.h>

#ifndef CONFIG_DISCONTIGMEM
/* use the per-pgdat data instead for discontigmem - mbligh */
unsigned long max_mapnr;
struct page *mem_map;

EXPORT_SYMBOL(max_mapnr);
EXPORT_SYMBOL(mem_map);
#endif

unsigned long num_physpages;
void * high_memory;
struct page *highmem_start_page;

EXPORT_SYMBOL(num_physpages);
EXPORT_SYMBOL(highmem_start_page);
EXPORT_SYMBOL(high_memory);

/*
 * We special-case the C-O-W ZERO_PAGE, because it's such
 * a common occurrence (no need to read the page to know
 * that it's zero - better for the cache and memory subsystem).
 */
static inline void copy_cow_page(struct page * from, struct page * to, unsigned long address)
{
	if (from == ZERO_PAGE(address)) {
		clear_user_highpage(to, address);
		return;
	}
	copy_user_highpage(to, from, address);
}

/*
 * Note: this doesn't free the actual pages themselves. That
 * has been handled earlier when unmapping all the memory regions.
 */
static inline void free_one_pmd(struct mmu_gather *tlb, pmd_t * dir)
{
	struct page *page;

	if (pmd_none(*dir))
		return;
	if (pmd_bad(*dir)) {
		pmd_ERROR(*dir);
		pmd_clear(dir);
		return;
	}
	page = pmd_page(*dir);
	pmd_clear(dir);
	pgtable_remove_rmap(page);
	pte_free_tlb(tlb, page);
}

static inline void free_one_pgd(struct mmu_gather *tlb, pgd_t * dir)
{
	int j;
	pmd_t * pmd;

	if (pgd_none(*dir))
		return;
	if (pgd_bad(*dir)) {
		pgd_ERROR(*dir);
		pgd_clear(dir);
		return;
	}
	pmd = pmd_offset(dir, 0);
	pgd_clear(dir);
	for (j = 0; j < PTRS_PER_PMD ; j++)
		free_one_pmd(tlb, pmd+j);
	pmd_free_tlb(tlb, pmd);
}

/*
 * This function clears all user-level page tables of a process - this
 * is needed by execve(), so that old pages aren't in the way.
 *
 * Must be called with pagetable lock held.
 */
void clear_page_tables(struct mmu_gather *tlb, unsigned long first, int nr)
{
	pgd_t * page_dir = tlb->mm->pgd;

	page_dir += first;
	do {
		free_one_pgd(tlb, page_dir);
		page_dir++;
	} while (--nr);
}

pte_t * pte_alloc_map(struct mm_struct *mm, pmd_t *pmd, unsigned long address)
{
	if (!pmd_present(*pmd)) {
		struct page *new;

		spin_unlock(&mm->page_table_lock);
		new = pte_alloc_one(mm, address);
		spin_lock(&mm->page_table_lock);
		if (!new)
			return NULL;

		/*
		 * Because we dropped the lock, we should re-check the
		 * entry, as somebody else could have populated it..
		 */
		if (pmd_present(*pmd)) {
			pte_free(new);
			goto out;
		}
		pgtable_add_rmap(new, mm, address);
		pmd_populate(mm, pmd, new);
	}
out:
	return pte_offset_map(pmd, address);
}

pte_t * pte_alloc_kernel(struct mm_struct *mm, pmd_t *pmd, unsigned long address)
{
	if (!pmd_present(*pmd)) {
		pte_t *new;

		spin_unlock(&mm->page_table_lock);
		new = pte_alloc_one_kernel(mm, address);
		spin_lock(&mm->page_table_lock);
		if (!new)
			return NULL;

		/*
		 * Because we dropped the lock, we should re-check the
		 * entry, as somebody else could have populated it..
		 */
		if (pmd_present(*pmd)) {
			pte_free_kernel(new);
			goto out;
		}
		pgtable_add_rmap(virt_to_page(new), mm, address);
		pmd_populate_kernel(mm, pmd, new);
	}
out:
	return pte_offset_kernel(pmd, address);
}
#define PTE_TABLE_MASK	((PTRS_PER_PTE-1) * sizeof(pte_t))
#define PMD_TABLE_MASK	((PTRS_PER_PMD-1) * sizeof(pmd_t))

/*
 * copy one vm_area from one task to the other. Assumes the page tables
 * already present in the new task to be cleared in the whole range
 * covered by this vma.
 *
 * 08Jan98 Merged into one routine from several inline routines to reduce
 *         variable count and make things faster. -jj
 *
 * dst->page_table_lock is held on entry and exit,
 * but may be dropped within pmd_alloc() and pte_alloc_map().
 */
int copy_page_range(struct mm_struct *dst, struct mm_struct *src,
			struct vm_area_struct *vma)
{
	pgd_t * src_pgd, * dst_pgd;
	unsigned long address = vma->vm_start;
	unsigned long end = vma->vm_end;
	unsigned long cow;
	struct pte_chain *pte_chain = NULL;

	if (is_vm_hugetlb_page(vma))
		return copy_hugetlb_page_range(dst, src, vma);

	pte_chain = pte_chain_alloc(GFP_ATOMIC);
	if (!pte_chain) {
		spin_unlock(&dst->page_table_lock);
		pte_chain = pte_chain_alloc(GFP_KERNEL);
		spin_lock(&dst->page_table_lock);
		if (!pte_chain)
			goto nomem;
	}
	
	cow = (vma->vm_flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE;
	src_pgd = pgd_offset(src, address)-1;
	dst_pgd = pgd_offset(dst, address)-1;

	for (;;) {
		pmd_t * src_pmd, * dst_pmd;

		src_pgd++; dst_pgd++;
		
		/* copy_pmd_range */
		
		if (pgd_none(*src_pgd))
			goto skip_copy_pmd_range;
		if (pgd_bad(*src_pgd)) {
			pgd_ERROR(*src_pgd);
			pgd_clear(src_pgd);
skip_copy_pmd_range:	address = (address + PGDIR_SIZE) & PGDIR_MASK;
			if (!address || (address >= end))
				goto out;
			continue;
		}

		src_pmd = pmd_offset(src_pgd, address);
		dst_pmd = pmd_alloc(dst, dst_pgd, address);
		if (!dst_pmd)
			goto nomem;

		do {
			pte_t * src_pte, * dst_pte;
		
			/* copy_pte_range */
		
			if (pmd_none(*src_pmd))
				goto skip_copy_pte_range;
			if (pmd_bad(*src_pmd)) {
				pmd_ERROR(*src_pmd);
				pmd_clear(src_pmd);
skip_copy_pte_range:
				address = (address + PMD_SIZE) & PMD_MASK;
				if (address >= end)
					goto out;
				goto cont_copy_pmd_range;
			}

			dst_pte = pte_alloc_map(dst, dst_pmd, address);
			if (!dst_pte)
				goto nomem;
			spin_lock(&src->page_table_lock);	
			src_pte = pte_offset_map_nested(src_pmd, address);
			do {
				pte_t pte = *src_pte;
				struct page *page;
				unsigned long pfn;

				/* copy_one_pte */

				if (pte_none(pte))
					goto cont_copy_pte_range_noset;
				/* pte contains position in swap, so copy. */
				if (!pte_present(pte)) {
					if (!pte_file(pte))
						swap_duplicate(pte_to_swp_entry(pte));
					set_pte(dst_pte, pte);
					goto cont_copy_pte_range_noset;
				}
				pfn = pte_pfn(pte);
				/* the pte points outside of valid memory, the
				 * mapping is assumed to be good, meaningful
				 * and not mapped via rmap - duplicate the
				 * mapping as is.
				 */
				page = NULL;
				if (pfn_valid(pfn)) 
					page = pfn_to_page(pfn); 

				if (!page || PageReserved(page)) {
					set_pte(dst_pte, pte);
					goto cont_copy_pte_range_noset;
				}

				/*
				 * If it's a COW mapping, write protect it both
				 * in the parent and the child
				 */
				if (cow) {
					ptep_set_wrprotect(src_pte);
					pte = *src_pte;
				}

				/*
				 * If it's a shared mapping, mark it clean in
				 * the child
				 */
				if (vma->vm_flags & VM_SHARED)
					pte = pte_mkclean(pte);
				pte = pte_mkold(pte);
				get_page(page);
				dst->rss++;

				set_pte(dst_pte, pte);
				pte_chain = page_add_rmap(page, dst_pte,
							pte_chain);
				if (pte_chain)
					goto cont_copy_pte_range_noset;
				pte_chain = pte_chain_alloc(GFP_ATOMIC);
				if (pte_chain)
					goto cont_copy_pte_range_noset;

				/*
				 * pte_chain allocation failed, and we need to
				 * run page reclaim.
				 */
				pte_unmap_nested(src_pte);
				pte_unmap(dst_pte);
				spin_unlock(&src->page_table_lock);	
				spin_unlock(&dst->page_table_lock);	
				pte_chain = pte_chain_alloc(GFP_KERNEL);
				spin_lock(&dst->page_table_lock);	
				if (!pte_chain)
					goto nomem;
				spin_lock(&src->page_table_lock);
				dst_pte = pte_offset_map(dst_pmd, address);
				src_pte = pte_offset_map_nested(src_pmd,
								address);
cont_copy_pte_range_noset:
				address += PAGE_SIZE;
				if (address >= end) {
					pte_unmap_nested(src_pte);
					pte_unmap(dst_pte);
					goto out_unlock;
				}
				src_pte++;
				dst_pte++;
			} while ((unsigned long)src_pte & PTE_TABLE_MASK);
			pte_unmap_nested(src_pte-1);
			pte_unmap(dst_pte-1);
			spin_unlock(&src->page_table_lock);
		
cont_copy_pmd_range:
			src_pmd++;
			dst_pmd++;
		} while ((unsigned long)src_pmd & PMD_TABLE_MASK);
	}
out_unlock:
	spin_unlock(&src->page_table_lock);
out:
	pte_chain_free(pte_chain);
	return 0;
nomem:
	pte_chain_free(pte_chain);
	return -ENOMEM;
}

static void
zap_pte_range(struct mmu_gather *tlb, pmd_t * pmd,
		unsigned long address, unsigned long size)
{
	unsigned long offset;
	pte_t *ptep;

	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		return;
	}
	ptep = pte_offset_map(pmd, address);
	offset = address & ~PMD_MASK;
	if (offset + size > PMD_SIZE)
		size = PMD_SIZE - offset;
	size &= PAGE_MASK;
	for (offset=0; offset < size; ptep++, offset += PAGE_SIZE) {
		pte_t pte = *ptep;
		if (pte_none(pte))
			continue;
		if (pte_present(pte)) {
			unsigned long pfn = pte_pfn(pte);

			pte = ptep_get_and_clear(ptep);
			tlb_remove_tlb_entry(tlb, ptep, address+offset);
			if (pfn_valid(pfn)) {
				struct page *page = pfn_to_page(pfn);
				if (!PageReserved(page)) {
					if (pte_dirty(pte))
						set_page_dirty(page);
					if (page->mapping && pte_young(pte) &&
							!PageSwapCache(page))
						mark_page_accessed(page);
					tlb->freed++;
					page_remove_rmap(page, ptep);
					tlb_remove_page(tlb, page);
				}
			}
		} else {
			if (!pte_file(pte))
				free_swap_and_cache(pte_to_swp_entry(pte));
			pte_clear(ptep);
		}
	}
	pte_unmap(ptep-1);
}

static void
zap_pmd_range(struct mmu_gather *tlb, pgd_t * dir,
		unsigned long address, unsigned long size)
{
	pmd_t * pmd;
	unsigned long end;

	if (pgd_none(*dir))
		return;
	if (pgd_bad(*dir)) {
		pgd_ERROR(*dir);
		pgd_clear(dir);
		return;
	}
	pmd = pmd_offset(dir, address);
	end = address + size;
	if (end > ((address + PGDIR_SIZE) & PGDIR_MASK))
		end = ((address + PGDIR_SIZE) & PGDIR_MASK);
	do {
		zap_pte_range(tlb, pmd, address, end - address);
		address = (address + PMD_SIZE) & PMD_MASK; 
		pmd++;
	} while (address < end);
}

void unmap_page_range(struct mmu_gather *tlb, struct vm_area_struct *vma,
			unsigned long address, unsigned long end)
{
	pgd_t * dir;

	if (is_vm_hugetlb_page(vma)) {
		unmap_hugepage_range(vma, address, end);
		return;
	}

	BUG_ON(address >= end);

	dir = pgd_offset(vma->vm_mm, address);
	tlb_start_vma(tlb, vma);
	do {
		zap_pmd_range(tlb, dir, address, end - address);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (address && (address < end));
	tlb_end_vma(tlb, vma);
}

/* Dispose of an entire struct mmu_gather per rescheduling point */
#if defined(CONFIG_SMP) && defined(CONFIG_PREEMPT)
#define ZAP_BLOCK_SIZE	(FREE_PTE_NR * PAGE_SIZE)
#endif

/* For UP, 256 pages at a time gives nice low latency */
#if !defined(CONFIG_SMP) && defined(CONFIG_PREEMPT)
#define ZAP_BLOCK_SIZE	(256 * PAGE_SIZE)
#endif

/* No preempt: go for the best straight-line efficiency */
#if !defined(CONFIG_PREEMPT)
#define ZAP_BLOCK_SIZE	(~(0UL))
#endif

/**
 * unmap_vmas - unmap a range of memory covered by a list of vma's
 * @tlbp: address of the caller's struct mmu_gather
 * @mm: the controlling mm_struct
 * @vma: the starting vma
 * @start_addr: virtual address at which to start unmapping
 * @end_addr: virtual address at which to end unmapping
 * @nr_accounted: Place number of unmapped pages in vm-accountable vma's here
 *
 * Returns the number of vma's which were covered by the unmapping.
 *
 * Unmap all pages in the vma list.  Called under page_table_lock.
 *
 * We aim to not hold page_table_lock for too long (for scheduling latency
 * reasons).  So zap pages in ZAP_BLOCK_SIZE bytecounts.  This means we need to
 * return the ending mmu_gather to the caller.
 *
 * Only addresses between `start' and `end' will be unmapped.
 *
 * The VMA list must be sorted in ascending virtual address order.
 *
 * unmap_vmas() assumes that the caller will flush the whole unmapped address
 * range after unmap_vmas() returns.  So the only responsibility here is to
 * ensure that any thus-far unmapped pages are flushed before unmap_vmas()
 * drops the lock and schedules.
 */
int unmap_vmas(struct mmu_gather **tlbp, struct mm_struct *mm,
		struct vm_area_struct *vma, unsigned long start_addr,
		unsigned long end_addr, unsigned long *nr_accounted)
{
	unsigned long zap_bytes = ZAP_BLOCK_SIZE;
	unsigned long tlb_start = 0;	/* For tlb_finish_mmu */
	int tlb_start_valid = 0;
	int ret = 0;

	if (vma) {	/* debug.  killme. */
		if (end_addr <= vma->vm_start)
			printk("%s: end_addr(0x%08lx) <= vm_start(0x%08lx)\n",
				__FUNCTION__, end_addr, vma->vm_start);
		if (start_addr >= vma->vm_end)
			printk("%s: start_addr(0x%08lx) <= vm_end(0x%08lx)\n",
				__FUNCTION__, start_addr, vma->vm_end);
	}

	for ( ; vma && vma->vm_start < end_addr; vma = vma->vm_next) {
		unsigned long start;
		unsigned long end;

		start = max(vma->vm_start, start_addr);
		if (start >= vma->vm_end)
			continue;
		end = min(vma->vm_end, end_addr);
		if (end <= vma->vm_start)
			continue;

		if (vma->vm_flags & VM_ACCOUNT)
			*nr_accounted += (end - start) >> PAGE_SHIFT;

		ret++;
		while (start != end) {
			unsigned long block;

			if (is_vm_hugetlb_page(vma))
				block = end - start;
			else
				block = min(zap_bytes, end - start);

			if (!tlb_start_valid) {
				tlb_start = start;
				tlb_start_valid = 1;
			}

			unmap_page_range(*tlbp, vma, start, start + block);
			start += block;
			zap_bytes -= block;
			if ((long)zap_bytes > 0)
				continue;
			if (need_resched()) {
				tlb_finish_mmu(*tlbp, tlb_start, start);
				cond_resched_lock(&mm->page_table_lock);
				*tlbp = tlb_gather_mmu(mm, 0);
				tlb_start_valid = 0;
			}
			zap_bytes = ZAP_BLOCK_SIZE;
		}
		if (vma->vm_next && vma->vm_next->vm_start < vma->vm_end)
			printk("%s: VMA list is not sorted correctly!\n",
				__FUNCTION__);		
	}
	return ret;
}

/**
 * zap_page_range - remove user pages in a given range
 * @vma: vm_area_struct holding the applicable pages
 * @address: starting address of pages to zap
 * @size: number of bytes to zap
 */
void zap_page_range(struct vm_area_struct *vma,
			unsigned long address, unsigned long size)
{
	struct mm_struct *mm = vma->vm_mm;
	struct mmu_gather *tlb;
	unsigned long end = address + size;
	unsigned long nr_accounted = 0;

	might_sleep();

	if (is_vm_hugetlb_page(vma)) {
		zap_hugepage_range(vma, address, size);
		return;
	}

	lru_add_drain();
	spin_lock(&mm->page_table_lock);
	tlb = tlb_gather_mmu(mm, 0);
	unmap_vmas(&tlb, mm, vma, address, end, &nr_accounted);
	tlb_finish_mmu(tlb, address, end);
	spin_unlock(&mm->page_table_lock);
}

/*
 * Do a quick page-table lookup for a single page.
 * mm->page_table_lock must be held.
 */
struct page *
follow_page(struct mm_struct *mm, unsigned long address, int write) 
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *ptep, pte;
	unsigned long pfn;
	struct vm_area_struct *vma;

	vma = hugepage_vma(mm, address);
	if (vma)
		return follow_huge_addr(mm, vma, address, write);

	pgd = pgd_offset(mm, address);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		goto out;

	pmd = pmd_offset(pgd, address);
	if (pmd_none(*pmd))
		goto out;
	if (pmd_huge(*pmd))
		return follow_huge_pmd(mm, address, pmd, write);
	if (pmd_bad(*pmd))
		goto out;

	ptep = pte_offset_map(pmd, address);
	if (!ptep)
		goto out;

	pte = *ptep;
	pte_unmap(ptep);
	if (pte_present(pte)) {
		if (!write || (pte_write(pte) && pte_dirty(pte))) {
			pfn = pte_pfn(pte);
			if (pfn_valid(pfn)) {
				struct page *page = pfn_to_page(pfn);

				mark_page_accessed(page);
				return page;
			}
		}
	}

out:
	return NULL;
}

/* 
 * Given a physical address, is there a useful struct page pointing to
 * it?  This may become more complex in the future if we start dealing
 * with IO-aperture pages for direct-IO.
 */

static inline struct page *get_page_map(struct page *page)
{
	if (!pfn_valid(page_to_pfn(page)))
		return 0;
	return page;
}


int get_user_pages(struct task_struct *tsk, struct mm_struct *mm,
		unsigned long start, int len, int write, int force,
		struct page **pages, struct vm_area_struct **vmas)
{
	int i;
	unsigned int flags;

	/* 
	 * Require read or write permissions.
	 * If 'force' is set, we only require the "MAY" flags.
	 */
	flags = write ? (VM_WRITE | VM_MAYWRITE) : (VM_READ | VM_MAYREAD);
	flags &= force ? (VM_MAYREAD | VM_MAYWRITE) : (VM_READ | VM_WRITE);
	i = 0;

	do {
		struct vm_area_struct *	vma;

		vma = find_extend_vma(mm, start);

#ifdef FIXADDR_USER_START
		if (!vma &&
		    start >= FIXADDR_USER_START && start < FIXADDR_USER_END) {
			static struct vm_area_struct fixmap_vma = {
				/* Catch users - if there are any valid
				   ones, we can make this be "&init_mm" or
				   something.  */
				.vm_mm = NULL,
				.vm_start = FIXADDR_USER_START,
				.vm_end = FIXADDR_USER_END,
				.vm_page_prot = PAGE_READONLY,
				.vm_flags = VM_READ | VM_EXEC,
			};
			unsigned long pg = start & PAGE_MASK;
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;
			if (write) /* user fixmap pages are read-only */
				return i ? : -EFAULT;
			pgd = pgd_offset_k(pg);
			if (!pgd)
				return i ? : -EFAULT;
			pmd = pmd_offset(pgd, pg);
			if (!pmd)
				return i ? : -EFAULT;
			pte = pte_offset_kernel(pmd, pg);
			if (!pte || !pte_present(*pte))
				return i ? : -EFAULT;
			if (pages) {
				pages[i] = pte_page(*pte);
				get_page(pages[i]);
			}
			if (vmas)
				vmas[i] = &fixmap_vma;
			i++;
			start += PAGE_SIZE;
			len--;
			continue;
		}
#endif

		if (!vma || (pages && (vma->vm_flags & VM_IO))
				|| !(flags & vma->vm_flags))
			return i ? : -EFAULT;

		if (is_vm_hugetlb_page(vma)) {
			i = follow_hugetlb_page(mm, vma, pages, vmas,
						&start, &len, i);
			continue;
		}
		spin_lock(&mm->page_table_lock);
		do {
			struct page *map;
			while (!(map = follow_page(mm, start, write))) {
				spin_unlock(&mm->page_table_lock);
				switch (handle_mm_fault(mm,vma,start,write)) {
				case VM_FAULT_MINOR:
					tsk->min_flt++;
					break;
				case VM_FAULT_MAJOR:
					tsk->maj_flt++;
					break;
				case VM_FAULT_SIGBUS:
					return i ? i : -EFAULT;
				case VM_FAULT_OOM:
					return i ? i : -ENOMEM;
				default:
					BUG();
				}
				spin_lock(&mm->page_table_lock);
			}
			if (pages) {
				pages[i] = get_page_map(map);
				if (!pages[i]) {
					spin_unlock(&mm->page_table_lock);
					while (i--)
						page_cache_release(pages[i]);
					i = -EFAULT;
					goto out;
				}
				flush_dcache_page(pages[i]);
				if (!PageReserved(pages[i]))
					page_cache_get(pages[i]);
			}
			if (vmas)
				vmas[i] = vma;
			i++;
			start += PAGE_SIZE;
			len--;
		} while(len && start < vma->vm_end);
		spin_unlock(&mm->page_table_lock);
	} while(len);
out:
	return i;
}

EXPORT_SYMBOL(get_user_pages);

static void zeromap_pte_range(pte_t * pte, unsigned long address,
                                     unsigned long size, pgprot_t prot)
{
	unsigned long end;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		pte_t zero_pte = pte_wrprotect(mk_pte(ZERO_PAGE(address), prot));
		BUG_ON(!pte_none(*pte));
		set_pte(pte, zero_pte);
		address += PAGE_SIZE;
		pte++;
	} while (address && (address < end));
}

static inline int zeromap_pmd_range(struct mm_struct *mm, pmd_t * pmd, unsigned long address,
                                    unsigned long size, pgprot_t prot)
{
	unsigned long base, end;

	base = address & PGDIR_MASK;
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		pte_t * pte = pte_alloc_map(mm, pmd, base + address);
		if (!pte)
			return -ENOMEM;
		zeromap_pte_range(pte, base + address, end - address, prot);
		pte_unmap(pte);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return 0;
}

int zeromap_page_range(struct vm_area_struct *vma, unsigned long address, unsigned long size, pgprot_t prot)
{
	int error = 0;
	pgd_t * dir;
	unsigned long beg = address;
	unsigned long end = address + size;
	struct mm_struct *mm = vma->vm_mm;

	dir = pgd_offset(mm, address);
	flush_cache_range(vma, beg, end);
	if (address >= end)
		BUG();

	spin_lock(&mm->page_table_lock);
	do {
		pmd_t *pmd = pmd_alloc(mm, dir, address);
		error = -ENOMEM;
		if (!pmd)
			break;
		error = zeromap_pmd_range(mm, pmd, address, end - address, prot);
		if (error)
			break;
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (address && (address < end));
	flush_tlb_range(vma, beg, end);
	spin_unlock(&mm->page_table_lock);
	return error;
}

/*
 * maps a range of physical memory into the requested pages. the old
 * mappings are removed. any references to nonexistent pages results
 * in null mappings (currently treated as "copy-on-access")
 */
static inline void remap_pte_range(pte_t * pte, unsigned long address, unsigned long size,
	unsigned long phys_addr, pgprot_t prot)
{
	unsigned long end;
	unsigned long pfn;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	pfn = phys_addr >> PAGE_SHIFT;
	do {
		BUG_ON(!pte_none(*pte));
		if (!pfn_valid(pfn) || PageReserved(pfn_to_page(pfn)))
 			set_pte(pte, pfn_pte(pfn, prot));
		address += PAGE_SIZE;
		pfn++;
		pte++;
	} while (address && (address < end));
}

static inline int remap_pmd_range(struct mm_struct *mm, pmd_t * pmd, unsigned long address, unsigned long size,
	unsigned long phys_addr, pgprot_t prot)
{
	unsigned long base, end;

	base = address & PGDIR_MASK;
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	phys_addr -= address;
	do {
		pte_t * pte = pte_alloc_map(mm, pmd, base + address);
		if (!pte)
			return -ENOMEM;
		remap_pte_range(pte, base + address, end - address, address + phys_addr, prot);
		pte_unmap(pte);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return 0;
}

/*  Note: this is only safe if the mm semaphore is held when called. */
int remap_page_range(struct vm_area_struct *vma, unsigned long from, unsigned long phys_addr, unsigned long size, pgprot_t prot)
{
	int error = 0;
	pgd_t * dir;
	unsigned long beg = from;
	unsigned long end = from + size;
	struct mm_struct *mm = vma->vm_mm;

	phys_addr -= from;
	dir = pgd_offset(mm, from);
	flush_cache_range(vma, beg, end);
	if (from >= end)
		BUG();

	spin_lock(&mm->page_table_lock);
	do {
		pmd_t *pmd = pmd_alloc(mm, dir, from);
		error = -ENOMEM;
		if (!pmd)
			break;
		error = remap_pmd_range(mm, pmd, from, end - from, phys_addr + from, prot);
		if (error)
			break;
		from = (from + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (from && (from < end));
	flush_tlb_range(vma, beg, end);
	spin_unlock(&mm->page_table_lock);
	return error;
}

EXPORT_SYMBOL(remap_page_range);

/*
 * Establish a new mapping:
 *  - flush the old one
 *  - update the page tables
 *  - inform the TLB about the new one
 *
 * We hold the mm semaphore for reading and vma->vm_mm->page_table_lock
 */
static inline void establish_pte(struct vm_area_struct * vma, unsigned long address, pte_t *page_table, pte_t entry)
{
	set_pte(page_table, entry);
	flush_tlb_page(vma, address);
	update_mmu_cache(vma, address, entry);
}

/*
 * We hold the mm semaphore for reading and vma->vm_mm->page_table_lock
 */
static inline void break_cow(struct vm_area_struct * vma, struct page * new_page, unsigned long address, 
		pte_t *page_table)
{
	flush_cache_page(vma, address);
	establish_pte(vma, address, page_table, pte_mkwrite(pte_mkdirty(mk_pte(new_page, vma->vm_page_prot))));
}

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * Goto-purists beware: the only reason for goto's here is that it results
 * in better assembly code.. The "default" path will see no jumps at all.
 *
 * Note that this routine assumes that the protection checks have been
 * done by the caller (the low-level page fault routine in most cases).
 * Thus we can safely just mark it writable once we've done any necessary
 * COW.
 *
 * We also mark the page dirty at this point even though the page will
 * change only once the write actually happens. This avoids a few races,
 * and potentially makes it more efficient.
 *
 * We hold the mm semaphore and the page_table_lock on entry and exit
 * with the page_table_lock released.
 */
static int do_wp_page(struct mm_struct *mm, struct vm_area_struct * vma,
	unsigned long address, pte_t *page_table, pmd_t *pmd, pte_t pte)
{
	struct page *old_page, *new_page;
	unsigned long pfn = pte_pfn(pte);
	struct pte_chain *pte_chain;

	if (unlikely(!pfn_valid(pfn))) {
		/*
		 * This should really halt the system so it can be debugged or
		 * at least the kernel stops what it's doing before it corrupts
		 * data, but for the moment just pretend this is OOM.
		 */
		pte_unmap(page_table);
		printk(KERN_ERR "do_wp_page: bogus page at address %08lx\n",
				address);
		spin_unlock(&mm->page_table_lock);
		return VM_FAULT_OOM;
	}
	old_page = pfn_to_page(pfn);

	if (!TestSetPageLocked(old_page)) {
		int reuse = can_share_swap_page(old_page);
		unlock_page(old_page);
		if (reuse) {
			flush_cache_page(vma, address);
			establish_pte(vma, address, page_table,
				pte_mkyoung(pte_mkdirty(pte_mkwrite(pte))));
			pte_unmap(page_table);
			spin_unlock(&mm->page_table_lock);
			return VM_FAULT_MINOR;
		}
	}
	pte_unmap(page_table);

	/*
	 * Ok, we need to copy. Oh, well..
	 */
	page_cache_get(old_page);
	spin_unlock(&mm->page_table_lock);

	pte_chain = pte_chain_alloc(GFP_KERNEL);
	if (!pte_chain)
		goto no_pte_chain;
	new_page = alloc_page(GFP_HIGHUSER);
	if (!new_page)
		goto no_new_page;
	copy_cow_page(old_page,new_page,address);

	/*
	 * Re-check the pte - we dropped the lock
	 */
	spin_lock(&mm->page_table_lock);
	page_table = pte_offset_map(pmd, address);
	if (pte_same(*page_table, pte)) {
		if (PageReserved(old_page))
			++mm->rss;
		page_remove_rmap(old_page, page_table);
		break_cow(vma, new_page, address, page_table);
		pte_chain = page_add_rmap(new_page, page_table, pte_chain);
		lru_cache_add_active(new_page);

		/* Free the old page.. */
		new_page = old_page;
	}
	pte_unmap(page_table);
	page_cache_release(new_page);
	page_cache_release(old_page);
	spin_unlock(&mm->page_table_lock);
	pte_chain_free(pte_chain);
	return VM_FAULT_MINOR;

no_new_page:
	pte_chain_free(pte_chain);
no_pte_chain:
	page_cache_release(old_page);
	return VM_FAULT_OOM;
}

/*
 * Helper function for invalidate_mmap_range().
 * Both hba and hlen are page numbers in PAGE_SIZE units.
 * An hlen of zero blows away the entire portion file after hba.
 */
static void
invalidate_mmap_range_list(struct list_head *head,
			   unsigned long const hba,
			   unsigned long const hlen)
{
	struct list_head *curr;
	unsigned long hea;	/* last page of hole. */
	unsigned long vba;
	unsigned long vea;	/* last page of corresponding uva hole. */
	struct vm_area_struct *vp;
	unsigned long zba;
	unsigned long zea;

	hea = hba + hlen - 1;	/* avoid overflow. */
	if (hea < hba)
		hea = ULONG_MAX;
	list_for_each(curr, head) {
		vp = list_entry(curr, struct vm_area_struct, shared);
		vba = vp->vm_pgoff;
		vea = vba + ((vp->vm_end - vp->vm_start) >> PAGE_SHIFT) - 1;
		if (hea < vba || vea < hba)
		    	continue;	/* Mapping disjoint from hole. */
		zba = (hba <= vba) ? vba : hba;
		zea = (vea <= hea) ? vea : hea;
		zap_page_range(vp,
			       ((zba - vba) << PAGE_SHIFT) + vp->vm_start,
			       (zea - zba + 1) << PAGE_SHIFT);
	}
}

/**
 * invalidate_mmap_range - invalidate the portion of all mmaps
 * in the specified address_space corresponding to the specified
 * page range in the underlying file.
 * @address_space: the address space containing mmaps to be invalidated.
 * @holebegin: byte in first page to invalidate, relative to the start of
 * the underlying file.  This will be rounded down to a PAGE_SIZE
 * boundary.  Note that this is different from vmtruncate(), which
 * must keep the partial page.  In contrast, we must get rid of
 * partial pages.
 * @holelen: size of prospective hole in bytes.  This will be rounded
 * up to a PAGE_SIZE boundary.  A holelen of zero truncates to the
 * end of the file.
 */
void invalidate_mmap_range(struct address_space *mapping,
		      loff_t const holebegin, loff_t const holelen)
{
	unsigned long hba = holebegin >> PAGE_SHIFT;
	unsigned long hlen = (holelen + PAGE_SIZE - 1) >> PAGE_SHIFT;

	/* Check for overflow. */
	if (sizeof(holelen) > sizeof(hlen)) {
		long long holeend =
			(holebegin + holelen + PAGE_SIZE - 1) >> PAGE_SHIFT;

		if (holeend & ~(long long)ULONG_MAX)
			hlen = ULONG_MAX - hba + 1;
	}
	down(&mapping->i_shared_sem);
	/* Protect against page fault */
	atomic_inc(&mapping->truncate_count);
	if (unlikely(!list_empty(&mapping->i_mmap)))
		invalidate_mmap_range_list(&mapping->i_mmap, hba, hlen);
	if (unlikely(!list_empty(&mapping->i_mmap_shared)))
		invalidate_mmap_range_list(&mapping->i_mmap_shared, hba, hlen);
	up(&mapping->i_shared_sem);
}
EXPORT_SYMBOL_GPL(invalidate_mmap_range);

/*
 * Handle all mappings that got truncated by a "truncate()"
 * system call.
 *
 * NOTE! We have to be ready to update the memory sharing
 * between the file and the memory map for a potential last
 * incomplete page.  Ugly, but necessary.
 */
int vmtruncate(struct inode * inode, loff_t offset)
{
	struct address_space *mapping = inode->i_mapping;
	unsigned long limit;

	if (inode->i_size < offset)
		goto do_expand;
	i_size_write(inode, offset);
	invalidate_mmap_range(mapping, offset + PAGE_SIZE - 1, 0);
	truncate_inode_pages(mapping, offset);
	goto out_truncate;

do_expand:
	limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
	if (limit != RLIM_INFINITY && offset > limit)
		goto out_sig;
	if (offset > inode->i_sb->s_maxbytes)
		goto out;
	i_size_write(inode, offset);

out_truncate:
	if (inode->i_op && inode->i_op->truncate)
		inode->i_op->truncate(inode);
	return 0;
out_sig:
	send_sig(SIGXFSZ, current, 0);
out:
	return -EFBIG;
}

EXPORT_SYMBOL(vmtruncate);

/* 
 * Primitive swap readahead code. We simply read an aligned block of
 * (1 << page_cluster) entries in the swap area. This method is chosen
 * because it doesn't cost us any seek time.  We also make sure to queue
 * the 'original' request together with the readahead ones...  
 */
void swapin_readahead(swp_entry_t entry)
{
	int i, num;
	struct page *new_page;
	unsigned long offset;

	/*
	 * Get the number of handles we should do readahead io to.
	 */
	num = valid_swaphandles(entry, &offset);
	for (i = 0; i < num; offset++, i++) {
		/* Ok, do the async read-ahead now */
		new_page = read_swap_cache_async(swp_entry(swp_type(entry),
						offset));
		if (!new_page)
			break;
		page_cache_release(new_page);
	}
	lru_add_drain();	/* Push any new pages onto the LRU now */
}

/*
 * We hold the mm semaphore and the page_table_lock on entry and
 * should release the pagetable lock on exit..
 */
static int do_swap_page(struct mm_struct * mm,
	struct vm_area_struct * vma, unsigned long address,
	pte_t *page_table, pmd_t *pmd, pte_t orig_pte, int write_access)
{
	struct page *page;
	swp_entry_t entry = pte_to_swp_entry(orig_pte);
	pte_t pte;
	int ret = VM_FAULT_MINOR;
	struct pte_chain *pte_chain = NULL;

	pte_unmap(page_table);
	spin_unlock(&mm->page_table_lock);
	page = lookup_swap_cache(entry);
	if (!page) {
		swapin_readahead(entry);
		page = read_swap_cache_async(entry);
		if (!page) {
			/*
			 * Back out if somebody else faulted in this pte while
			 * we released the page table lock.
			 */
			spin_lock(&mm->page_table_lock);
			page_table = pte_offset_map(pmd, address);
			if (pte_same(*page_table, orig_pte))
				ret = VM_FAULT_OOM;
			else
				ret = VM_FAULT_MINOR;
			pte_unmap(page_table);
			spin_unlock(&mm->page_table_lock);
			goto out;
		}

		/* Had to read the page from swap area: Major fault */
		ret = VM_FAULT_MAJOR;
		inc_page_state(pgmajfault);
	}

	mark_page_accessed(page);
	pte_chain = pte_chain_alloc(GFP_KERNEL);
	if (!pte_chain) {
		ret = -ENOMEM;
		goto out;
	}
	lock_page(page);

	/*
	 * Back out if somebody else faulted in this pte while we
	 * released the page table lock.
	 */
	spin_lock(&mm->page_table_lock);
	page_table = pte_offset_map(pmd, address);
	if (!pte_same(*page_table, orig_pte)) {
		pte_unmap(page_table);
		spin_unlock(&mm->page_table_lock);
		unlock_page(page);
		page_cache_release(page);
		ret = VM_FAULT_MINOR;
		goto out;
	}

	/* The page isn't present yet, go ahead with the fault. */
		
	swap_free(entry);
	if (vm_swap_full())
		remove_exclusive_swap_page(page);

	mm->rss++;
	pte = mk_pte(page, vma->vm_page_prot);
	if (write_access && can_share_swap_page(page))
		pte = pte_mkdirty(pte_mkwrite(pte));
	unlock_page(page);

	flush_icache_page(vma, page);
	set_pte(page_table, pte);
	pte_chain = page_add_rmap(page, page_table, pte_chain);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, address, pte);
	pte_unmap(page_table);
	spin_unlock(&mm->page_table_lock);
out:
	pte_chain_free(pte_chain);
	return ret;
}

/*
 * We are called with the MM semaphore and page_table_lock
 * spinlock held to protect against concurrent faults in
 * multithreaded programs. 
 */
static int
do_anonymous_page(struct mm_struct *mm, struct vm_area_struct *vma,
		pte_t *page_table, pmd_t *pmd, int write_access,
		unsigned long addr)
{
	pte_t entry;
	struct page * page = ZERO_PAGE(addr);
	struct pte_chain *pte_chain;
	int ret;

	pte_chain = pte_chain_alloc(GFP_ATOMIC);
	if (!pte_chain) {
		pte_unmap(page_table);
		spin_unlock(&mm->page_table_lock);
		pte_chain = pte_chain_alloc(GFP_KERNEL);
		if (!pte_chain)
			goto no_mem;
		spin_lock(&mm->page_table_lock);
		page_table = pte_offset_map(pmd, addr);
	}
		
	/* Read-only mapping of ZERO_PAGE. */
	entry = pte_wrprotect(mk_pte(ZERO_PAGE(addr), vma->vm_page_prot));

	/* ..except if it's a write access */
	if (write_access) {
		/* Allocate our own private page. */
		pte_unmap(page_table);
		spin_unlock(&mm->page_table_lock);

		page = alloc_page(GFP_HIGHUSER);
		if (!page)
			goto no_mem;
		clear_user_highpage(page, addr);

		spin_lock(&mm->page_table_lock);
		page_table = pte_offset_map(pmd, addr);

		if (!pte_none(*page_table)) {
			pte_unmap(page_table);
			page_cache_release(page);
			spin_unlock(&mm->page_table_lock);
			ret = VM_FAULT_MINOR;
			goto out;
		}
		mm->rss++;
		entry = pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
		lru_cache_add_active(page);
		mark_page_accessed(page);
	}

	set_pte(page_table, entry);
	/* ignores ZERO_PAGE */
	pte_chain = page_add_rmap(page, page_table, pte_chain);
	pte_unmap(page_table);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, addr, entry);
	spin_unlock(&mm->page_table_lock);
	ret = VM_FAULT_MINOR;
	goto out;

no_mem:
	ret = VM_FAULT_OOM;
out:
	pte_chain_free(pte_chain);
	return ret;
}

/*
 * do_no_page() tries to create a new page mapping. It aggressively
 * tries to share with existing pages, but makes a separate copy if
 * the "write_access" parameter is true in order to avoid the next
 * page fault.
 *
 * As this is called only for pages that do not currently exist, we
 * do not need to flush old virtual caches or the TLB.
 *
 * This is called with the MM semaphore held and the page table
 * spinlock held. Exit with the spinlock released.
 */
static int
do_no_page(struct mm_struct *mm, struct vm_area_struct *vma,
	unsigned long address, int write_access, pte_t *page_table, pmd_t *pmd)
{
	struct page * new_page;
	struct address_space *mapping = NULL;
	pte_t entry;
	struct pte_chain *pte_chain;
	int sequence = 0;
	int ret = VM_FAULT_MINOR;

	if (!vma->vm_ops || !vma->vm_ops->nopage)
		return do_anonymous_page(mm, vma, page_table,
					pmd, write_access, address);
	pte_unmap(page_table);
	spin_unlock(&mm->page_table_lock);

	if (vma->vm_file) {
		mapping = vma->vm_file->f_dentry->d_inode->i_mapping;
		sequence = atomic_read(&mapping->truncate_count);
	}
	smp_rmb();  /* Prevent CPU from reordering lock-free ->nopage() */
retry:
	new_page = vma->vm_ops->nopage(vma, address & PAGE_MASK, &ret);

	/* no page was available -- either SIGBUS or OOM */
	if (new_page == NOPAGE_SIGBUS)
		return VM_FAULT_SIGBUS;
	if (new_page == NOPAGE_OOM)
		return VM_FAULT_OOM;

	pte_chain = pte_chain_alloc(GFP_KERNEL);
	if (!pte_chain)
		goto oom;

	/*
	 * Should we do an early C-O-W break?
	 */
	if (write_access && !(vma->vm_flags & VM_SHARED)) {
		struct page * page = alloc_page(GFP_HIGHUSER);
		if (!page) {
			page_cache_release(new_page);
			goto oom;
		}
		copy_user_highpage(page, new_page, address);
		page_cache_release(new_page);
		lru_cache_add_active(page);
		new_page = page;
	}

	spin_lock(&mm->page_table_lock);
	/*
	 * For a file-backed vma, someone could have truncated or otherwise
	 * invalidated this page.  If invalidate_mmap_range got called,
	 * retry getting the page.
	 */
	if (mapping &&
	      (unlikely(sequence != atomic_read(&mapping->truncate_count)))) {
		sequence = atomic_read(&mapping->truncate_count);
		spin_unlock(&mm->page_table_lock);
		page_cache_release(new_page);
		pte_chain_free(pte_chain);
		goto retry;
	}
	page_table = pte_offset_map(pmd, address);

	/*
	 * This silly early PAGE_DIRTY setting removes a race
	 * due to the bad i386 page protection. But it's valid
	 * for other architectures too.
	 *
	 * Note that if write_access is true, we either now have
	 * an exclusive copy of the page, or this is a shared mapping,
	 * so we can make it writable and dirty to avoid having to
	 * handle that later.
	 */
	/* Only go through if we didn't race with anybody else... */
	if (pte_none(*page_table)) {
		if (!PageReserved(new_page))
			++mm->rss;
		flush_icache_page(vma, new_page);
		entry = mk_pte(new_page, vma->vm_page_prot);
		if (write_access)
			entry = pte_mkwrite(pte_mkdirty(entry));
		set_pte(page_table, entry);
		pte_chain = page_add_rmap(new_page, page_table, pte_chain);
		pte_unmap(page_table);
	} else {
		/* One of our sibling threads was faster, back out. */
		pte_unmap(page_table);
		page_cache_release(new_page);
		spin_unlock(&mm->page_table_lock);
		goto out;
	}

	/* no need to invalidate: a not-present page shouldn't be cached */
	update_mmu_cache(vma, address, entry);
	spin_unlock(&mm->page_table_lock);
	goto out;
oom:
	ret = VM_FAULT_OOM;
out:
	pte_chain_free(pte_chain);
	return ret;
}

/*
 * Fault of a previously existing named mapping. Repopulate the pte
 * from the encoded file_pte if possible. This enables swappable
 * nonlinear vmas.
 */
static int do_file_page(struct mm_struct * mm, struct vm_area_struct * vma,
	unsigned long address, int write_access, pte_t *pte, pmd_t *pmd)
{
	unsigned long pgoff;
	int err;

	BUG_ON(!vma->vm_ops || !vma->vm_ops->nopage);
	/*
	 * Fall back to the linear mapping if the fs does not support
	 * ->populate:
	 */
	if (!vma->vm_ops || !vma->vm_ops->populate || 
			(write_access && !(vma->vm_flags & VM_SHARED))) {
		pte_clear(pte);
		return do_no_page(mm, vma, address, write_access, pte, pmd);
	}

	pgoff = pte_to_pgoff(*pte);

	pte_unmap(pte);
	spin_unlock(&mm->page_table_lock);

	err = vma->vm_ops->populate(vma, address & PAGE_MASK, PAGE_SIZE, vma->vm_page_prot, pgoff, 0);
	if (err == -ENOMEM)
		return VM_FAULT_OOM;
	if (err)
		return VM_FAULT_SIGBUS;
	return VM_FAULT_MAJOR;
}

/*
 * These routines also need to handle stuff like marking pages dirty
 * and/or accessed for architectures that don't do it in hardware (most
 * RISC architectures).  The early dirtying is also good on the i386.
 *
 * There is also a hook called "update_mmu_cache()" that architectures
 * with external mmu caches can use to update those (ie the Sparc or
 * PowerPC hashed page tables that act as extended TLBs).
 *
 * Note the "page_table_lock". It is to protect against kswapd removing
 * pages from under us. Note that kswapd only ever _removes_ pages, never
 * adds them. As such, once we have noticed that the page is not present,
 * we can drop the lock early.
 *
 * The adding of pages is protected by the MM semaphore (which we hold),
 * so we don't need to worry about a page being suddenly been added into
 * our VM.
 *
 * We enter with the pagetable spinlock held, we are supposed to
 * release it when done.
 */
static inline int handle_pte_fault(struct mm_struct *mm,
	struct vm_area_struct * vma, unsigned long address,
	int write_access, pte_t *pte, pmd_t *pmd)
{
	pte_t entry;

	entry = *pte;
	if (!pte_present(entry)) {
		/*
		 * If it truly wasn't present, we know that kswapd
		 * and the PTE updates will not touch it later. So
		 * drop the lock.
		 */
		if (pte_none(entry))
			return do_no_page(mm, vma, address, write_access, pte, pmd);
		if (pte_file(entry))
			return do_file_page(mm, vma, address, write_access, pte, pmd);
		return do_swap_page(mm, vma, address, pte, pmd, entry, write_access);
	}

	if (write_access) {
		if (!pte_write(entry))
			return do_wp_page(mm, vma, address, pte, pmd, entry);

		entry = pte_mkdirty(entry);
	}
	entry = pte_mkyoung(entry);
	establish_pte(vma, address, pte, entry);
	pte_unmap(pte);
	spin_unlock(&mm->page_table_lock);
	return VM_FAULT_MINOR;
}

/*
 * By the time we get here, we already hold the mm semaphore
 */
int handle_mm_fault(struct mm_struct *mm, struct vm_area_struct * vma,
	unsigned long address, int write_access)
{
	pgd_t *pgd;
	pmd_t *pmd;

	__set_current_state(TASK_RUNNING);
	pgd = pgd_offset(mm, address);

	inc_page_state(pgfault);

	if (is_vm_hugetlb_page(vma))
		return VM_FAULT_SIGBUS;	/* mapping truncation does this. */

	/*
	 * We need the page table lock to synchronize with kswapd
	 * and the SMP-safe atomic PTE updates.
	 */
	spin_lock(&mm->page_table_lock);
	pmd = pmd_alloc(mm, pgd, address);

	if (pmd) {
		pte_t * pte = pte_alloc_map(mm, pmd, address);
		if (pte)
			return handle_pte_fault(mm, vma, address, write_access, pte, pmd);
	}
	spin_unlock(&mm->page_table_lock);
	return VM_FAULT_OOM;
}

/*
 * Allocate page middle directory.
 *
 * We've already handled the fast-path in-line, and we own the
 * page table lock.
 *
 * On a two-level page table, this ends up actually being entirely
 * optimized away.
 */
pmd_t *__pmd_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address)
{
	pmd_t *new;

	spin_unlock(&mm->page_table_lock);
	new = pmd_alloc_one(mm, address);
	spin_lock(&mm->page_table_lock);
	if (!new)
		return NULL;

	/*
	 * Because we dropped the lock, we should re-check the
	 * entry, as somebody else could have populated it..
	 */
	if (pgd_present(*pgd)) {
		pmd_free(new);
		goto out;
	}
	pgd_populate(mm, pgd, new);
out:
	return pmd_offset(pgd, address);
}

int make_pages_present(unsigned long addr, unsigned long end)
{
	int ret, len, write;
	struct vm_area_struct * vma;

	vma = find_vma(current->mm, addr);
	write = (vma->vm_flags & VM_WRITE) != 0;
	if (addr >= end)
		BUG();
	if (end > vma->vm_end)
		BUG();
	len = (end+PAGE_SIZE-1)/PAGE_SIZE-addr/PAGE_SIZE;
	ret = get_user_pages(current, current->mm, addr,
			len, write, 0, NULL, NULL);
	if (ret < 0)
		return ret;
	return ret == len ? 0 : -1;
}

/* 
 * Map a vmalloc()-space virtual address to the physical page.
 */
struct page * vmalloc_to_page(void * vmalloc_addr)
{
	unsigned long addr = (unsigned long) vmalloc_addr;
	struct page *page = NULL;
	pgd_t *pgd = pgd_offset_k(addr);
	pmd_t *pmd;
	pte_t *ptep, pte;
  
	if (!pgd_none(*pgd)) {
		pmd = pmd_offset(pgd, addr);
		if (!pmd_none(*pmd)) {
			preempt_disable();
			ptep = pte_offset_map(pmd, addr);
			pte = *ptep;
			if (pte_present(pte))
				page = pte_page(pte);
			pte_unmap(ptep);
			preempt_enable();
		}
	}
	return page;
}

EXPORT_SYMBOL(vmalloc_to_page);

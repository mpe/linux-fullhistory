/*
 * mm/rmap.c - physical to virtual reverse mappings
 *
 * Copyright 2001, Rik van Riel <riel@conectiva.com.br>
 * Released under the General Public License (GPL).
 *
 *
 * Simple, low overhead pte-based reverse mapping scheme.
 * This is kept modular because we may want to experiment
 * with object-based reverse mapping schemes. Please try
 * to keep this thing as modular as possible.
 */

/*
 * Locking:
 * - the page->pte.chain is protected by the PG_chainlock bit,
 *   which nests within the the mm->page_table_lock,
 *   which nests within the page lock.
 * - because swapout locking is opposite to the locking order
 *   in the page fault path, the swapout path uses trylocks
 *   on the mm->page_table_lock
 */
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/rmap-locking.h>
#include <linux/cache.h>
#include <linux/percpu.h>

#include <asm/pgalloc.h>
#include <asm/rmap.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

/* #define DEBUG_RMAP */

/*
 * Shared pages have a chain of pte_chain structures, used to locate
 * all the mappings to this page. We only need a pointer to the pte
 * here, the page struct for the page table page contains the process
 * it belongs to and the offset within that process.
 *
 * We use an array of pte pointers in this structure to minimise cache misses
 * while traversing reverse maps.
 */
#define NRPTE ((L1_CACHE_BYTES - sizeof(unsigned long))/sizeof(pte_addr_t))

/*
 * next_and_idx encodes both the address of the next pte_chain and the
 * offset of the highest-index used pte in ptes[].
 */
struct pte_chain {
	unsigned long next_and_idx;
	pte_addr_t ptes[NRPTE];
} ____cacheline_aligned;

kmem_cache_t	*pte_chain_cache;

static inline struct pte_chain *pte_chain_next(struct pte_chain *pte_chain)
{
	return (struct pte_chain *)(pte_chain->next_and_idx & ~NRPTE);
}

static inline struct pte_chain *pte_chain_ptr(unsigned long pte_chain_addr)
{
	return (struct pte_chain *)(pte_chain_addr & ~NRPTE);
}

static inline int pte_chain_idx(struct pte_chain *pte_chain)
{
	return pte_chain->next_and_idx & NRPTE;
}

static inline unsigned long
pte_chain_encode(struct pte_chain *pte_chain, int idx)
{
	return (unsigned long)pte_chain | idx;
}

/*
 * pte_chain list management policy:
 *
 * - If a page has a pte_chain list then it is shared by at least two processes,
 *   because a single sharing uses PageDirect. (Well, this isn't true yet,
 *   coz this code doesn't collapse singletons back to PageDirect on the remove
 *   path).
 * - A pte_chain list has free space only in the head member - all succeeding
 *   members are 100% full.
 * - If the head element has free space, it occurs in its leading slots.
 * - All free space in the pte_chain is at the start of the head member.
 * - Insertion into the pte_chain puts a pte pointer in the last free slot of
 *   the head member.
 * - Removal from a pte chain moves the head pte of the head member onto the
 *   victim pte and frees the head member if it became empty.
 */

/**
 ** VM stuff below this comment
 **/

/**
 * page_referenced - test if the page was referenced
 * @page: the page to test
 *
 * Quick test_and_clear_referenced for all mappings to a page,
 * returns the number of processes which referenced the page.
 * Caller needs to hold the pte_chain_lock.
 *
 * If the page has a single-entry pte_chain, collapse that back to a PageDirect
 * representation.  This way, it's only done under memory pressure.
 */
int page_referenced(struct page * page)
{
	struct pte_chain *pc;
	int referenced = 0;

	if (page_test_and_clear_young(page))
		mark_page_accessed(page);

	if (TestClearPageReferenced(page))
		referenced++;

	if (PageDirect(page)) {
		pte_t *pte = rmap_ptep_map(page->pte.direct);
		if (ptep_test_and_clear_young(pte))
			referenced++;
		rmap_ptep_unmap(pte);
	} else {
		int nr_chains = 0;

		/* Check all the page tables mapping this page. */
		for (pc = page->pte.chain; pc; pc = pte_chain_next(pc)) {
			int i;

			for (i = pte_chain_idx(pc); i < NRPTE; i++) {
				pte_addr_t pte_paddr = pc->ptes[i];
				pte_t *p;

				p = rmap_ptep_map(pte_paddr);
				if (ptep_test_and_clear_young(p))
					referenced++;
				rmap_ptep_unmap(p);
				nr_chains++;
			}
		}
		if (nr_chains == 1) {
			pc = page->pte.chain;
			page->pte.direct = pc->ptes[NRPTE-1];
			SetPageDirect(page);
			pc->ptes[NRPTE-1] = 0;
			__pte_chain_free(pc);
		}
	}
	return referenced;
}

/**
 * page_add_rmap - add reverse mapping entry to a page
 * @page: the page to add the mapping to
 * @ptep: the page table entry mapping this page
 *
 * Add a new pte reverse mapping to a page.
 * The caller needs to hold the mm->page_table_lock.
 */
struct pte_chain *
page_add_rmap(struct page *page, pte_t *ptep, struct pte_chain *pte_chain)
{
	pte_addr_t pte_paddr = ptep_to_paddr(ptep);
	struct pte_chain *cur_pte_chain;

	if (!pfn_valid(page_to_pfn(page)) || PageReserved(page))
		return pte_chain;

	pte_chain_lock(page);

	if (page->pte.direct == 0) {
		page->pte.direct = pte_paddr;
		SetPageDirect(page);
		inc_page_state(nr_mapped);
		goto out;
	}

	if (PageDirect(page)) {
		/* Convert a direct pointer into a pte_chain */
		ClearPageDirect(page);
		pte_chain->ptes[NRPTE-1] = page->pte.direct;
		pte_chain->ptes[NRPTE-2] = pte_paddr;
		pte_chain->next_and_idx = pte_chain_encode(NULL, NRPTE-2);
		page->pte.direct = 0;
		page->pte.chain = pte_chain;
		pte_chain = NULL;	/* We consumed it */
		goto out;
	}

	cur_pte_chain = page->pte.chain;
	if (cur_pte_chain->ptes[0]) {	/* It's full */
		pte_chain->next_and_idx = pte_chain_encode(cur_pte_chain,
								NRPTE - 1);
		page->pte.chain = pte_chain;
		pte_chain->ptes[NRPTE-1] = pte_paddr;
		pte_chain = NULL;	/* We consumed it */
		goto out;
	}
	cur_pte_chain->ptes[pte_chain_idx(cur_pte_chain) - 1] = pte_paddr;
	cur_pte_chain->next_and_idx--;
out:
	pte_chain_unlock(page);
	return pte_chain;
}

/**
 * page_remove_rmap - take down reverse mapping to a page
 * @page: page to remove mapping from
 * @ptep: page table entry to remove
 *
 * Removes the reverse mapping from the pte_chain of the page,
 * after that the caller can clear the page table entry and free
 * the page.
 * Caller needs to hold the mm->page_table_lock.
 */
void page_remove_rmap(struct page *page, pte_t *ptep)
{
	pte_addr_t pte_paddr = ptep_to_paddr(ptep);
	struct pte_chain *pc;

	if (!pfn_valid(page_to_pfn(page)) || PageReserved(page))
		return;

	pte_chain_lock(page);

	if (!page_mapped(page))
		goto out_unlock;	/* remap_page_range() from a driver? */

	if (PageDirect(page)) {
		if (page->pte.direct == pte_paddr) {
			page->pte.direct = 0;
			ClearPageDirect(page);
			goto out;
		}
	} else {
		struct pte_chain *start = page->pte.chain;
		struct pte_chain *next;
		int victim_i = pte_chain_idx(start);

		for (pc = start; pc; pc = next) {
			int i;

			next = pte_chain_next(pc);
			if (next)
				prefetch(next);
			for (i = pte_chain_idx(pc); i < NRPTE; i++) {
				pte_addr_t pa = pc->ptes[i];

				if (pa != pte_paddr)
					continue;
				pc->ptes[i] = start->ptes[victim_i];
				start->ptes[victim_i] = 0;
				if (victim_i == NRPTE-1) {
					/* Emptied a pte_chain */
					page->pte.chain = pte_chain_next(start);
					__pte_chain_free(start);
				} else {
					start->next_and_idx++;
				}
				goto out;
			}
		}
	}
out:
	if (page->pte.direct == 0 && page_test_and_clear_dirty(page))
		set_page_dirty(page);
	if (!page_mapped(page))
		dec_page_state(nr_mapped);
out_unlock:
	pte_chain_unlock(page);
	return;
}

/**
 * try_to_unmap_one - worker function for try_to_unmap
 * @page: page to unmap
 * @ptep: page table entry to unmap from page
 *
 * Internal helper function for try_to_unmap, called for each page
 * table entry mapping a page. Because locking order here is opposite
 * to the locking order used by the page fault path, we use trylocks.
 * Locking:
 *	    page lock			shrink_list(), trylock
 *		pte_chain_lock		shrink_list()
 *		    mm->page_table_lock	try_to_unmap_one(), trylock
 */
static int FASTCALL(try_to_unmap_one(struct page *, pte_addr_t));
static int try_to_unmap_one(struct page * page, pte_addr_t paddr)
{
	pte_t *ptep = rmap_ptep_map(paddr);
	unsigned long address = ptep_to_address(ptep);
	struct mm_struct * mm = ptep_to_mm(ptep);
	struct vm_area_struct * vma;
	pte_t pte;
	int ret;

	if (!mm)
		BUG();

	/*
	 * We need the page_table_lock to protect us from page faults,
	 * munmap, fork, etc...
	 */
	if (!spin_trylock(&mm->page_table_lock)) {
		rmap_ptep_unmap(ptep);
		return SWAP_AGAIN;
	}


	/* During mremap, it's possible pages are not in a VMA. */
	vma = find_vma(mm, address);
	if (!vma) {
		ret = SWAP_FAIL;
		goto out_unlock;
	}

	/* The page is mlock()d, we cannot swap it out. */
	if (vma->vm_flags & VM_LOCKED) {
		ret = SWAP_FAIL;
		goto out_unlock;
	}

	/* Nuke the page table entry. */
	flush_cache_page(vma, address);
	pte = ptep_clear_flush(vma, address, ptep);

	if (PageSwapCache(page)) {
		/*
		 * Store the swap location in the pte.
		 * See handle_pte_fault() ...
		 */
		swp_entry_t entry = { .val = page->index };
		swap_duplicate(entry);
		set_pte(ptep, swp_entry_to_pte(entry));
		BUG_ON(pte_file(*ptep));
	} else {
		unsigned long pgidx;
		/*
		 * If a nonlinear mapping then store the file page offset
		 * in the pte.
		 */
		pgidx = (address - vma->vm_start) >> PAGE_SHIFT;
		pgidx += vma->vm_pgoff;
		pgidx >>= PAGE_CACHE_SHIFT - PAGE_SHIFT;
		if (page->index != pgidx) {
			set_pte(ptep, pgoff_to_pte(page->index));
			BUG_ON(!pte_file(*ptep));
		}
	}

	/* Move the dirty bit to the physical page now the pte is gone. */
	if (pte_dirty(pte))
		set_page_dirty(page);

	mm->rss--;
	page_cache_release(page);
	ret = SWAP_SUCCESS;

out_unlock:
	rmap_ptep_unmap(ptep);
	spin_unlock(&mm->page_table_lock);
	return ret;
}

/**
 * try_to_unmap - try to remove all page table mappings to a page
 * @page: the page to get unmapped
 *
 * Tries to remove all the page table entries which are mapping this
 * page, used in the pageout path.  Caller must hold the page lock
 * and its pte chain lock.  Return values are:
 *
 * SWAP_SUCCESS	- we succeeded in removing all mappings
 * SWAP_AGAIN	- we missed a trylock, try again later
 * SWAP_FAIL	- the page is unswappable
 */
int try_to_unmap(struct page * page)
{
	struct pte_chain *pc, *next_pc, *start;
	int ret = SWAP_SUCCESS;
	int victim_i;

	/* This page should not be on the pageout lists. */
	if (PageReserved(page))
		BUG();
	if (!PageLocked(page))
		BUG();
	/* We need backing store to swap out a page. */
	if (!page->mapping)
		BUG();

	if (PageDirect(page)) {
		ret = try_to_unmap_one(page, page->pte.direct);
		if (ret == SWAP_SUCCESS) {
			if (page_test_and_clear_dirty(page))
				set_page_dirty(page);
			page->pte.direct = 0;
			ClearPageDirect(page);
		}
		goto out;
	}		

	start = page->pte.chain;
	victim_i = pte_chain_idx(start);
	for (pc = start; pc; pc = next_pc) {
		int i;

		next_pc = pte_chain_next(pc);
		if (next_pc)
			prefetch(next_pc);
		for (i = pte_chain_idx(pc); i < NRPTE; i++) {
			pte_addr_t pte_paddr = pc->ptes[i];

			switch (try_to_unmap_one(page, pte_paddr)) {
			case SWAP_SUCCESS:
				/*
				 * Release a slot.  If we're releasing the
				 * first pte in the first pte_chain then
				 * pc->ptes[i] and start->ptes[victim_i] both
				 * refer to the same thing.  It works out.
				 */
				pc->ptes[i] = start->ptes[victim_i];
				start->ptes[victim_i] = 0;
				victim_i++;
				if (victim_i == NRPTE) {
					page->pte.chain = pte_chain_next(start);
					__pte_chain_free(start);
					start = page->pte.chain;
					victim_i = 0;
				} else {
					start->next_and_idx++;
				}
				if (page->pte.direct == 0 &&
				    page_test_and_clear_dirty(page))
					set_page_dirty(page);
				break;
			case SWAP_AGAIN:
				/* Skip this pte, remembering status. */
				ret = SWAP_AGAIN;
				continue;
			case SWAP_FAIL:
				ret = SWAP_FAIL;
				goto out;
			}
		}
	}
out:
	if (!page_mapped(page))
		dec_page_state(nr_mapped);
	return ret;
}

/**
 ** No more VM stuff below this comment, only pte_chain helper
 ** functions.
 **/

static void pte_chain_ctor(void *p, kmem_cache_t *cachep, unsigned long flags)
{
	struct pte_chain *pc = p;

	memset(pc, 0, sizeof(*pc));
}

DEFINE_PER_CPU(struct pte_chain *, local_pte_chain) = 0;

/**
 * __pte_chain_free - free pte_chain structure
 * @pte_chain: pte_chain struct to free
 */
void __pte_chain_free(struct pte_chain *pte_chain)
{
	struct pte_chain **pte_chainp;

	pte_chainp = &get_cpu_var(local_pte_chain);
	if (pte_chain->next_and_idx)
		pte_chain->next_and_idx = 0;
	if (*pte_chainp)
		kmem_cache_free(pte_chain_cache, *pte_chainp);
	*pte_chainp = pte_chain;
	put_cpu_var(local_pte_chain);
}

/*
 * pte_chain_alloc(): allocate a pte_chain structure for use by page_add_rmap().
 *
 * The caller of page_add_rmap() must perform the allocation because
 * page_add_rmap() is invariably called under spinlock.  Often, page_add_rmap()
 * will not actually use the pte_chain, because there is space available in one
 * of the existing pte_chains which are attached to the page.  So the case of
 * allocating and then freeing a single pte_chain is specially optimised here,
 * with a one-deep per-cpu cache.
 */
struct pte_chain *pte_chain_alloc(int gfp_flags)
{
	struct pte_chain *ret;
	struct pte_chain **pte_chainp;

	might_sleep_if(gfp_flags & __GFP_WAIT);

	pte_chainp = &get_cpu_var(local_pte_chain);
	if (*pte_chainp) {
		ret = *pte_chainp;
		*pte_chainp = NULL;
		put_cpu_var(local_pte_chain);
	} else {
		put_cpu_var(local_pte_chain);
		ret = kmem_cache_alloc(pte_chain_cache, gfp_flags);
	}
	return ret;
}

void __init pte_chain_init(void)
{
	pte_chain_cache = kmem_cache_create(	"pte_chain",
						sizeof(struct pte_chain),
						0,
						SLAB_MUST_HWCACHE_ALIGN,
						pte_chain_ctor,
						NULL);

	if (!pte_chain_cache)
		panic("failed to create pte_chain cache!\n");
}

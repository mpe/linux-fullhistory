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
 */

#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>

unsigned long max_mapnr = 0;
unsigned long num_physpages = 0;
void * high_memory = NULL;

/*
 * We special-case the C-O-W ZERO_PAGE, because it's such
 * a common occurrence (no need to read the page to know
 * that it's zero - better for the cache and memory subsystem).
 */
static inline void copy_cow_page(unsigned long from, unsigned long to)
{
	if (from == ZERO_PAGE) {
		clear_page(to);
		return;
	}
	copy_page(to, from);
}

mem_map_t * mem_map = NULL;

/*
 * oom() prints a message (so that the user knows why the process died),
 * and gives the process an untrappable SIGKILL.
 */
void oom(struct task_struct * task)
{
	printk("\nOut of memory for %s.\n", task->comm);
	force_sig(SIGKILL, task);
}

/*
 * Note: this doesn't free the actual pages themselves. That
 * has been handled earlier when unmapping all the memory regions.
 */
static inline void free_one_pmd(pmd_t * dir)
{
	pte_t * pte;

	if (pmd_none(*dir))
		return;
	if (pmd_bad(*dir)) {
		printk("free_one_pmd: bad directory entry %08lx\n", pmd_val(*dir));
		pmd_clear(dir);
		return;
	}
	pte = pte_offset(dir, 0);
	pmd_clear(dir);
	pte_free(pte);
}

static inline void free_one_pgd(pgd_t * dir)
{
	int j;
	pmd_t * pmd;

	if (pgd_none(*dir))
		return;
	if (pgd_bad(*dir)) {
		printk("free_one_pgd: bad directory entry %08lx\n", pgd_val(*dir));
		pgd_clear(dir);
		return;
	}
	pmd = pmd_offset(dir, 0);
	pgd_clear(dir);
	for (j = 0; j < PTRS_PER_PMD ; j++)
		free_one_pmd(pmd+j);
	pmd_free(pmd);
}

/* Low and high watermarks for page table cache.
   The system should try to have pgt_water[0] <= cache elements <= pgt_water[1]
 */
int pgt_cache_water[2] = { 25, 50 };

/* Returns the number of pages freed */
int check_pgt_cache(void)
{
	return do_check_pgt_cache(pgt_cache_water[0], pgt_cache_water[1]);
}


/*
 * This function clears all user-level page tables of a process - this
 * is needed by execve(), so that old pages aren't in the way.
 */
void clear_page_tables(struct mm_struct *mm, unsigned long first, int nr)
{
	pgd_t * page_dir = mm->pgd;

	if (page_dir && page_dir != swapper_pg_dir) {
		page_dir += first;
		do {
			free_one_pgd(page_dir);
			page_dir++;
		} while (--nr);

		/* keep the page table cache within bounds */
		check_pgt_cache();
	}
}

/*
 * This function just free's the page directory - the
 * pages tables themselves have been freed earlier by 
 * clear_page_tables().
 */
void free_page_tables(struct mm_struct * mm)
{
	pgd_t * page_dir = mm->pgd;

	if (page_dir) {
		if (page_dir == swapper_pg_dir)
			goto out_bad;
		pgd_free(page_dir);
	}
	return;

out_bad:
	printk(KERN_ERR
		"free_page_tables: Trying to free kernel pgd\n");
	return;
}

int new_page_tables(struct task_struct * tsk)
{
	pgd_t * new_pg;

	if (!(new_pg = pgd_alloc()))
		return -ENOMEM;
	SET_PAGE_DIR(tsk, new_pg);
	tsk->mm->pgd = new_pg;
	return 0;
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
 */
int copy_page_range(struct mm_struct *dst, struct mm_struct *src,
			struct vm_area_struct *vma)
{
	pgd_t * src_pgd, * dst_pgd;
	unsigned long address = vma->vm_start;
	unsigned long end = vma->vm_end;
	unsigned long cow = (vma->vm_flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE;
	
	src_pgd = pgd_offset(src, address)-1;
	dst_pgd = pgd_offset(dst, address)-1;
	
	for (;;) {
		pmd_t * src_pmd, * dst_pmd;

		src_pgd++; dst_pgd++;
		
		/* copy_pmd_range */
		
		if (pgd_none(*src_pgd))
			goto skip_copy_pmd_range;
		if (pgd_bad(*src_pgd)) {
			printk("copy_pmd_range: bad pgd (%08lx)\n", 
				pgd_val(*src_pgd));
			pgd_clear(src_pgd);
skip_copy_pmd_range:	address = (address + PGDIR_SIZE) & PGDIR_MASK;
			if (address >= end)
				goto out;
			continue;
		}
		if (pgd_none(*dst_pgd)) {
			if (!pmd_alloc(dst_pgd, 0))
				goto nomem;
		}
		
		src_pmd = pmd_offset(src_pgd, address);
		dst_pmd = pmd_offset(dst_pgd, address);

		do {
			pte_t * src_pte, * dst_pte;
		
			/* copy_pte_range */
		
			if (pmd_none(*src_pmd))
				goto skip_copy_pte_range;
			if (pmd_bad(*src_pmd)) {
				printk("copy_pte_range: bad pmd (%08lx)\n", pmd_val(*src_pmd));
				pmd_clear(src_pmd);
skip_copy_pte_range:		address = (address + PMD_SIZE) & PMD_MASK;
				if (address >= end)
					goto out;
				goto cont_copy_pmd_range;
			}
			if (pmd_none(*dst_pmd)) {
				if (!pte_alloc(dst_pmd, 0))
					goto nomem;
			}
			
			src_pte = pte_offset(src_pmd, address);
			dst_pte = pte_offset(dst_pmd, address);
			
			do {
				pte_t pte = *src_pte;
				unsigned long page_nr;
				
				/* copy_one_pte */

				if (pte_none(pte))
					goto cont_copy_pte_range;
				if (!pte_present(pte)) {
					swap_duplicate(pte_val(pte));
					set_pte(dst_pte, pte);
					goto cont_copy_pte_range;
				}
				page_nr = MAP_NR(pte_page(pte));
				if (page_nr >= max_mapnr || 
				    PageReserved(mem_map+page_nr)) {
					set_pte(dst_pte, pte);
					goto cont_copy_pte_range;
				}
				/* If it's a COW mapping, write protect it both in the parent and the child */
				if (cow) {
					pte = pte_wrprotect(pte);
					set_pte(src_pte, pte);
				}
				/* If it's a shared mapping, mark it clean in the child */
				if (vma->vm_flags & VM_SHARED)
					pte = pte_mkclean(pte);
				set_pte(dst_pte, pte_mkold(pte));
				atomic_inc(&mem_map[page_nr].count);
			
cont_copy_pte_range:		address += PAGE_SIZE;
				if (address >= end)
					goto out;
				src_pte++;
				dst_pte++;
			} while ((unsigned long)src_pte & PTE_TABLE_MASK);
		
cont_copy_pmd_range:	src_pmd++;
			dst_pmd++;
		} while ((unsigned long)src_pmd & PMD_TABLE_MASK);
	}
out:
	return 0;

nomem:
	return -ENOMEM;
}

/*
 * Return indicates whether a page was freed so caller can adjust rss
 */
static inline int free_pte(pte_t page)
{
	if (pte_present(page)) {
		unsigned long addr = pte_page(page);
		if (MAP_NR(addr) >= max_mapnr || PageReserved(mem_map+MAP_NR(addr)))
			return 0;
		/* 
		 * free_page() used to be able to clear swap cache
		 * entries.  We may now have to do it manually.  
		 */
		free_page_and_swap_cache(addr);
		return 1;
	}
	swap_free(pte_val(page));
	return 0;
}

static inline void forget_pte(pte_t page)
{
	if (!pte_none(page)) {
		printk("forget_pte: old mapping existed!\n");
		free_pte(page);
	}
}

static inline int zap_pte_range(pmd_t * pmd, unsigned long address, unsigned long size)
{
	pte_t * pte;
	int freed;

	if (pmd_none(*pmd))
		return 0;
	if (pmd_bad(*pmd)) {
		printk("zap_pte_range: bad pmd (%08lx)\n", pmd_val(*pmd));
		pmd_clear(pmd);
		return 0;
	}
	pte = pte_offset(pmd, address);
	address &= ~PMD_MASK;
	if (address + size > PMD_SIZE)
		size = PMD_SIZE - address;
	size >>= PAGE_SHIFT;
	freed = 0;
	for (;;) {
		pte_t page;
		if (!size)
			break;
		page = *pte;
		pte++;
		size--;
		if (pte_none(page))
			continue;
		pte_clear(pte-1);
		freed += free_pte(page);
	}
	return freed;
}

static inline int zap_pmd_range(pgd_t * dir, unsigned long address, unsigned long size)
{
	pmd_t * pmd;
	unsigned long end;
	int freed;

	if (pgd_none(*dir))
		return 0;
	if (pgd_bad(*dir)) {
		printk("zap_pmd_range: bad pgd (%08lx)\n", pgd_val(*dir));
		pgd_clear(dir);
		return 0;
	}
	pmd = pmd_offset(dir, address);
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	freed = 0;
	do {
		freed += zap_pte_range(pmd, address, end - address);
		address = (address + PMD_SIZE) & PMD_MASK; 
		pmd++;
	} while (address < end);
	return freed;
}

/*
 * remove user pages in a given range.
 */
void zap_page_range(struct mm_struct *mm, unsigned long address, unsigned long size)
{
	pgd_t * dir;
	unsigned long end = address + size;
	int freed = 0;

	dir = pgd_offset(mm, address);
	while (address < end) {
		freed += zap_pmd_range(dir, address, end - address);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	/*
	 * Update rss for the mm_struct (not necessarily current->mm)
	 */
	if (mm->rss > 0) {
		mm->rss -= freed;
		if (mm->rss < 0)
			mm->rss = 0;
	}
}

static inline void zeromap_pte_range(pte_t * pte, unsigned long address, unsigned long size, pte_t zero_pte)
{
	unsigned long end;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		pte_t oldpage = *pte;
		set_pte(pte, zero_pte);
		forget_pte(oldpage);
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
}

static inline int zeromap_pmd_range(pmd_t * pmd, unsigned long address, unsigned long size, pte_t zero_pte)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		pte_t * pte = pte_alloc(pmd, address);
		if (!pte)
			return -ENOMEM;
		zeromap_pte_range(pte, address, end - address, zero_pte);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

int zeromap_page_range(unsigned long address, unsigned long size, pgprot_t prot)
{
	int error = 0;
	pgd_t * dir;
	unsigned long beg = address;
	unsigned long end = address + size;
	pte_t zero_pte;

	zero_pte = pte_wrprotect(mk_pte(ZERO_PAGE, prot));
	dir = pgd_offset(current->mm, address);
	flush_cache_range(current->mm, beg, end);
	while (address < end) {
		pmd_t *pmd = pmd_alloc(dir, address);
		error = -ENOMEM;
		if (!pmd)
			break;
		error = zeromap_pmd_range(pmd, address, end - address, zero_pte);
		if (error)
			break;
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	flush_tlb_range(current->mm, beg, end);
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

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		unsigned long mapnr;
		pte_t oldpage = *pte;
		pte_clear(pte);

		mapnr = MAP_NR(__va(phys_addr));
		if (mapnr >= max_mapnr || PageReserved(mem_map+mapnr))
 			set_pte(pte, mk_pte_phys(phys_addr, prot));
		forget_pte(oldpage);
		address += PAGE_SIZE;
		phys_addr += PAGE_SIZE;
		pte++;
	} while (address < end);
}

static inline int remap_pmd_range(pmd_t * pmd, unsigned long address, unsigned long size,
	unsigned long phys_addr, pgprot_t prot)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	phys_addr -= address;
	do {
		pte_t * pte = pte_alloc(pmd, address);
		if (!pte)
			return -ENOMEM;
		remap_pte_range(pte, address, end - address, address + phys_addr, prot);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

int remap_page_range(unsigned long from, unsigned long phys_addr, unsigned long size, pgprot_t prot)
{
	int error = 0;
	pgd_t * dir;
	unsigned long beg = from;
	unsigned long end = from + size;

	phys_addr -= from;
	dir = pgd_offset(current->mm, from);
	flush_cache_range(current->mm, beg, end);
	while (from < end) {
		pmd_t *pmd = pmd_alloc(dir, from);
		error = -ENOMEM;
		if (!pmd)
			break;
		error = remap_pmd_range(pmd, from, end - from, phys_addr + from, prot);
		if (error)
			break;
		from = (from + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	flush_tlb_range(current->mm, beg, end);
	return error;
}

/*
 * sanity-check function..
 */
static void put_page(pte_t * page_table, pte_t pte)
{
	if (!pte_none(*page_table)) {
		free_page_and_swap_cache(pte_page(pte));
		return;
	}
/* no need for flush_tlb */
	set_pte(page_table, pte);
}

/*
 * This routine is used to map in a page into an address space: needed by
 * execve() for the initial stack and environment pages.
 */
unsigned long put_dirty_page(struct task_struct * tsk, unsigned long page, unsigned long address)
{
	pgd_t * pgd;
	pmd_t * pmd;
	pte_t * pte;

	if (MAP_NR(page) >= max_mapnr)
		printk("put_dirty_page: trying to put page %08lx at %08lx\n",page,address);
	if (atomic_read(&mem_map[MAP_NR(page)].count) != 1)
		printk("mem_map disagrees with %08lx at %08lx\n",page,address);
	pgd = pgd_offset(tsk->mm,address);
	pmd = pmd_alloc(pgd, address);
	if (!pmd) {
		free_page(page);
		oom(tsk);
		return 0;
	}
	pte = pte_alloc(pmd, address);
	if (!pte) {
		free_page(page);
		oom(tsk);
		return 0;
	}
	if (!pte_none(*pte)) {
		printk("put_dirty_page: page already exists\n");
		free_page(page);
		return 0;
	}
	flush_page_to_ram(page);
	set_pte(pte, pte_mkwrite(pte_mkdirty(mk_pte(page, PAGE_COPY))));
/* no need for flush_tlb */
	return page;
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
 */
static int do_wp_page(struct task_struct * tsk, struct vm_area_struct * vma,
	unsigned long address, pte_t *page_table)
{
	pte_t pte;
	unsigned long old_page, new_page;
	struct page * page_map;
	
	pte = *page_table;
	new_page = __get_free_page(GFP_USER);
	/* Did someone else copy this page for us while we slept? */
	if (pte_val(*page_table) != pte_val(pte))
		goto end_wp_page;
	if (!pte_present(pte))
		goto end_wp_page;
	if (pte_write(pte))
		goto end_wp_page;
	old_page = pte_page(pte);
	if (MAP_NR(old_page) >= max_mapnr)
		goto bad_wp_page;
	tsk->min_flt++;
	page_map = mem_map + MAP_NR(old_page);
	
	/*
	 * We can avoid the copy if:
	 * - we're the only user (count == 1)
	 * - the only other user is the swap cache,
	 *   and the only swap cache user is itself,
	 *   in which case we can remove the page
	 *   from the swap cache.
	 */
	switch (atomic_read(&page_map->count)) {
	case 2:
		if (!PageSwapCache(page_map))
			break;
		if (swap_count(page_map->offset) != 1)
			break;
		delete_from_swap_cache(page_map);
		/* FallThrough */
	case 1:
		/* We can release the kernel lock now.. */
		unlock_kernel();

		flush_cache_page(vma, address);
		set_pte(page_table, pte_mkdirty(pte_mkwrite(pte)));
		flush_tlb_page(vma, address);
end_wp_page:
		if (new_page)
			free_page(new_page);
		return 1;
	}
		
	unlock_kernel();
	if (!new_page)
		return 0;

	if (PageReserved(mem_map + MAP_NR(old_page)))
		++vma->vm_mm->rss;
	copy_cow_page(old_page,new_page);
	flush_page_to_ram(old_page);
	flush_page_to_ram(new_page);
	flush_cache_page(vma, address);
	set_pte(page_table, pte_mkwrite(pte_mkdirty(mk_pte(new_page, vma->vm_page_prot))));
	free_page(old_page);
	flush_tlb_page(vma, address);
	return 1;

bad_wp_page:
	printk("do_wp_page: bogus page at address %08lx (%08lx)\n",address,old_page);
	send_sig(SIGKILL, tsk, 1);
	if (new_page)
		free_page(new_page);
	return 0;
}

/*
 * This function zeroes out partial mmap'ed pages at truncation time..
 */
static void partial_clear(struct vm_area_struct *vma, unsigned long address)
{
	pgd_t *page_dir;
	pmd_t *page_middle;
	pte_t *page_table, pte;

	page_dir = pgd_offset(vma->vm_mm, address);
	if (pgd_none(*page_dir))
		return;
	if (pgd_bad(*page_dir)) {
		printk("bad page table directory entry %p:[%lx]\n", page_dir, pgd_val(*page_dir));
		pgd_clear(page_dir);
		return;
	}
	page_middle = pmd_offset(page_dir, address);
	if (pmd_none(*page_middle))
		return;
	if (pmd_bad(*page_middle)) {
		printk("bad page table directory entry %p:[%lx]\n", page_dir, pgd_val(*page_dir));
		pmd_clear(page_middle);
		return;
	}
	page_table = pte_offset(page_middle, address);
	pte = *page_table;
	if (!pte_present(pte))
		return;
	flush_cache_page(vma, address);
	address &= ~PAGE_MASK;
	address += pte_page(pte);
	if (MAP_NR(address) >= max_mapnr)
		return;
	memset((void *) address, 0, PAGE_SIZE - (address & ~PAGE_MASK));
	flush_page_to_ram(pte_page(pte));
}

/*
 * Handle all mappings that got truncated by a "truncate()"
 * system call.
 *
 * NOTE! We have to be ready to update the memory sharing
 * between the file and the memory map for a potential last
 * incomplete page.  Ugly, but necessary.
 */
void vmtruncate(struct inode * inode, unsigned long offset)
{
	struct vm_area_struct * mpnt;

	truncate_inode_pages(inode, offset);
	if (!inode->i_mmap)
		return;
	mpnt = inode->i_mmap;
	do {
		struct mm_struct *mm = mpnt->vm_mm;
		unsigned long start = mpnt->vm_start;
		unsigned long end = mpnt->vm_end;
		unsigned long len = end - start;
		unsigned long diff;

		/* mapping wholly truncated? */
		if (mpnt->vm_offset >= offset) {
			flush_cache_range(mm, start, end);
			zap_page_range(mm, start, len);
			flush_tlb_range(mm, start, end);
			continue;
		}
		/* mapping wholly unaffected? */
		diff = offset - mpnt->vm_offset;
		if (diff >= len)
			continue;
		/* Ok, partially affected.. */
		start += diff;
		len = (len - diff) & PAGE_MASK;
		if (start & ~PAGE_MASK) {
			partial_clear(mpnt, start);
			start = (start + ~PAGE_MASK) & PAGE_MASK;
		}
		flush_cache_range(mm, start, end);
		zap_page_range(mm, start, len);
		flush_tlb_range(mm, start, end);
	} while ((mpnt = mpnt->vm_next_share) != NULL);
}


/*
 * This is called with the kernel lock held, we need
 * to return without it.
 */
static int do_swap_page(struct task_struct * tsk, 
	struct vm_area_struct * vma, unsigned long address,
	pte_t * page_table, pte_t entry, int write_access)
{
	if (!vma->vm_ops || !vma->vm_ops->swapin) {
		swap_in(tsk, vma, page_table, pte_val(entry), write_access);
		flush_page_to_ram(pte_page(*page_table));
	} else {
		pte_t page = vma->vm_ops->swapin(vma, address - vma->vm_start + vma->vm_offset, pte_val(entry));
		if (pte_val(*page_table) != pte_val(entry)) {
			free_page(pte_page(page));
		} else {
			if (atomic_read(&mem_map[MAP_NR(pte_page(page))].count) > 1 &&
			    !(vma->vm_flags & VM_SHARED))
				page = pte_wrprotect(page);
			++vma->vm_mm->rss;
			++tsk->maj_flt;
			flush_page_to_ram(pte_page(page));
			set_pte(page_table, page);
		}
	}
	unlock_kernel();
	return 1;
}

/*
 * This only needs the MM semaphore
 */
static int do_anonymous_page(struct task_struct * tsk, struct vm_area_struct * vma, pte_t *page_table, int write_access)
{
	pte_t entry = pte_wrprotect(mk_pte(ZERO_PAGE, vma->vm_page_prot));
	if (write_access) {
		unsigned long page = __get_free_page(GFP_USER);
		if (!page)
			return 0;
		clear_page(page);
		entry = pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
		vma->vm_mm->rss++;
		tsk->min_flt++;
		flush_page_to_ram(page);
	}
	put_page(page_table, entry);
	return 1;
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
 * This is called with the MM semaphore and the kernel lock held.
 * We need to release the kernel lock as soon as possible..
 */
static int do_no_page(struct task_struct * tsk, struct vm_area_struct * vma,
	unsigned long address, int write_access, pte_t *page_table)
{
	unsigned long page;
	pte_t entry;

	if (!vma->vm_ops || !vma->vm_ops->nopage) {
		unlock_kernel();
		return do_anonymous_page(tsk, vma, page_table, write_access);
	}

	/*
	 * The third argument is "no_share", which tells the low-level code
	 * to copy, not share the page even if sharing is possible.  It's
	 * essentially an early COW detection.
	 */
	page = vma->vm_ops->nopage(vma, address & PAGE_MASK,
		(vma->vm_flags & VM_SHARED)?0:write_access);

	unlock_kernel();
	if (!page)
		return 0;

	++tsk->maj_flt;
	++vma->vm_mm->rss;
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
	flush_page_to_ram(page);
	entry = mk_pte(page, vma->vm_page_prot);
	if (write_access) {
		entry = pte_mkwrite(pte_mkdirty(entry));
	} else if (atomic_read(&mem_map[MAP_NR(page)].count) > 1 &&
		   !(vma->vm_flags & VM_SHARED))
		entry = pte_wrprotect(entry);
	put_page(page_table, entry);
	/* no need to invalidate: a not-present page shouldn't be cached */
	return 1;
}

/*
 * These routines also need to handle stuff like marking pages dirty
 * and/or accessed for architectures that don't do it in hardware (most
 * RISC architectures).  The early dirtying is also good on the i386.
 *
 * There is also a hook called "update_mmu_cache()" that architectures
 * with external mmu caches can use to update those (ie the Sparc or
 * PowerPC hashed page tables that act as extended TLBs).
 */
static inline int handle_pte_fault(struct task_struct *tsk,
	struct vm_area_struct * vma, unsigned long address,
	int write_access, pte_t * pte)
{
	pte_t entry;

	lock_kernel();
	entry = *pte;

	if (!pte_present(entry)) {
		if (pte_none(entry))
			return do_no_page(tsk, vma, address, write_access, pte);
		return do_swap_page(tsk, vma, address, pte, entry, write_access);
	}

	entry = pte_mkyoung(entry);
	set_pte(pte, entry);
	flush_tlb_page(vma, address);
	if (write_access) {
		if (!pte_write(entry))
			return do_wp_page(tsk, vma, address, pte);

		entry = pte_mkdirty(entry);
		set_pte(pte, entry);
		flush_tlb_page(vma, address);
	}
	unlock_kernel();
	return 1;
}

/*
 * By the time we get here, we already hold the mm semaphore
 */
int handle_mm_fault(struct task_struct *tsk, struct vm_area_struct * vma,
	unsigned long address, int write_access)
{
	pgd_t *pgd;
	pmd_t *pmd;

	pgd = pgd_offset(vma->vm_mm, address);
	pmd = pmd_alloc(pgd, address);
	if (pmd) {
		pte_t * pte = pte_alloc(pmd, address);
		if (pte) {
			if (handle_pte_fault(tsk, vma, address, write_access, pte)) {
				update_mmu_cache(vma, address, *pte);
				return 1;
			}
		}
	}
	return 0;
}

/*
 * Simplistic page force-in..
 */
void make_pages_present(unsigned long addr, unsigned long end)
{
	int write;
	struct vm_area_struct * vma;

	vma = find_vma(current->mm, addr);
	write = (vma->vm_flags & VM_WRITE) != 0;
	while (addr < end) {
		handle_mm_fault(current, vma, addr, write);
		addr += PAGE_SIZE;
	}
}

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

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/string.h>

unsigned long high_memory = 0;

/*
 * We special-case the C-O-W ZERO_PAGE, because it's such
 * a common occurrence (no need to read the page to know
 * that it's zero - better for the cache and memory subsystem).
 */
static inline void copy_page(unsigned long from, unsigned long to)
{
	if (from == ZERO_PAGE) {
		memset((void *) to, 0, PAGE_SIZE);
		return;
	}
	memcpy((void *) to, (void *) from, PAGE_SIZE);
}

#define USER_PTRS_PER_PGD (TASK_SIZE / PGDIR_SIZE)

mem_map_t * mem_map = NULL;

/*
 * oom() prints a message (so that the user knows why the process died),
 * and gives the process an untrappable SIGKILL.
 */
void oom(struct task_struct * task)
{
	printk("\nOut of memory for %s.\n", task->comm);
	task->sig->action[SIGKILL-1].sa_handler = NULL;
	task->blocked &= ~(1<<(SIGKILL-1));
	send_sig(SIGKILL,task,1);
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
	
/*
 * This function clears all user-level page tables of a process - this
 * is needed by execve(), so that old pages aren't in the way.
 */
void clear_page_tables(struct task_struct * tsk)
{
	int i;
	pgd_t * page_dir;

	page_dir = tsk->mm->pgd;
	if (!page_dir || page_dir == swapper_pg_dir) {
		printk("%s trying to clear kernel page-directory: not good\n", tsk->comm);
		return;
	}
	flush_cache_mm(tsk->mm);
	for (i = 0 ; i < USER_PTRS_PER_PGD ; i++)
		free_one_pgd(page_dir + i);
	flush_tlb_mm(tsk->mm);
}

/*
 * This function frees up all page tables of a process when it exits. It
 * is the same as "clear_page_tables()", except it also changes the process'
 * page table directory to the kernel page tables and then frees the old
 * page table directory.
 */
void free_page_tables(struct mm_struct * mm)
{
	int i;
	pgd_t * page_dir;

	page_dir = mm->pgd;
	if (!page_dir || page_dir == swapper_pg_dir) {
		printk("Trying to free kernel page-directory: not good\n");
		return;
	}
	for (i = 0 ; i < USER_PTRS_PER_PGD ; i++)
		free_one_pgd(page_dir + i);
	pgd_free(page_dir);
}

int new_page_tables(struct task_struct * tsk)
{
	pgd_t * page_dir, * new_pg;

	if (!(new_pg = pgd_alloc()))
		return -ENOMEM;
	page_dir = pgd_offset(&init_mm, 0);
	flush_cache_mm(tsk->mm);
	memcpy(new_pg + USER_PTRS_PER_PGD, page_dir + USER_PTRS_PER_PGD,
	       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof (pgd_t));
	flush_tlb_mm(tsk->mm);
	SET_PAGE_DIR(tsk, new_pg);
	tsk->mm->pgd = new_pg;
	return 0;
}

static inline void copy_one_pte(pte_t * old_pte, pte_t * new_pte, int cow)
{
	pte_t pte = *old_pte;
	unsigned long page_nr;

	if (pte_none(pte))
		return;
	if (!pte_present(pte)) {
		swap_duplicate(pte_val(pte));
		set_pte(new_pte, pte);
		return;
	}
	page_nr = MAP_NR(pte_page(pte));
	if (page_nr >= MAP_NR(high_memory) || PageReserved(mem_map+page_nr)) {
		set_pte(new_pte, pte);
		return;
	}
	if (cow)
		pte = pte_wrprotect(pte);
	if (delete_from_swap_cache(page_nr))
		pte = pte_mkdirty(pte);
	set_pte(new_pte, pte_mkold(pte));
	set_pte(old_pte, pte);
	mem_map[page_nr].count++;
}

static inline int copy_pte_range(pmd_t *dst_pmd, pmd_t *src_pmd, unsigned long address, unsigned long size, int cow)
{
	pte_t * src_pte, * dst_pte;
	unsigned long end;

	if (pmd_none(*src_pmd))
		return 0;
	if (pmd_bad(*src_pmd)) {
		printk("copy_pte_range: bad pmd (%08lx)\n", pmd_val(*src_pmd));
		pmd_clear(src_pmd);
		return 0;
	}
	src_pte = pte_offset(src_pmd, address);
	if (pmd_none(*dst_pmd)) {
		if (!pte_alloc(dst_pmd, 0))
			return -ENOMEM;
	}
	dst_pte = pte_offset(dst_pmd, address);
	address &= ~PMD_MASK;
	end = address + size;
	if (end >= PMD_SIZE)
		end = PMD_SIZE;
	do {
		/* I would like to switch arguments here, to make it
		 * consistent with copy_xxx_range and memcpy syntax.
		 */
		copy_one_pte(src_pte++, dst_pte++, cow);
		address += PAGE_SIZE;
	} while (address < end);
	return 0;
}

static inline int copy_pmd_range(pgd_t *dst_pgd, pgd_t *src_pgd, unsigned long address, unsigned long size, int cow)
{
	pmd_t * src_pmd, * dst_pmd;
	unsigned long end;
	int error = 0;

	if (pgd_none(*src_pgd))
		return 0;
	if (pgd_bad(*src_pgd)) {
		printk("copy_pmd_range: bad pgd (%08lx)\n", pgd_val(*src_pgd));
		pgd_clear(src_pgd);
		return 0;
	}
	src_pmd = pmd_offset(src_pgd, address);
	if (pgd_none(*dst_pgd)) {
		if (!pmd_alloc(dst_pgd, 0))
			return -ENOMEM;
	}
	dst_pmd = pmd_offset(dst_pgd, address);
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		error = copy_pte_range(dst_pmd++, src_pmd++, address, end - address, cow);
		if (error)
			break;
		address = (address + PMD_SIZE) & PMD_MASK; 
	} while (address < end);
	return error;
}

/*
 * copy one vm_area from one task to the other. Assumes the page tables
 * already present in the new task to be cleared in the whole range
 * covered by this vma.
 */
int copy_page_range(struct mm_struct *dst, struct mm_struct *src,
			struct vm_area_struct *vma)
{
	pgd_t * src_pgd, * dst_pgd;
	unsigned long address = vma->vm_start;
	unsigned long end = vma->vm_end;
	int error = 0, cow;

	cow = (vma->vm_flags & (VM_SHARED | VM_WRITE)) == VM_WRITE;
	src_pgd = pgd_offset(src, address);
	dst_pgd = pgd_offset(dst, address);
	flush_cache_range(src, vma->vm_start, vma->vm_end);
	flush_cache_range(dst, vma->vm_start, vma->vm_end);
	while (address < end) {
		error = copy_pmd_range(dst_pgd++, src_pgd++, address, end - address, cow);
		if (error)
			break;
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
	}
	/* Note that the src ptes get c-o-w treatment, so they change too. */
	flush_tlb_range(src, vma->vm_start, vma->vm_end);
	flush_tlb_range(dst, vma->vm_start, vma->vm_end);
	return error;
}

static inline void forget_pte(pte_t page)
{
	if (pte_none(page))
		return;
	if (pte_present(page)) {
		unsigned long addr = pte_page(page);
		if (addr >= high_memory || PageReserved(mem_map+MAP_NR(addr)))
			return;
		free_page(addr);
		if (current->mm->rss <= 0)
			return;
		current->mm->rss--;
		return;
	}
	swap_free(pte_val(page));
}

static inline void zap_pte_range(pmd_t * pmd, unsigned long address, unsigned long size)
{
	pte_t * pte;
	unsigned long end;

	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd)) {
		printk("zap_pte_range: bad pmd (%08lx)\n", pmd_val(*pmd));
		pmd_clear(pmd);
		return;
	}
	pte = pte_offset(pmd, address);
	address &= ~PMD_MASK;
	end = address + size;
	if (end >= PMD_SIZE)
		end = PMD_SIZE;
	do {
		pte_t page = *pte;
		pte_clear(pte);
		forget_pte(page);
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
}

static inline void zap_pmd_range(pgd_t * dir, unsigned long address, unsigned long size)
{
	pmd_t * pmd;
	unsigned long end;

	if (pgd_none(*dir))
		return;
	if (pgd_bad(*dir)) {
		printk("zap_pmd_range: bad pgd (%08lx)\n", pgd_val(*dir));
		pgd_clear(dir);
		return;
	}
	pmd = pmd_offset(dir, address);
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		zap_pte_range(pmd, address, end - address);
		address = (address + PMD_SIZE) & PMD_MASK; 
		pmd++;
	} while (address < end);
}

/*
 * remove user pages in a given range.
 */
int zap_page_range(struct mm_struct *mm, unsigned long address, unsigned long size)
{
	pgd_t * dir;
	unsigned long end = address + size;

	dir = pgd_offset(mm, address);
	flush_cache_range(mm, end - size, end);
	while (address < end) {
		zap_pmd_range(dir, address, end - address);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	flush_tlb_range(mm, end - size, end);
	return 0;
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
	unsigned long offset, pgprot_t prot)
{
	unsigned long end;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		pte_t oldpage = *pte;
		pte_clear(pte);
		if (offset >= high_memory || PageReserved(mem_map+MAP_NR(offset)))
 			set_pte(pte, mk_pte(offset, prot));
		forget_pte(oldpage);
		address += PAGE_SIZE;
		offset += PAGE_SIZE;
		pte++;
	} while (address < end);
}

static inline int remap_pmd_range(pmd_t * pmd, unsigned long address, unsigned long size,
	unsigned long offset, pgprot_t prot)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	offset -= address;
	do {
		pte_t * pte = pte_alloc(pmd, address);
		if (!pte)
			return -ENOMEM;
		remap_pte_range(pte, address, end - address, address + offset, prot);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

int remap_page_range(unsigned long from, unsigned long offset, unsigned long size, pgprot_t prot)
{
	int error = 0;
	pgd_t * dir;
	unsigned long beg = from;
	unsigned long end = from + size;

	offset -= from;
	dir = pgd_offset(current->mm, from);
	flush_cache_range(current->mm, beg, end);
	while (from < end) {
		pmd_t *pmd = pmd_alloc(dir, from);
		error = -ENOMEM;
		if (!pmd)
			break;
		error = remap_pmd_range(pmd, from, end - from, offset + from, prot);
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
		printk("put_page: page already exists %08lx\n", pte_val(*page_table));
		free_page(pte_page(pte));
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

	if (page >= high_memory)
		printk("put_dirty_page: trying to put page %08lx at %08lx\n",page,address);
	if (mem_map[MAP_NR(page)].count != 1)
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
/* no need for invalidate */
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
void do_wp_page(struct task_struct * tsk, struct vm_area_struct * vma,
	unsigned long address, int write_access)
{
	pgd_t *page_dir;
	pmd_t *page_middle;
	pte_t *page_table, pte;
	unsigned long old_page, new_page;

	new_page = __get_free_page(GFP_KERNEL);
	page_dir = pgd_offset(vma->vm_mm, address);
	if (pgd_none(*page_dir))
		goto end_wp_page;
	if (pgd_bad(*page_dir))
		goto bad_wp_pagedir;
	page_middle = pmd_offset(page_dir, address);
	if (pmd_none(*page_middle))
		goto end_wp_page;
	if (pmd_bad(*page_middle))
		goto bad_wp_pagemiddle;
	page_table = pte_offset(page_middle, address);
	pte = *page_table;
	if (!pte_present(pte))
		goto end_wp_page;
	if (pte_write(pte))
		goto end_wp_page;
	old_page = pte_page(pte);
	if (old_page >= high_memory)
		goto bad_wp_page;
	tsk->min_flt++;
	/*
	 * Do we need to copy?
	 */
	if (mem_map[MAP_NR(old_page)].count != 1) {
		if (new_page) {
			if (PageReserved(mem_map + MAP_NR(old_page)))
				++vma->vm_mm->rss;
			copy_page(old_page,new_page);
			flush_page_to_ram(old_page);
			flush_page_to_ram(new_page);
			flush_cache_page(vma, address);
			set_pte(page_table, pte_mkwrite(pte_mkdirty(mk_pte(new_page, vma->vm_page_prot))));
			free_page(old_page);
			flush_tlb_page(vma, address);
			return;
		}
		flush_cache_page(vma, address);
		set_pte(page_table, BAD_PAGE);
		flush_tlb_page(vma, address);
		free_page(old_page);
		oom(tsk);
		return;
	}
	flush_cache_page(vma, address);
	set_pte(page_table, pte_mkdirty(pte_mkwrite(pte)));
	flush_tlb_page(vma, address);
	if (new_page)
		free_page(new_page);
	return;
bad_wp_page:
	printk("do_wp_page: bogus page at address %08lx (%08lx)\n",address,old_page);
	send_sig(SIGKILL, tsk, 1);
	goto end_wp_page;
bad_wp_pagemiddle:
	printk("do_wp_page: bogus page-middle at address %08lx (%08lx)\n", address, pmd_val(*page_middle));
	send_sig(SIGKILL, tsk, 1);
	goto end_wp_page;
bad_wp_pagedir:
	printk("do_wp_page: bogus page-dir entry at address %08lx (%08lx)\n", address, pgd_val(*page_dir));
	send_sig(SIGKILL, tsk, 1);
end_wp_page:
	if (new_page)
		free_page(new_page);
	return;
}

/*
 * Ugly, ugly, but the goto's result in better assembly..
 */
int verify_area(int type, const void * addr, unsigned long size)
{
	struct vm_area_struct * vma;
	unsigned long start = (unsigned long) addr;

	/* If the current user space is mapped to kernel space (for the
	 * case where we use a fake user buffer with get_fs/set_fs()) we
	 * don't expect to find the address in the user vm map.
	 */
	if (!size || get_fs() == KERNEL_DS)
		return 0;

	vma = find_vma(current, start);
	if (!vma)
		goto bad_area;
	if (vma->vm_start > start)
		goto check_stack;

good_area:
	if (type == VERIFY_WRITE)
		goto check_write;
	for (;;) {
		struct vm_area_struct * next;
		if (!(vma->vm_flags & VM_READ))
			goto bad_area;
		if (vma->vm_end - start >= size)
			return 0;
		next = vma->vm_next;
		if (!next || vma->vm_end != next->vm_start)
			goto bad_area;
		vma = next;
	}

check_write:
	if (!(vma->vm_flags & VM_WRITE))
		goto bad_area;
	if (!wp_works_ok)
		goto check_wp_fault_by_hand;
	for (;;) {
		if (vma->vm_end - start >= size)
			break;
		if (!vma->vm_next || vma->vm_end != vma->vm_next->vm_start)
			goto bad_area;
		vma = vma->vm_next;
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	}
	return 0;

check_wp_fault_by_hand:
	size--;
	size += start & ~PAGE_MASK;
	size >>= PAGE_SHIFT;
	start &= PAGE_MASK;

	for (;;) {
		do_wp_page(current, vma, start, 1);
		if (!size)
			break;
		size--;
		start += PAGE_SIZE;
		if (start < vma->vm_end)
			continue;
		vma = vma->vm_next;
		if (!vma || vma->vm_start != start)
			goto bad_area;
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;;
	}
	return 0;

check_stack:
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (expand_stack(vma, start) == 0)
		goto good_area;

bad_area:
	return -EFAULT;
}

static inline void get_empty_page(struct task_struct * tsk, struct vm_area_struct * vma,
	pte_t * page_table, int write_access)
{
	pte_t pte;

	pte = pte_wrprotect(mk_pte(ZERO_PAGE, vma->vm_page_prot));
	if (write_access) {
		unsigned long page = get_free_page(GFP_KERNEL);
		pte = pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
		vma->vm_mm->rss++;
		tsk->min_flt++;
		if (!page) {
			oom(tsk);
			pte = BAD_PAGE;
		}
		flush_page_to_ram(page);
	}
	put_page(page_table, pte);
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
	if (address >= high_memory)
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
		unsigned long start = mpnt->vm_start;
		unsigned long len = mpnt->vm_end - start;
		unsigned long diff;

		/* mapping wholly truncated? */
		if (mpnt->vm_offset >= offset) {
			zap_page_range(mpnt->vm_mm, start, len);
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
		zap_page_range(mpnt->vm_mm, start, len);
	} while ((mpnt = mpnt->vm_next_share) != inode->i_mmap);
}

/*
 * fill in an empty page-table if none exists.
 */
static inline pte_t * get_empty_pgtable(struct task_struct * tsk,unsigned long address)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset(tsk->mm, address);
	pmd = pmd_alloc(pgd, address);
	if (!pmd) {
		oom(tsk);
		return NULL;
	}
	pte = pte_alloc(pmd, address);
	if (!pte) {
		oom(tsk);
		return NULL;
	}
	return pte;
}

static inline void do_swap_page(struct task_struct * tsk, 
	struct vm_area_struct * vma, unsigned long address,
	pte_t * page_table, pte_t entry, int write_access)
{
	pte_t page;

	if (!vma->vm_ops || !vma->vm_ops->swapin) {
		swap_in(tsk, vma, page_table, pte_val(entry), write_access);
		flush_page_to_ram(pte_page(*page_table));
		return;
	}
	page = vma->vm_ops->swapin(vma, address - vma->vm_start + vma->vm_offset, pte_val(entry));
	if (pte_val(*page_table) != pte_val(entry)) {
		free_page(pte_page(page));
		return;
	}
	if (mem_map[MAP_NR(pte_page(page))].count > 1 && !(vma->vm_flags & VM_SHARED))
		page = pte_wrprotect(page);
	++vma->vm_mm->rss;
	++tsk->maj_flt;
	flush_page_to_ram(pte_page(page));
	set_pte(page_table, page);
	return;
}

/*
 * do_no_page() tries to create a new page mapping. It aggressively
 * tries to share with existing pages, but makes a separate copy if
 * the "write_access" parameter is true in order to avoid the next
 * page fault.
 */
void do_no_page(struct task_struct * tsk, struct vm_area_struct * vma,
	unsigned long address, int write_access)
{
	pte_t * page_table;
	pte_t entry;
	unsigned long page;

	page_table = get_empty_pgtable(tsk, address);
	if (!page_table)
		return;
	entry = *page_table;
	if (pte_present(entry))
		return;
	if (!pte_none(entry)) {
		do_swap_page(tsk, vma, address, page_table, entry, write_access);
		return;
	}
	address &= PAGE_MASK;
	if (!vma->vm_ops || !vma->vm_ops->nopage) {
		flush_cache_page(vma, address);
		get_empty_page(tsk, vma, page_table, write_access);
		return;
	}
	++tsk->maj_flt;
	++vma->vm_mm->rss;
	/*
	 * The third argument is "no_share", which tells the low-level code
	 * to copy, not share the page even if sharing is possible.  It's
	 * essentially an early COW detection 
	 */
	page = vma->vm_ops->nopage(vma, address, write_access && !(vma->vm_flags & VM_SHARED));
	if (!page) {
		force_sig(SIGBUS, current);
		flush_cache_page(vma, address);
		put_page(page_table, BAD_PAGE);
		flush_tlb_page(vma, address);
		return;
	}
	/*
	 * This silly early PAGE_DIRTY setting removes a race
	 * due to the bad i386 page protection. But it's valid
	 * for other architectures too.
	 *
	 * Note that if write_access is true, we either now have
	 * a exclusive copy of the page, or this is a shared mapping,
	 * so we can make it writable and dirty to avoid having to
	 * handle that later.
	 */
	flush_page_to_ram(page);
	entry = mk_pte(page, vma->vm_page_prot);
	if (write_access) {
		entry = pte_mkwrite(pte_mkdirty(entry));
	} else if (mem_map[MAP_NR(page)].count > 1 && !(vma->vm_flags & VM_SHARED))
		entry = pte_wrprotect(entry);
	flush_cache_page(vma, address);
	put_page(page_table, entry);
	flush_tlb_page(vma, address);
}

/*
 * The above separate functions for the no-page and wp-page
 * cases will go away (they mostly do the same thing anyway),
 * and we'll instead use only a general "handle_mm_fault()".
 *
 * These routines also need to handle stuff like marking pages dirty
 * and/or accessed for architectures that don't do it in hardware (most
 * RISC architectures).  The early dirtying is also good on the i386.
 *
 * There is also a hook called "update_mmu_cache()" that architectures
 * with external mmu caches can use to update those (ie the Sparc or
 * PowerPC hashed page tables that act as extended TLBs).
 */
static inline void handle_pte_fault(struct vm_area_struct * vma, unsigned long address,
	int write_access, pte_t * pte)
{
	if (!pte_present(*pte)) {
		do_no_page(current, vma, address, write_access);
		return;
	}
	set_pte(pte, pte_mkyoung(*pte));
	flush_tlb_page(vma, address);
	if (!write_access)
		return;
	if (pte_write(*pte)) {
		set_pte(pte, pte_mkdirty(*pte));
		flush_tlb_page(vma, address);
		return;
	}
	do_wp_page(current, vma, address, write_access);
}

void handle_mm_fault(struct vm_area_struct * vma, unsigned long address,
	int write_access)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset(vma->vm_mm, address);
	pmd = pmd_alloc(pgd, address);
	if (!pmd)
		goto no_memory;
	pte = pte_alloc(pmd, address);
	if (!pte)
		goto no_memory;
	handle_pte_fault(vma, address, write_access, pte);
	update_mmu_cache(vma, address, *pte);
	return;
no_memory:
	oom(current);
}

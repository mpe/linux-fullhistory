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

#include <linux/config.h>
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

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/pgtable.h>

unsigned long high_memory = 0;

/*
 * The free_area_list arrays point to the queue heads of the free areas
 * of different sizes
 */
int nr_swap_pages = 0;
int nr_free_pages = 0;
struct mem_list free_area_list[NR_MEM_LISTS];
unsigned char * free_area_map[NR_MEM_LISTS];

#define copy_page(from,to) memcpy((void *) to, (void *) from, PAGE_SIZE)

mem_map_t * mem_map = NULL;

#define CODE_SPACE(addr,p) ((addr) < (p)->end_code)

/*
 * oom() prints a message (so that the user knows why the process died),
 * and gives the process an untrappable SIGKILL.
 */
void oom(struct task_struct * task)
{
	printk("\nOut of memory for %s.\n", current->comm);
	task->sigaction[SIGKILL-1].sa_handler = NULL;
	task->blocked &= ~(1<<(SIGKILL-1));
	send_sig(SIGKILL,task,1);
}

static inline void free_one_pte(pte_t * page_table)
{
	pte_t page = *page_table;

	if (pte_none(page))
		return;
	pte_clear(page_table);
	if (!pte_present(page)) {
		swap_free(pte_val(page));
		return;
	}
	free_page(pte_page(page));
	return;
}

static void free_one_table(pgd_t * page_dir)
{
	int j;
	pgd_t pg_table = *page_dir;
	pte_t * page_table;
	unsigned long page;

	if (pgd_none(pg_table))
		return;
	pgd_clear(page_dir);
	if (pgd_bad(pg_table)) {
		printk("Bad page table: [%p]=%08lx\n",page_dir,pgd_val(pg_table));
		return;
	}
	page = pgd_page(pg_table);
	if (mem_map[MAP_NR(page)] & MAP_PAGE_RESERVED)
		return;
	page_table = (pte_t *) page;
	for (j = 0 ; j < PTRS_PER_PAGE ; j++,page_table++)
		free_one_pte(page_table);
	free_page(page);
}

/*
 * This function clears all user-level page tables of a process - this
 * is needed by execve(), so that old pages aren't in the way. Note that
 * unlike 'free_page_tables()', this function still leaves a valid
 * page-table-tree in memory: it just removes the user pages. The two
 * functions are similar, but there is a fundamental difference.
 */
void clear_page_tables(struct task_struct * tsk)
{
	int i;
	pgd_t * page_dir;

	if (!tsk)
		return;
	if (tsk == task[0])
		panic("task[0] (swapper) doesn't support exec()\n");
	page_dir = PAGE_DIR_OFFSET(tsk, 0);
	if (!page_dir || page_dir == swapper_pg_dir) {
		printk("Trying to clear kernel page-directory: not good\n");
		return;
	}
	if (mem_map[MAP_NR((unsigned long) page_dir)] > 1) {
		pgd_t * new_pg;

		if (!(new_pg = (pgd_t *) get_free_page(GFP_KERNEL))) {
			oom(tsk);
			return;
		}
		for (i = 768 ; i < 1024 ; i++)
			new_pg[i] = page_dir[i];
		free_page((unsigned long) page_dir);
		SET_PAGE_DIR(tsk, new_pg);
		return;
	}
	for (i = 0 ; i < 768 ; i++,page_dir++)
		free_one_table(page_dir);
	invalidate();
	return;
}

/*
 * This function frees up all page tables of a process when it exits.
 */
void free_page_tables(struct task_struct * tsk)
{
	int i;
	pgd_t * page_dir;

	if (!tsk)
		return;
	if (tsk == task[0]) {
		printk("task[0] (swapper) killed: unable to recover\n");
		panic("Trying to free up swapper memory space");
	}
	page_dir = PAGE_DIR_OFFSET(tsk, 0);
	if (!page_dir || page_dir == swapper_pg_dir) {
		printk("Trying to free kernel page-directory: not good\n");
		return;
	}
	SET_PAGE_DIR(tsk, swapper_pg_dir);
	if (mem_map[MAP_NR((unsigned long) page_dir)] > 1) {
		free_page((unsigned long) page_dir);
		return;
	}
	for (i = 0 ; i < PTRS_PER_PAGE ; i++)
		free_one_table(page_dir + i);
	free_page((unsigned long) page_dir);
	invalidate();
}

/*
 * clone_page_tables() clones the page table for a process - both
 * processes will have the exact same pages in memory. There are
 * probably races in the memory management with cloning, but we'll
 * see..
 */
int clone_page_tables(struct task_struct * tsk)
{
	pgd_t * pg_dir;

	pg_dir = PAGE_DIR_OFFSET(current, 0);
	mem_map[MAP_NR((unsigned long) pg_dir)]++;
	SET_PAGE_DIR(tsk, pg_dir);
	return 0;
}

/*
 * copy_page_tables() just copies the whole process memory range:
 * note the special handling of RESERVED (ie kernel) pages, which
 * means that they are always shared by all processes.
 */
int copy_page_tables(struct task_struct * tsk)
{
	int i;
	pgd_t *old_page_dir;
	pgd_t *new_page_dir;

	new_page_dir = (pgd_t *) get_free_page(GFP_KERNEL);
	if (!new_page_dir)
		return -ENOMEM;
	old_page_dir = PAGE_DIR_OFFSET(current, 0);
	SET_PAGE_DIR(tsk, new_page_dir);
	for (i = 0 ; i < PTRS_PER_PAGE ; i++,old_page_dir++,new_page_dir++) {
		int j;
		pgd_t old_pg_table;
		pte_t *old_page_table, *new_page_table;

		old_pg_table = *old_page_dir;
		if (pgd_none(old_pg_table))
			continue;
		if (pgd_bad(old_pg_table)) {
			printk("copy_page_tables: bad page table: "
				"probable memory corruption\n");
			pgd_clear(old_page_dir);
			continue;
		}
		if (mem_map[MAP_NR(pgd_page(old_pg_table))] & MAP_PAGE_RESERVED) {
			*new_page_dir = old_pg_table;
			continue;
		}
		if (!(new_page_table = (pte_t *) get_free_page(GFP_KERNEL))) {
			free_page_tables(tsk);
			return -ENOMEM;
		}
		old_page_table = (pte_t *) pgd_page(old_pg_table);
		pgd_set(new_page_dir, new_page_table);
		for (j = 0 ; j < PTRS_PER_PAGE ; j++,old_page_table++,new_page_table++) {
			pte_t pte = *old_page_table;
			if (pte_none(pte))
				continue;
			if (!pte_present(pte)) {
				swap_duplicate(pte_val(pte));
				*new_page_table = pte;
				continue;
			}
			if (pte_page(pte) > high_memory || (mem_map[MAP_NR(pte_page(pte))] & MAP_PAGE_RESERVED)) {
				*new_page_table = pte;
				continue;
			}
			if (pte_cow(pte))
				pte = pte_wrprotect(pte);
			if (delete_from_swap_cache(pte_page(pte)))
				pte = pte_mkdirty(pte);
			*new_page_table = pte;
			*old_page_table = pte;
			mem_map[MAP_NR(pte_page(pte))]++;
		}
	}
	invalidate();
	return 0;
}

/*
 * a more complete version of free_page_tables which performs with page
 * granularity.
 */
int unmap_page_range(unsigned long from, unsigned long size)
{
	pgd_t page_dir, * dir;
	pte_t page, * page_table;
	unsigned long poff, pcnt, pc;

	if (from & ~PAGE_MASK) {
		printk("unmap_page_range called with wrong alignment\n");
		return -EINVAL;
	}
	size = (size + ~PAGE_MASK) >> PAGE_SHIFT;
	dir = PAGE_DIR_OFFSET(current,from);
	poff = (from >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	if ((pcnt = PTRS_PER_PAGE - poff) > size)
		pcnt = size;

	for ( ; size > 0; ++dir, size -= pcnt,
	     pcnt = (size > PTRS_PER_PAGE ? PTRS_PER_PAGE : size)) {
	     	page_dir = *dir;
	     	if (pgd_none(page_dir)) {
			poff = 0;
			continue;
		}
		if (pgd_bad(page_dir)) {
			printk("unmap_page_range: bad page directory.");
			continue;
		}
		page_table = (pte_t *) pgd_page(page_dir);
		if (poff) {
			page_table += poff;
			poff = 0;
		}
		for (pc = pcnt; pc--; page_table++) {
			page = *page_table;
			if (!pte_none(page)) {
				pte_clear(page_table);
				if (pte_present(page)) {
					if (!(mem_map[MAP_NR(pte_page(page))] & MAP_PAGE_RESERVED))
						if (current->mm->rss > 0)
							--current->mm->rss;
					free_page(pte_page(page));
				} else
					swap_free(pte_val(page));
			}
		}
		if (pcnt == PTRS_PER_PAGE) {
			pgd_clear(dir);
			free_page(pgd_page(page_dir));
		}
	}
	invalidate();
	return 0;
}

int zeromap_page_range(unsigned long from, unsigned long size, pgprot_t prot)
{
	pgd_t * dir;
	pte_t * page_table;
	unsigned long poff, pcnt;
	pte_t zero_pte;

	if (from & ~PAGE_MASK) {
		printk("zeromap_page_range: from = %08lx\n",from);
		return -EINVAL;
	}
	zero_pte = pte_wrprotect(mk_pte(ZERO_PAGE, prot));
	dir = PAGE_DIR_OFFSET(current,from);
	size = (size + ~PAGE_MASK) >> PAGE_SHIFT;
	poff = (from >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	if ((pcnt = PTRS_PER_PAGE - poff) > size)
		pcnt = size;

	while (size > 0) {
		if (!pgd_present(*dir)) {
			if (!(page_table = (pte_t *) get_free_page(GFP_KERNEL))) {
				invalidate();
				return -ENOMEM;
			}
			if (pgd_present(*dir)) {
				free_page((unsigned long) page_table);
				page_table = (pte_t *) pgd_page(*dir);
			} else
				pgd_set(dir, page_table);
		} else
			page_table = (pte_t *) pgd_page(*dir);
		dir++;
		page_table += poff;
		poff = 0;
		for (size -= pcnt; pcnt-- ;) {
			pte_t page = *page_table;
			if (!pte_none(page)) {
				pte_clear(page_table);
				if (pte_present(page)) {
					if (!(mem_map[MAP_NR(pte_page(page))] & MAP_PAGE_RESERVED))
						if (current->mm->rss > 0)
							--current->mm->rss;
					free_page(pte_page(page));
				} else
					swap_free(pte_val(page));
			}
			*page_table++ = zero_pte;
		}
		pcnt = (size > PTRS_PER_PAGE ? PTRS_PER_PAGE : size);
	}
	invalidate();
	return 0;
}

/*
 * maps a range of physical memory into the requested pages. the old
 * mappings are removed. any references to nonexistent pages results
 * in null mappings (currently treated as "copy-on-access")
 */
int remap_page_range(unsigned long from, unsigned long to, unsigned long size, pgprot_t prot)
{
	pgd_t * dir;
	pte_t * page_table;
	unsigned long poff, pcnt;

	if ((from & ~PAGE_MASK) || (to & ~PAGE_MASK)) {
		printk("remap_page_range: from = %08lx, to=%08lx\n",from,to);
		return -EINVAL;
	}
	dir = PAGE_DIR_OFFSET(current,from);
	size = (size + ~PAGE_MASK) >> PAGE_SHIFT;
	poff = (from >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	if ((pcnt = PTRS_PER_PAGE - poff) > size)
		pcnt = size;

	while (size > 0) {
		if (!pgd_present(*dir)) {
			if (!(page_table = (pte_t *) get_free_page(GFP_KERNEL))) {
				invalidate();
				return -1;
			}
			if (pgd_present(*dir)) {
				free_page((unsigned long) page_table);
				page_table = (pte_t *) pgd_page(*dir);
			} else
				pgd_set(dir, page_table);
		} else
			page_table = (pte_t *) pgd_page(*dir);
		dir++;
		page_table += poff;
		poff = 0;

		for (size -= pcnt; pcnt-- ;) {
			pte_t page = *page_table;
			if (!pte_none(page)) {
				pte_clear(page_table);
				if (pte_present(page)) {
					if (!(mem_map[MAP_NR(pte_page(page))] & MAP_PAGE_RESERVED))
						if (current->mm->rss > 0)
							--current->mm->rss;
					free_page(pte_page(page));
				} else
					swap_free(pte_val(page));
			}
			if (to >= high_memory)
				*page_table = mk_pte(to, prot);
			else if (mem_map[MAP_NR(to)]) {
				*page_table = mk_pte(to, prot);
				if (!(mem_map[MAP_NR(to)] & MAP_PAGE_RESERVED)) {
					++current->mm->rss;
					mem_map[MAP_NR(to)]++;
				}
			}
			page_table++;
			to += PAGE_SIZE;
		}
		pcnt = (size > PTRS_PER_PAGE ? PTRS_PER_PAGE : size);
	}
	invalidate();
	return 0;
}

/*
 * sanity-check function..
 */
static void put_page(pte_t * page_table, pte_t pte)
{
	if (!pte_none(*page_table)) {
		printk("put_page: page already exists\n");
		free_page(pte_page(pte));
		return;
	}
/* no need for invalidate */
	*page_table = pte;
}

/*
 * This routine is used to map in a page into an address space: needed by
 * execve() for the initial stack and environment pages.
 */
unsigned long put_dirty_page(struct task_struct * tsk, unsigned long page, unsigned long address)
{
	pgd_t * page_dir;
	pte_t * page_table;

	if (page >= high_memory)
		printk("put_dirty_page: trying to put page %08lx at %08lx\n",page,address);
	if (mem_map[MAP_NR(page)] != 1)
		printk("mem_map disagrees with %08lx at %08lx\n",page,address);
	page_dir = PAGE_DIR_OFFSET(tsk,address);
	if (pgd_present(*page_dir)) {
		page_table = (pte_t *) pgd_page(*page_dir);
	} else {
		if (!(page_table = (pte_t *) get_free_page(GFP_KERNEL)))
			return 0;
		if (pgd_present(*page_dir)) {
			free_page((unsigned long) page_table);
			page_table = (pte_t *) pgd_page(*page_dir);
		} else {
			pgd_set(page_dir, page_table);
		}
	}
	page_table += (address >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	if (!pte_none(*page_table)) {
		printk("put_dirty_page: page already exists\n");
		pte_clear(page_table);
		invalidate();
	}
	*page_table = pte_mkwrite(pte_mkdirty(mk_pte(page, PAGE_COPY)));
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
void do_wp_page(struct vm_area_struct * vma, unsigned long address,
	int write_access)
{
	pgd_t *page_dir;
	pte_t *page_table, pte;
	unsigned long old_page, new_page;

	new_page = __get_free_page(GFP_KERNEL);
	page_dir = PAGE_DIR_OFFSET(vma->vm_task,address);
	if (pgd_none(*page_dir))
		goto end_wp_page;
	if (pgd_bad(*page_dir))
		goto bad_wp_pagetable;
	page_table = (pte_t *) pgd_page(*page_dir);
	page_table += (address >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	pte = *page_table;
	if (!pte_present(pte))
		goto end_wp_page;
	if (pte_write(pte))
		goto end_wp_page;
	old_page = pte_page(pte);
	if (old_page >= high_memory)
		goto bad_wp_page;
	vma->vm_task->mm->min_flt++;
	/*
	 * Do we need to copy?
	 */
	if (mem_map[MAP_NR(old_page)] != 1) {
		if (new_page) {
			if (mem_map[MAP_NR(old_page)] & MAP_PAGE_RESERVED)
				++vma->vm_task->mm->rss;
			copy_page(old_page,new_page);
			*page_table = pte_mkwrite(pte_mkdirty(mk_pte(new_page, vma->vm_page_prot)));
			free_page(old_page);
			invalidate();
			return;
		}
		free_page(old_page);
		oom(vma->vm_task);
		*page_table = BAD_PAGE;
		invalidate();
		return;
	}
	*page_table = pte_mkdirty(pte_mkwrite(pte));
	invalidate();
	if (new_page)
		free_page(new_page);
	return;
bad_wp_page:
	printk("do_wp_page: bogus page at address %08lx (%08lx)\n",address,old_page);
	*page_table = BAD_PAGE;
	send_sig(SIGKILL, vma->vm_task, 1);
	goto end_wp_page;
bad_wp_pagetable:
	printk("do_wp_page: bogus page-table at address %08lx (%08lx)\n", address, pgd_val(*page_dir));
	pgd_set(page_dir, BAD_PAGETABLE);
	send_sig(SIGKILL, vma->vm_task, 1);
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
	if (get_fs() == get_ds())
		return 0;

	vma = find_vma(current, start);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= start)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (vma->vm_end - start > current->rlim[RLIMIT_STACK].rlim_cur)
		goto bad_area;

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
		do_wp_page(vma, start, 1);
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

bad_area:
	return -EFAULT;
}

static inline void get_empty_page(struct vm_area_struct * vma, pte_t * page_table)
{
	unsigned long tmp;

	if (!(tmp = get_free_page(GFP_KERNEL))) {
		oom(vma->vm_task);
		put_page(page_table, BAD_PAGE);
		return;
	}
	put_page(page_table, pte_mkwrite(mk_pte(tmp, vma->vm_page_prot)));
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same inode and can generally otherwise be shared.
 */
static int try_to_share(unsigned long to_address, struct vm_area_struct * to_area,
	unsigned long from_address, struct vm_area_struct * from_area,
	unsigned long newpage)
{
	pgd_t * from_dir, * to_dir;
	pte_t * from_table, * to_table;
	pte_t from, to;

	from_dir = PAGE_DIR_OFFSET(from_area->vm_task,from_address);
/* is there a page-directory at from? */
	if (!pgd_present(*from_dir))
		return 0;
	from_table = (pte_t *) (pgd_page(*from_dir) + PAGE_PTR(from_address));
	from = *from_table;
/* is the page present? */
	if (!pte_present(from))
		return 0;
/* if it is dirty it must be from a shared mapping to be shared */
	if (pte_dirty(from)) {
		if (!(from_area->vm_flags & VM_SHARED))
			return 0;
		if (pte_write(from)) {
			printk("nonwritable, but dirty, shared page\n");
			return 0;
		}
	}
/* is the page reasonable at all? */
	if (pte_page(from) >= high_memory)
		return 0;
	if (mem_map[MAP_NR(pte_page(from))] & MAP_PAGE_RESERVED)
		return 0;
/* is the destination ok? */
	to_dir = PAGE_DIR_OFFSET(to_area->vm_task,to_address);
	if (!pgd_present(*to_dir))
		return 0;
	to_table = (pte_t *) (pgd_page(*to_dir) + PAGE_PTR(to_address));
	to = *to_table;
	if (!pte_none(to))
		return 0;
/* do we copy? */
	if (newpage) {
		/* if it's in the swap cache, it's dirty by implication */
		/* so we can't use it if it's not from a shared mapping */
		if (in_swap_cache(pte_page(from))) {
			if (!(from_area->vm_flags & VM_SHARED))
				return 0;
			if (!pte_write(from)) {
				printk("nonwritable, but dirty, shared page\n");
				return 0;
			}
		}
		copy_page(pte_page(from), newpage);
		*to_table = mk_pte(newpage, to_area->vm_page_prot);
		return 1;
	}
/*
 * do a final swap-cache test before sharing them: if it's in the swap
 * cache, we have to remove it now, as we get two pointers to the same
 * physical page and the cache can't handle it. Mark the original dirty.
 *
 * NOTE! Even if "from" is dirty, "to" will be clean: if we get here
 * with a dirty "from", the from-mapping is a shared map, so we can trust
 * the page contents to be up-to-date
 */
	if (in_swap_cache(pte_page(from))) {
		if (!(from_area->vm_flags & VM_SHARED))
			return 0;
		*from_table = pte_mkdirty(from);
		delete_from_swap_cache(pte_page(from));
	}
	mem_map[MAP_NR(pte_page(from))]++;
	*to_table = mk_pte(pte_page(from), to_area->vm_page_prot);
/* Check if we need to do anything at all to the 'from' field */
	if (!pte_write(from))
		return 1;
	if (from_area->vm_flags & VM_SHARED)
		return 1;
/* ok, need to mark it read-only, so invalidate any possible old TB entry */
	*from_table = pte_wrprotect(from);
	invalidate();
	return 1;
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one.
 *
 * We first check if it is at all feasible by checking inode->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */
static int share_page(struct vm_area_struct * area, unsigned long address,
	int write_access, unsigned long newpage)
{
	struct inode * inode;
	unsigned long offset;
	unsigned long from_address;
	unsigned long give_page;
	struct vm_area_struct * mpnt;

	if (!area || !(inode = area->vm_inode) || inode->i_count < 2)
		return 0;
	/* do we need to copy or can we just share? */
	give_page = 0;
	if (write_access && !(area->vm_flags & VM_SHARED)) {
		if (!newpage)
			return 0;
		give_page = newpage;
	}
	offset = address - area->vm_start + area->vm_offset;
	/* See if there is something in the VM we can share pages with. */
	/* Traverse the entire circular i_mmap list, except `area' itself. */
	for (mpnt = area->vm_next_share; mpnt != area; mpnt = mpnt->vm_next_share) {
		/* must be same inode */
		if (mpnt->vm_inode != inode) {
			printk("Aiee! Corrupt vm_area_struct i_mmap ring\n");
			break;	
		}
		/* offsets must be mutually page-aligned */
		if ((mpnt->vm_offset ^ area->vm_offset) & ~PAGE_MASK)
			continue;
		/* the other area must actually cover the wanted page.. */
		from_address = offset + mpnt->vm_start - mpnt->vm_offset;
		if (from_address < mpnt->vm_start || from_address >= mpnt->vm_end)
			continue;
		/* .. NOW we can actually try to use the same physical page */
		if (!try_to_share(address, area, from_address, mpnt, give_page))
			continue;
		/* free newpage if we never used it.. */
		if (give_page || !newpage)
			return 1;
		free_page(newpage);
		return 1;
	}
	return 0;
}

/*
 * fill in an empty page-table if none exists.
 */
static inline pte_t * get_empty_pgtable(struct task_struct * tsk,unsigned long address)
{
	pgd_t *p;
	unsigned long page;

	p = PAGE_DIR_OFFSET(tsk,address);
	if (pgd_present(*p))
		return (pte_t *) (PAGE_PTR(address) + pgd_page(*p));
	if (!pgd_none(*p)) {
		printk("get_empty_pgtable: bad page-directory entry \n");
		pgd_clear(p);
	}
	page = get_free_page(GFP_KERNEL);
	if (pgd_present(*p)) {
		free_page(page);
		return (pte_t *) (PAGE_PTR(address) + pgd_page(*p));
	}
	if (!pgd_none(*p)) {
		printk("get_empty_pgtable: bad page-directory entry \n");
		pgd_clear(p);
	}
	if (page) {
		pgd_set(p, (pte_t *) page);
		return (pte_t *) (PAGE_PTR(address) + page);
	}
	oom(current);
	pgd_set(p, BAD_PAGETABLE);
	return NULL;
}

static inline void do_swap_page(struct vm_area_struct * vma, unsigned long address,
	pte_t * page_table, pte_t entry, int write_access)
{
	pte_t page;

	if (!vma->vm_ops || !vma->vm_ops->swapin) {
		swap_in(vma, page_table, pte_val(entry), write_access);
		return;
	}
	page = vma->vm_ops->swapin(vma, address - vma->vm_start + vma->vm_offset, pte_val(entry));
	if (pte_val(*page_table) != pte_val(entry)) {
		free_page(pte_page(page));
		return;
	}
	if (mem_map[MAP_NR(pte_page(page))] > 1 && !(vma->vm_flags & VM_SHARED))
		page = pte_wrprotect(page);
	++vma->vm_task->mm->rss;
	++vma->vm_task->mm->maj_flt;
	*page_table = page;
	return;
}

/*
 * do_no_page() tries to create a new page mapping. It aggressively
 * tries to share with existing pages, but makes a separate copy if
 * the "write_access" parameter is true in order to avoid the next
 * page fault.
 */
void do_no_page(struct vm_area_struct * vma, unsigned long address,
	int write_access)
{
	pte_t * page_table;
	pte_t entry;
	unsigned long page;

	page_table = get_empty_pgtable(vma->vm_task,address);
	if (!page_table)
		return;
	entry = *page_table;
	if (pte_present(entry))
		return;
	if (!pte_none(entry)) {
		do_swap_page(vma, address, page_table, entry, write_access);
		return;
	}
	address &= PAGE_MASK;

	if (!vma->vm_ops || !vma->vm_ops->nopage) {
		++vma->vm_task->mm->rss;
		++vma->vm_task->mm->min_flt;
		get_empty_page(vma, page_table);
		return;
	}
	page = get_free_page(GFP_KERNEL);
	if (share_page(vma, address, write_access, page)) {
		++vma->vm_task->mm->min_flt;
		++vma->vm_task->mm->rss;
		return;
	}
	if (!page) {
		oom(current);
		put_page(page_table, BAD_PAGE);
		return;
	}
	++vma->vm_task->mm->maj_flt;
	++vma->vm_task->mm->rss;
	/*
	 * The fourth argument is "no_share", which tells the low-level code
	 * to copy, not share the page even if sharing is possible.  It's
	 * essentially an early COW detection 
	 */
	page = vma->vm_ops->nopage(vma, address, page,
		write_access && !(vma->vm_flags & VM_SHARED));
	if (share_page(vma, address, write_access, 0)) {
		free_page(page);
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
	entry = mk_pte(page, vma->vm_page_prot);
	if (write_access) {
		entry = pte_mkwrite(pte_mkdirty(entry));
	} else if (mem_map[MAP_NR(page)] > 1 && !(vma->vm_flags & VM_SHARED))
		entry = pte_wrprotect(entry);
	put_page(page_table, entry);
}

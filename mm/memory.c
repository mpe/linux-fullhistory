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

#include <asm/system.h>
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
#include <linux/segment.h>
#include <asm/segment.h>

/*
 * Define this if things work differently on a i386 and a i486:
 * it will (on a i486) warn about kernel memory accesses that are
 * done without a 'verify_area(VERIFY_WRITE,..)'
 */
#undef CONFIG_TEST_VERIFY_AREA

unsigned long high_memory = 0;

extern unsigned long pg0[1024];		/* page table for 0-4MB for everybody */

extern void sound_mem_init(void);
extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);

/*
 * The free_area_list arrays point to the queue heads of the free areas
 * of different sizes
 */
int nr_swap_pages = 0;
int nr_free_pages = 0;
struct mem_list free_area_list[NR_MEM_LISTS];
unsigned char * free_area_map[NR_MEM_LISTS];

#define copy_page(from,to) \
__asm__("cld ; rep ; movsl": :"S" (from),"D" (to),"c" (1024):"cx","di","si")

unsigned short * mem_map = NULL;

#define CODE_SPACE(addr,p) ((addr) < (p)->end_code)

/*
 * oom() prints a message (so that the user knows why the process died),
 * and gives the process an untrappable SIGKILL.
 */
void oom(struct task_struct * task)
{
	printk("\nOut of memory.\n");
	task->sigaction[SIGKILL-1].sa_handler = NULL;
	task->blocked &= ~(1<<(SIGKILL-1));
	send_sig(SIGKILL,task,1);
}

static void free_one_table(unsigned long * page_dir)
{
	int j;
	unsigned long pg_table = *page_dir;
	unsigned long * page_table;

	if (!pg_table)
		return;
	*page_dir = 0;
	if (pg_table >= high_memory || !(pg_table & PAGE_PRESENT)) {
		printk("Bad page table: [%p]=%08lx\n",page_dir,pg_table);
		return;
	}
	if (mem_map[MAP_NR(pg_table)] & MAP_PAGE_RESERVED)
		return;
	page_table = (unsigned long *) (pg_table & PAGE_MASK);
	for (j = 0 ; j < PTRS_PER_PAGE ; j++,page_table++) {
		unsigned long pg = *page_table;
		
		if (!pg)
			continue;
		*page_table = 0;
		if (pg & PAGE_PRESENT)
			free_page(PAGE_MASK & pg);
		else
			swap_free(pg);
	}
	free_page(PAGE_MASK & pg_table);
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
	unsigned long pg_dir;
	unsigned long * page_dir;

	if (!tsk)
		return;
	if (tsk == task[0])
		panic("task[0] (swapper) doesn't support exec()\n");
	pg_dir = tsk->tss.cr3;
	page_dir = (unsigned long *) pg_dir;
	if (!page_dir || page_dir == swapper_pg_dir) {
		printk("Trying to clear kernel page-directory: not good\n");
		return;
	}
	if (mem_map[MAP_NR(pg_dir)] > 1) {
		unsigned long * new_pg;

		if (!(new_pg = (unsigned long*) get_free_page(GFP_KERNEL))) {
			oom(tsk);
			return;
		}
		for (i = 768 ; i < 1024 ; i++)
			new_pg[i] = page_dir[i];
		free_page(pg_dir);
		tsk->tss.cr3 = (unsigned long) new_pg;
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
	unsigned long pg_dir;
	unsigned long * page_dir;

	if (!tsk)
		return;
	if (tsk == task[0]) {
		printk("task[0] (swapper) killed: unable to recover\n");
		panic("Trying to free up swapper memory space");
	}
	pg_dir = tsk->tss.cr3;
	if (!pg_dir || pg_dir == (unsigned long) swapper_pg_dir) {
		printk("Trying to free kernel page-directory: not good\n");
		return;
	}
	tsk->tss.cr3 = (unsigned long) swapper_pg_dir;
	if (tsk == current)
		__asm__ __volatile__("movl %0,%%cr3": :"a" (tsk->tss.cr3));
	if (mem_map[MAP_NR(pg_dir)] > 1) {
		free_page(pg_dir);
		return;
	}
	page_dir = (unsigned long *) pg_dir;
	for (i = 0 ; i < PTRS_PER_PAGE ; i++,page_dir++)
		free_one_table(page_dir);
	free_page(pg_dir);
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
	unsigned long pg_dir;

	pg_dir = current->tss.cr3;
	mem_map[MAP_NR(pg_dir)]++;
	tsk->tss.cr3 = pg_dir;
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
	unsigned long old_pg_dir, *old_page_dir;
	unsigned long new_pg_dir, *new_page_dir;

	if (!(new_pg_dir = get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	old_pg_dir = current->tss.cr3;
	tsk->tss.cr3 = new_pg_dir;
	old_page_dir = (unsigned long *) old_pg_dir;
	new_page_dir = (unsigned long *) new_pg_dir;
	for (i = 0 ; i < PTRS_PER_PAGE ; i++,old_page_dir++,new_page_dir++) {
		int j;
		unsigned long old_pg_table, *old_page_table;
		unsigned long new_pg_table, *new_page_table;

		old_pg_table = *old_page_dir;
		if (!old_pg_table)
			continue;
		if (old_pg_table >= high_memory || !(old_pg_table & PAGE_PRESENT)) {
			printk("copy_page_tables: bad page table: "
				"probable memory corruption\n");
			*old_page_dir = 0;
			continue;
		}
		if (mem_map[MAP_NR(old_pg_table)] & MAP_PAGE_RESERVED) {
			*new_page_dir = old_pg_table;
			continue;
		}
		if (!(new_pg_table = get_free_page(GFP_KERNEL))) {
			free_page_tables(tsk);
			return -ENOMEM;
		}
		old_page_table = (unsigned long *) (PAGE_MASK & old_pg_table);
		new_page_table = (unsigned long *) (PAGE_MASK & new_pg_table);
		for (j = 0 ; j < PTRS_PER_PAGE ; j++,old_page_table++,new_page_table++) {
			unsigned long pg;
			pg = *old_page_table;
			if (!pg)
				continue;
			if (!(pg & PAGE_PRESENT)) {
				*new_page_table = swap_duplicate(pg);
				continue;
			}
			if (pg > high_memory || (mem_map[MAP_NR(pg)] & MAP_PAGE_RESERVED)) {
				*new_page_table = pg;
				continue;
			}
			if (pg & PAGE_COW)
				pg &= ~PAGE_RW;
			if (delete_from_swap_cache(pg))
				pg |= PAGE_DIRTY;
			*new_page_table = pg;
			*old_page_table = pg;
			mem_map[MAP_NR(pg)]++;
		}
		*new_page_dir = new_pg_table | PAGE_TABLE;
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
	unsigned long page, page_dir;
	unsigned long *page_table, *dir;
	unsigned long poff, pcnt, pc;

	if (from & ~PAGE_MASK) {
		printk("unmap_page_range called with wrong alignment\n");
		return -EINVAL;
	}
	size = (size + ~PAGE_MASK) >> PAGE_SHIFT;
	dir = PAGE_DIR_OFFSET(current->tss.cr3,from);
	poff = (from >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	if ((pcnt = PTRS_PER_PAGE - poff) > size)
		pcnt = size;

	for ( ; size > 0; ++dir, size -= pcnt,
	     pcnt = (size > PTRS_PER_PAGE ? PTRS_PER_PAGE : size)) {
		if (!(page_dir = *dir))	{
			poff = 0;
			continue;
		}
		if (!(page_dir & PAGE_PRESENT)) {
			printk("unmap_page_range: bad page directory.");
			continue;
		}
		page_table = (unsigned long *)(PAGE_MASK & page_dir);
		if (poff) {
			page_table += poff;
			poff = 0;
		}
		for (pc = pcnt; pc--; page_table++) {
			if ((page = *page_table) != 0) {
				*page_table = 0;
				if (PAGE_PRESENT & page) {
					if (!(mem_map[MAP_NR(page)] & MAP_PAGE_RESERVED))
						if (current->mm->rss > 0)
							--current->mm->rss;
					free_page(PAGE_MASK & page);
				} else
					swap_free(page);
			}
		}
		if (pcnt == PTRS_PER_PAGE) {
			*dir = 0;
			free_page(PAGE_MASK & page_dir);
		}
	}
	invalidate();
	return 0;
}

int zeromap_page_range(unsigned long from, unsigned long size, int mask)
{
	unsigned long *page_table, *dir;
	unsigned long poff, pcnt;
	unsigned long page;

	if (mask) {
		if ((mask & (PAGE_MASK|PAGE_PRESENT)) != PAGE_PRESENT) {
			printk("zeromap_page_range: mask = %08x\n",mask);
			return -EINVAL;
		}
		mask |= ZERO_PAGE;
	}
	if (from & ~PAGE_MASK) {
		printk("zeromap_page_range: from = %08lx\n",from);
		return -EINVAL;
	}
	dir = PAGE_DIR_OFFSET(current->tss.cr3,from);
	size = (size + ~PAGE_MASK) >> PAGE_SHIFT;
	poff = (from >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	if ((pcnt = PTRS_PER_PAGE - poff) > size)
		pcnt = size;

	while (size > 0) {
		if (!(PAGE_PRESENT & *dir)) {
				/* clear page needed here?  SRB. */
			if (!(page_table = (unsigned long*) get_free_page(GFP_KERNEL))) {
				invalidate();
				return -ENOMEM;
			}
			if (PAGE_PRESENT & *dir) {
				free_page((unsigned long) page_table);
				page_table = (unsigned long *)(PAGE_MASK & *dir++);
			} else
				*dir++ = ((unsigned long) page_table) | PAGE_TABLE;
		} else
			page_table = (unsigned long *)(PAGE_MASK & *dir++);
		page_table += poff;
		poff = 0;
		for (size -= pcnt; pcnt-- ;) {
			if ((page = *page_table) != 0) {
				*page_table = 0;
				if (page & PAGE_PRESENT) {
					if (!(mem_map[MAP_NR(page)] & MAP_PAGE_RESERVED))
						if (current->mm->rss > 0)
							--current->mm->rss;
					free_page(PAGE_MASK & page);
				} else
					swap_free(page);
			}
			*page_table++ = mask;
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
int remap_page_range(unsigned long from, unsigned long to, unsigned long size, int mask)
{
	unsigned long *page_table, *dir;
	unsigned long poff, pcnt;
	unsigned long page;

	if (mask) {
		if ((mask & (PAGE_MASK|PAGE_PRESENT)) != PAGE_PRESENT) {
			printk("remap_page_range: mask = %08x\n",mask);
			return -EINVAL;
		}
	}
	if ((from & ~PAGE_MASK) || (to & ~PAGE_MASK)) {
		printk("remap_page_range: from = %08lx, to=%08lx\n",from,to);
		return -EINVAL;
	}
	dir = PAGE_DIR_OFFSET(current->tss.cr3,from);
	size = (size + ~PAGE_MASK) >> PAGE_SHIFT;
	poff = (from >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	if ((pcnt = PTRS_PER_PAGE - poff) > size)
		pcnt = size;

	while (size > 0) {
		if (!(PAGE_PRESENT & *dir)) {
			/* clearing page here, needed?  SRB. */
			if (!(page_table = (unsigned long*) get_free_page(GFP_KERNEL))) {
				invalidate();
				return -1;
			}
			*dir++ = ((unsigned long) page_table) | PAGE_TABLE;
		}
		else
			page_table = (unsigned long *)(PAGE_MASK & *dir++);
		if (poff) {
			page_table += poff;
			poff = 0;
		}

		for (size -= pcnt; pcnt-- ;) {
			if ((page = *page_table) != 0) {
				*page_table = 0;
				if (PAGE_PRESENT & page) {
					if (!(mem_map[MAP_NR(page)] & MAP_PAGE_RESERVED))
						if (current->mm->rss > 0)
							--current->mm->rss;
					free_page(PAGE_MASK & page);
				} else
					swap_free(page);
			}

			/*
			 * the first condition should return an invalid access
			 * when the page is referenced. current assumptions
			 * cause it to be treated as demand allocation in some
			 * cases.
			 */
			if (!mask)
				*page_table++ = 0;	/* not present */
			else if (to >= high_memory)
				*page_table++ = (to | mask);
			else if (!mem_map[MAP_NR(to)])
				*page_table++ = 0;	/* not present */
			else {
				*page_table++ = (to | mask);
				if (!(mem_map[MAP_NR(to)] & MAP_PAGE_RESERVED)) {
					++current->mm->rss;
					mem_map[MAP_NR(to)]++;
				}
			}
			to += PAGE_SIZE;
		}
		pcnt = (size > PTRS_PER_PAGE ? PTRS_PER_PAGE : size);
	}
	invalidate();
	return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
unsigned long put_page(struct task_struct * tsk,unsigned long page,
	unsigned long address,int prot)
{
	unsigned long *page_table;

	if ((prot & (PAGE_MASK|PAGE_PRESENT)) != PAGE_PRESENT)
		printk("put_page: prot = %08x\n",prot);
	if (page >= high_memory) {
		printk("put_page: trying to put page %08lx at %08lx\n",page,address);
		return 0;
	}
	page_table = PAGE_DIR_OFFSET(tsk->tss.cr3,address);
	if ((*page_table) & PAGE_PRESENT)
		page_table = (unsigned long *) (PAGE_MASK & *page_table);
	else {
		printk("put_page: bad page directory entry\n");
		oom(tsk);
		*page_table = BAD_PAGETABLE | PAGE_TABLE;
		return 0;
	}
	page_table += (address >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	if (*page_table) {
		printk("put_page: page already exists\n");
		*page_table = 0;
		invalidate();
	}
	*page_table = page | prot;
/* no need for invalidate */
	return page;
}

/*
 * The previous function doesn't work very well if you also want to mark
 * the page dirty: exec.c wants this, as it has earlier changed the page,
 * and we want the dirty-status to be correct (for VM). Thus the same
 * routine, but this time we mark it dirty too.
 */
unsigned long put_dirty_page(struct task_struct * tsk, unsigned long page, unsigned long address)
{
	unsigned long tmp, *page_table;

	if (page >= high_memory)
		printk("put_dirty_page: trying to put page %08lx at %08lx\n",page,address);
	if (mem_map[MAP_NR(page)] != 1)
		printk("mem_map disagrees with %08lx at %08lx\n",page,address);
	page_table = PAGE_DIR_OFFSET(tsk->tss.cr3,address);
	if (PAGE_PRESENT & *page_table)
		page_table = (unsigned long *) (PAGE_MASK & *page_table);
	else {
		if (!(tmp = get_free_page(GFP_KERNEL)))
			return 0;
		if (PAGE_PRESENT & *page_table) {
			free_page(tmp);
			page_table = (unsigned long *) (PAGE_MASK & *page_table);
		} else {
			*page_table = tmp | PAGE_TABLE;
			page_table = (unsigned long *) tmp;
		}
	}
	page_table += (address >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	if (*page_table) {
		printk("put_dirty_page: page already exists\n");
		*page_table = 0;
		invalidate();
	}
	*page_table = page | (PAGE_DIRTY | PAGE_PRIVATE);
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
 */
void do_wp_page(struct vm_area_struct * vma, unsigned long address,
	unsigned long error_code)
{
	unsigned long *pde, pte, old_page, prot;
	unsigned long new_page;

	new_page = __get_free_page(GFP_KERNEL);
	pde = PAGE_DIR_OFFSET(vma->vm_task->tss.cr3,address);
	pte = *pde;
	if (!(pte & PAGE_PRESENT))
		goto end_wp_page;
	if ((pte & PAGE_TABLE) != PAGE_TABLE || pte >= high_memory)
		goto bad_wp_pagetable;
	pte &= PAGE_MASK;
	pte += PAGE_PTR(address);
	old_page = *(unsigned long *) pte;
	if (!(old_page & PAGE_PRESENT))
		goto end_wp_page;
	if (old_page >= high_memory)
		goto bad_wp_page;
	if (old_page & PAGE_RW)
		goto end_wp_page;
	vma->vm_task->mm->min_flt++;
	prot = (old_page & ~PAGE_MASK) | PAGE_RW | PAGE_DIRTY;
	old_page &= PAGE_MASK;
	if (mem_map[MAP_NR(old_page)] != 1) {
		if (new_page) {
			if (mem_map[MAP_NR(old_page)] & MAP_PAGE_RESERVED)
				++vma->vm_task->mm->rss;
			copy_page(old_page,new_page);
			*(unsigned long *) pte = new_page | prot;
			free_page(old_page);
			invalidate();
			return;
		}
		free_page(old_page);
		oom(vma->vm_task);
		*(unsigned long *) pte = BAD_PAGE | prot;
		invalidate();
		return;
	}
	*(unsigned long *) pte |= PAGE_RW | PAGE_DIRTY;
	invalidate();
	if (new_page)
		free_page(new_page);
	return;
bad_wp_page:
	printk("do_wp_page: bogus page at address %08lx (%08lx)\n",address,old_page);
	*(unsigned long *) pte = BAD_PAGE | PAGE_SHARED;
	send_sig(SIGKILL, vma->vm_task, 1);
	goto end_wp_page;
bad_wp_pagetable:
	printk("do_wp_page: bogus page-table at address %08lx (%08lx)\n",address,pte);
	*pde = BAD_PAGETABLE | PAGE_TABLE;
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

	for (vma = current->mm->mmap ; ; vma = vma->vm_next) {
		if (!vma)
			goto bad_area;
		if (vma->vm_end > start)
			break;
	}
	if (vma->vm_start <= start)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (vma->vm_end - start > current->rlim[RLIMIT_STACK].rlim_cur)
		goto bad_area;

good_area:
	if (!wp_works_ok && type == VERIFY_WRITE)
		goto check_wp_fault_by_hand;
	for (;;) {
		struct vm_area_struct * next;
		if (!(vma->vm_page_prot & PAGE_USER))
			goto bad_area;
		if (type != VERIFY_READ && !(vma->vm_page_prot & (PAGE_COW | PAGE_RW)))
			goto bad_area;
		if (vma->vm_end - start >= size)
			return 0;
		next = vma->vm_next;
		if (!next || vma->vm_end != next->vm_start)
			goto bad_area;
		vma = next;
	}

check_wp_fault_by_hand:
	size--;
	size += start & ~PAGE_MASK;
	size >>= PAGE_SHIFT;
	start &= PAGE_MASK;

	for (;;) {
		if (!(vma->vm_page_prot & (PAGE_COW | PAGE_RW)))
			goto bad_area;
		do_wp_page(vma, start, PAGE_PRESENT);
		if (!size)
			return 0;
		size--;
		start += PAGE_SIZE;
		if (start < vma->vm_end)
			continue;
		vma = vma->vm_next;
		if (!vma || vma->vm_start != start)
			break;
	}

bad_area:
	return -EFAULT;
}

static inline void get_empty_page(struct task_struct * tsk, unsigned long address)
{
	unsigned long tmp;

	if (!(tmp = get_free_page(GFP_KERNEL))) {
		oom(tsk);
		tmp = BAD_PAGE;
	}
	if (!put_page(tsk,tmp,address,PAGE_PRIVATE))
		free_page(tmp);
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
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;

	from_page = (unsigned long)PAGE_DIR_OFFSET(from_area->vm_task->tss.cr3,from_address);
	to_page = (unsigned long)PAGE_DIR_OFFSET(to_area->vm_task->tss.cr3,to_address);
/* is there a page-directory at from? */
	from = *(unsigned long *) from_page;
	if (!(from & PAGE_PRESENT))
		return 0;
	from &= PAGE_MASK;
	from_page = from + PAGE_PTR(from_address);
	from = *(unsigned long *) from_page;
/* is the page present? */
	if (!(from & PAGE_PRESENT))
		return 0;
/* if it is private, it must be clean to be shared */
	if (from & PAGE_DIRTY) {
		if (from_area->vm_page_prot & PAGE_COW)
			return 0;
		if (!(from_area->vm_page_prot & PAGE_RW))
			return 0;
	}		
/* is the page reasonable at all? */
	if (from >= high_memory)
		return 0;
	if (mem_map[MAP_NR(from)] & MAP_PAGE_RESERVED)
		return 0;
/* is the destination ok? */
	to = *(unsigned long *) to_page;
	if (!(to & PAGE_PRESENT))
		return 0;
	to &= PAGE_MASK;
	to_page = to + PAGE_PTR(to_address);
	if (*(unsigned long *) to_page)
		return 0;
/* do we copy? */
	if (newpage) {
		if (in_swap_cache(from)) { /* implies PAGE_DIRTY */
			if (from_area->vm_page_prot & PAGE_COW)
				return 0;
			if (!(from_area->vm_page_prot & PAGE_RW))
				return 0;
		}
		copy_page((from & PAGE_MASK), newpage);
		*(unsigned long *) to_page = newpage | to_area->vm_page_prot;
		return 1;
	}
/* do a final swap-cache test before sharing them.. */
	if (in_swap_cache(from)) {
		if (from_area->vm_page_prot & PAGE_COW)
			return 0;
		if (!(from_area->vm_page_prot & PAGE_RW))
			return 0;
		from |= PAGE_DIRTY;
		*(unsigned long *) from_page = from;
		delete_from_swap_cache(from);
		invalidate();
	}
	mem_map[MAP_NR(from)]++;
/* fill in the 'to' field, checking for COW-stuff */
	to = (from & (PAGE_MASK | PAGE_DIRTY)) | to_area->vm_page_prot;
	if (to & PAGE_COW)
		to &= ~PAGE_RW;
	*(unsigned long *) to_page = to;
/* Check if we need to do anything at all to the 'from' field */
	if (!(from & PAGE_RW))
		return 1;
	if (!(from_area->vm_page_prot & PAGE_COW))
		return 1;
/* ok, need to mark it read-only, so invalidate any possible old TB entry */
	from &= ~PAGE_RW;
	*(unsigned long *) from_page = from;
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
	unsigned long error_code, unsigned long newpage)
{
	struct inode * inode;
	struct task_struct ** p;
	unsigned long offset;
	unsigned long from_address;
	unsigned long give_page;

	if (!area || !(inode = area->vm_inode) || inode->i_count < 2)
		return 0;
	/* do we need to copy or can we just share? */
	give_page = 0;
	if ((area->vm_page_prot & PAGE_COW) && (error_code & PAGE_RW)) {
		if (!newpage)
			return 0;
		give_page = newpage;
	}
	offset = address - area->vm_start + area->vm_offset;
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		struct vm_area_struct * mpnt;
		if (!*p)
			continue;
		if (area->vm_task == *p)
			continue;
		/* Now see if there is something in the VMM that
		   we can share pages with */
		for (mpnt = (*p)->mm->mmap; mpnt; mpnt = mpnt->vm_next) {
			/* must be same inode */
			if (mpnt->vm_inode != inode)
				continue;
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
	}
	return 0;
}

/*
 * fill in an empty page-table if none exists.
 */
static inline unsigned long get_empty_pgtable(struct task_struct * tsk,unsigned long address)
{
	unsigned long page;
	unsigned long *p;

	p = PAGE_DIR_OFFSET(tsk->tss.cr3,address);
	if (PAGE_PRESENT & *p)
		return *p;
	if (*p) {
		printk("get_empty_pgtable: bad page-directory entry \n");
		*p = 0;
	}
	page = get_free_page(GFP_KERNEL);
	p = PAGE_DIR_OFFSET(tsk->tss.cr3,address);
	if (PAGE_PRESENT & *p) {
		free_page(page);
		return *p;
	}
	if (*p) {
		printk("get_empty_pgtable: bad page-directory entry \n");
		*p = 0;
	}
	if (page) {
		*p = page | PAGE_TABLE;
		return *p;
	}
	oom(current);
	*p = BAD_PAGETABLE | PAGE_TABLE;
	return 0;
}

static inline void do_swap_page(struct vm_area_struct * vma,
	unsigned long address, unsigned long * pge, unsigned long entry)
{
	unsigned long page;

	if (vma->vm_ops && vma->vm_ops->swapin)
		page = vma->vm_ops->swapin(vma, entry);
	else
		page = swap_in(entry);
	if (*pge != entry) {
		free_page(page);
		return;
	}
	page = page | vma->vm_page_prot;
	if (mem_map[MAP_NR(page)] > 1 && (page & PAGE_COW))
		page &= ~PAGE_RW;
	++vma->vm_task->mm->rss;
	++vma->vm_task->mm->maj_flt;
	*pge = page;
	return;
}

void do_no_page(struct vm_area_struct * vma, unsigned long address,
	unsigned long error_code)
{
	unsigned long page, entry, prot;

	page = get_empty_pgtable(vma->vm_task,address);
	if (!page)
		return;
	page &= PAGE_MASK;
	page += PAGE_PTR(address);
	entry = *(unsigned long *) page;
	if (entry & PAGE_PRESENT)
		return;
	if (entry) {
		do_swap_page(vma, address, (unsigned long *) page, entry);
		return;
	}
	address &= PAGE_MASK;

	if (!vma->vm_ops || !vma->vm_ops->nopage) {
		++vma->vm_task->mm->rss;
		++vma->vm_task->mm->min_flt;
		get_empty_page(vma->vm_task,address);
		return;
	}
	page = get_free_page(GFP_KERNEL);
	if (share_page(vma, address, error_code, page)) {
		++vma->vm_task->mm->min_flt;
		++vma->vm_task->mm->rss;
		return;
	}
	if (!page) {
		oom(current);
		put_page(vma->vm_task, BAD_PAGE, address, PAGE_PRIVATE);
		return;
	}
	++vma->vm_task->mm->maj_flt;
	++vma->vm_task->mm->rss;
	prot = vma->vm_page_prot;
	/*
	 * The fourth argument is "no_share", which tells the low-level code
	 * to copy, not share the page even if sharing is possible.  It's
	 * essentially an early COW detection ("moo at 5 AM").
	 */
	page = vma->vm_ops->nopage(vma, address, page, (error_code & PAGE_RW) && (prot & PAGE_COW));
	if (share_page(vma, address, error_code, 0)) {
		free_page(page);
		return;
	}
	/*
	 * This silly early PAGE_DIRTY setting removes a race
	 * due to the bad i386 page protection.
	 */
	if (error_code & PAGE_RW) {
		prot |= PAGE_DIRTY;	/* can't be COW-shared: see "no_share" above */
	} else if ((prot & PAGE_COW) && mem_map[MAP_NR(page)] > 1)
		prot &= ~PAGE_RW;
	if (put_page(vma->vm_task, page, address, prot))
		return;
	free_page(page);
	oom(current);
}

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
	struct vm_area_struct * vma;
	unsigned long address;
	unsigned long page;

	/* get the address */
	__asm__("movl %%cr2,%0":"=r" (address));
	for (vma = current->mm->mmap ; ; vma = vma->vm_next) {
		if (!vma)
			goto bad_area;
		if (vma->vm_end > address)
			break;
	}
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (vma->vm_end - address > current->rlim[RLIMIT_STACK].rlim_cur)
		goto bad_area;
	vma->vm_offset -= vma->vm_start - (address & PAGE_MASK);
	vma->vm_start = (address & PAGE_MASK);
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	if (regs->eflags & VM_MASK) {
		unsigned long bit = (address - 0xA0000) >> PAGE_SHIFT;
		if (bit < 32)
			current->screen_bitmap |= 1 << bit;
	}
	if (!(vma->vm_page_prot & PAGE_USER))
		goto bad_area;
	if (error_code & PAGE_PRESENT) {
		if (!(vma->vm_page_prot & (PAGE_RW | PAGE_COW)))
			goto bad_area;
#ifdef CONFIG_TEST_VERIFY_AREA
		if (regs->cs == KERNEL_CS)
			printk("WP fault at %08x\n", regs->eip);
#endif
		do_wp_page(vma, address, error_code);
		return;
	}
	do_no_page(vma, address, error_code);
	return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	if (error_code & PAGE_USER) {
		current->tss.cr2 = address;
		current->tss.error_code = error_code;
		current->tss.trap_no = 14;
		send_sig(SIGSEGV, current, 1);
		return;
	}
/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 */
	if (wp_works_ok < 0 && address == TASK_SIZE && (error_code & PAGE_PRESENT)) {
		wp_works_ok = 1;
		pg0[0] = PAGE_SHARED;
		printk("This processor honours the WP bit even when in supervisor mode. Good.\n");
		return;
	}
	if ((unsigned long) (address-TASK_SIZE) < PAGE_SIZE) {
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
		pg0[0] = PAGE_SHARED;
	} else
		printk(KERN_ALERT "Unable to handle kernel paging request");
	printk(" at virtual address %08lx\n",address);
	__asm__("movl %%cr3,%0" : "=r" (page));
	printk(KERN_ALERT "current->tss.cr3 = %08lx, %%cr3 = %08lx\n",
		current->tss.cr3, page);
	page = ((unsigned long *) page)[address >> 22];
	printk(KERN_ALERT "*pde = %08lx\n", page);
	if (page & PAGE_PRESENT) {
		page &= PAGE_MASK;
		address &= 0x003ff000;
		page = ((unsigned long *) page)[address >> PAGE_SHIFT];
		printk(KERN_ALERT "*pte = %08lx\n", page);
	}
	die_if_kernel("Oops", regs, error_code);
	do_exit(SIGKILL);
}

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving a inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
unsigned long __bad_pagetable(void)
{
	extern char empty_bad_page_table[PAGE_SIZE];

	__asm__ __volatile__("cld ; rep ; stosl":
		:"a" (BAD_PAGE + PAGE_TABLE),
		 "D" ((long) empty_bad_page_table),
		 "c" (PTRS_PER_PAGE)
		:"di","cx");
	return (unsigned long) empty_bad_page_table;
}

unsigned long __bad_page(void)
{
	extern char empty_bad_page[PAGE_SIZE];

	__asm__ __volatile__("cld ; rep ; stosl":
		:"a" (0),
		 "D" ((long) empty_bad_page),
		 "c" (PTRS_PER_PAGE)
		:"di","cx");
	return (unsigned long) empty_bad_page;
}

unsigned long __zero_page(void)
{
	extern char empty_zero_page[PAGE_SIZE];

	__asm__ __volatile__("cld ; rep ; stosl":
		:"a" (0),
		 "D" ((long) empty_zero_page),
		 "c" (PTRS_PER_PAGE)
		:"di","cx");
	return (unsigned long) empty_zero_page;
}

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = high_memory >> PAGE_SHIFT;
	while (i-- > 0) {
		total++;
		if (mem_map[i] & MAP_PAGE_RESERVED)
			reserved++;
		else if (!mem_map[i])
			free++;
		else
			shared += mem_map[i]-1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
}

extern unsigned long free_area_init(unsigned long, unsigned long);

/*
 * paging_init() sets up the page tables - note that the first 4MB are
 * already mapped by head.S.
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long * pg_dir;
	unsigned long * pg_table;
	unsigned long tmp;
	unsigned long address;

/*
 * Physical page 0 is special; it's not touched by Linux since BIOS
 * and SMM (for laptops with [34]86/SL chips) may need it.  It is read
 * and write protected to detect null pointer references in the
 * kernel.
 */
#if 0
	memset((void *) 0, 0, PAGE_SIZE);
#endif
	start_mem = PAGE_ALIGN(start_mem);
	address = 0;
	pg_dir = swapper_pg_dir;
	while (address < end_mem) {
		tmp = *(pg_dir + 768);		/* at virtual addr 0xC0000000 */
		if (!tmp) {
			tmp = start_mem | PAGE_TABLE;
			*(pg_dir + 768) = tmp;
			start_mem += PAGE_SIZE;
		}
		*pg_dir = tmp;			/* also map it in at 0x0000000 for init */
		pg_dir++;
		pg_table = (unsigned long *) (tmp & PAGE_MASK);
		for (tmp = 0 ; tmp < PTRS_PER_PAGE ; tmp++,pg_table++) {
			if (address < end_mem)
				*pg_table = address | PAGE_SHARED;
			else
				*pg_table = 0;
			address += PAGE_SIZE;
		}
	}
	invalidate();
	return free_area_init(start_mem, end_mem);
}

void mem_init(unsigned long start_low_mem,
	      unsigned long start_mem, unsigned long end_mem)
{
	int codepages = 0;
	int reservedpages = 0;
	int datapages = 0;
	unsigned long tmp;
	extern int etext;

	cli();
	end_mem &= PAGE_MASK;
	high_memory = end_mem;

	/* mark usable pages in the mem_map[] */
	start_low_mem = PAGE_ALIGN(start_low_mem);
	start_mem = PAGE_ALIGN(start_mem);

	/*
	 * IBM messed up *AGAIN* in their thinkpad: 0xA0000 -> 0x9F000.
	 * They seem to have done something stupid with the floppy
	 * controller as well..
	 */
	while (start_low_mem < 0x9f000) {
		mem_map[MAP_NR(start_low_mem)] = 0;
		start_low_mem += PAGE_SIZE;
	}

	while (start_mem < high_memory) {
		mem_map[MAP_NR(start_mem)] = 0;
		start_mem += PAGE_SIZE;
	}
#ifdef CONFIG_SOUND
	sound_mem_init();
#endif
	for (tmp = 0 ; tmp < high_memory ; tmp += PAGE_SIZE) {
		if (mem_map[MAP_NR(tmp)]) {
			if (tmp >= 0xA0000 && tmp < 0x100000)
				reservedpages++;
			else if (tmp < (unsigned long) &etext)
				codepages++;
			else
				datapages++;
			continue;
		}
		mem_map[MAP_NR(tmp)] = 1;
		free_page(tmp);
	}
	tmp = nr_free_pages << PAGE_SHIFT;
	printk("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data)\n",
		tmp >> 10,
		high_memory >> 10,
		codepages << (PAGE_SHIFT-10),
		reservedpages << (PAGE_SHIFT-10),
		datapages << (PAGE_SHIFT-10));
/* test if the WP bit is honoured in supervisor mode */
	wp_works_ok = -1;
	pg0[0] = PAGE_READONLY;
	invalidate();
	__asm__ __volatile__("movb 0,%%al ; movb %%al,0": : :"ax", "memory");
	pg0[0] = 0;
	invalidate();
	if (wp_works_ok < 0)
		wp_works_ok = 0;
#ifdef CONFIG_TEST_VERIFY_AREA
	wp_works_ok = 0;
#endif
	return;
}

void si_meminfo(struct sysinfo *val)
{
	int i;

	i = high_memory >> PAGE_SHIFT;
	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages << PAGE_SHIFT;
	val->bufferram = buffermem;
	while (i-- > 0)  {
		if (mem_map[i] & MAP_PAGE_RESERVED)
			continue;
		val->totalram++;
		if (!mem_map[i])
			continue;
		val->sharedram += mem_map[i]-1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
	return;
}


/*
 * This handles a generic mmap of a disk file.
 */
static unsigned long file_mmap_nopage(struct vm_area_struct * area, unsigned long address,
	unsigned long page, int no_share)
{
	struct inode * inode = area->vm_inode;
	unsigned int block;
	int nr[8];
	int i, *p;

	address &= PAGE_MASK;
	block = address - area->vm_start + area->vm_offset;
	block >>= inode->i_sb->s_blocksize_bits;
	i = PAGE_SIZE >> inode->i_sb->s_blocksize_bits;
	p = nr;
	do {
		*p = bmap(inode,block);
		i--;
		block++;
		p++;
	} while (i > 0);
	return bread_page(page, inode->i_dev, nr, inode->i_sb->s_blocksize, no_share);
}

struct vm_operations_struct file_mmap = {
	NULL,			/* open */
	NULL,			/* close */
	file_mmap_nopage,	/* nopage */
	NULL,			/* wppage */
	NULL,			/* share */
	NULL,			/* unmap */
};

/*
 *  linux/mm/swap.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

/*
 * This file should contain most things doing the swapping from/to disk.
 * Started 18.12.91
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fs.h>

#include <asm/dma.h>
#include <asm/system.h> /* for cli()/sti() */
#include <asm/bitops.h>
#include <asm/pgtable.h>

#define MAX_SWAPFILES 8

#define SWP_USED	1
#define SWP_WRITEOK	3

#define SWP_TYPE(entry) (((entry) >> 1) & 0x7f)
#define SWP_OFFSET(entry) ((entry) >> 12)
#define SWP_ENTRY(type,offset) (((type) << 1) | ((offset) << 12))

int min_free_pages = 20;

static int nr_swapfiles = 0;
static struct wait_queue * lock_queue = NULL;

static struct swap_info_struct {
	unsigned long flags;
	struct inode * swap_file;
	unsigned int swap_device;
	unsigned char * swap_map;
	unsigned char * swap_lockmap;
	int pages;
	int lowest_bit;
	int highest_bit;
	unsigned long max;
} swap_info[MAX_SWAPFILES];

extern int shm_swap (int);

unsigned long *swap_cache;

#ifdef SWAP_CACHE_INFO
unsigned long swap_cache_add_total = 0;
unsigned long swap_cache_add_success = 0;
unsigned long swap_cache_del_total = 0;
unsigned long swap_cache_del_success = 0;
unsigned long swap_cache_find_total = 0;
unsigned long swap_cache_find_success = 0;

extern inline void show_swap_cache_info(void)
{
	printk("Swap cache: add %ld/%ld, delete %ld/%ld, find %ld/%ld\n",
		swap_cache_add_total, swap_cache_add_success, 
		swap_cache_del_total, swap_cache_del_success,
		swap_cache_find_total, swap_cache_find_success);
}
#endif

static int add_to_swap_cache(unsigned long addr, unsigned long entry)
{
	struct swap_info_struct * p = &swap_info[SWP_TYPE(entry)];

#ifdef SWAP_CACHE_INFO
	swap_cache_add_total++;
#endif
	if ((p->flags & SWP_WRITEOK) == SWP_WRITEOK) {
		entry = (unsigned long) xchg_ptr(swap_cache + MAP_NR(addr), (void *) entry);
		if (entry)  {
			printk("swap_cache: replacing non-NULL entry\n");
		}
#ifdef SWAP_CACHE_INFO
		swap_cache_add_success++;
#endif
		return 1;
	}
	return 0;
}

static unsigned long init_swap_cache(unsigned long mem_start,
	unsigned long mem_end)
{
	unsigned long swap_cache_size;

	mem_start = (mem_start + 15) & ~15;
	swap_cache = (unsigned long *) mem_start;
	swap_cache_size = MAP_NR(mem_end);
	memset(swap_cache, 0, swap_cache_size * sizeof (unsigned long));
	return (unsigned long) (swap_cache + swap_cache_size);
}

void rw_swap_page(int rw, unsigned long entry, char * buf)
{
	unsigned long type, offset;
	struct swap_info_struct * p;

	type = SWP_TYPE(entry);
	if (type >= nr_swapfiles) {
		printk("Internal error: bad swap-device\n");
		return;
	}
	p = &swap_info[type];
	offset = SWP_OFFSET(entry);
	if (offset >= p->max) {
		printk("rw_swap_page: weirdness\n");
		return;
	}
	if (p->swap_map && !p->swap_map[offset]) {
		printk("Hmm.. Trying to use unallocated swap (%08lx)\n", entry);
		return;
	}
	if (!(p->flags & SWP_USED)) {
		printk("Trying to swap to unused swap-device\n");
		return;
	}
	while (set_bit(offset,p->swap_lockmap))
		sleep_on(&lock_queue);
	if (rw == READ)
		kstat.pswpin++;
	else
		kstat.pswpout++;
	if (p->swap_device) {
		ll_rw_page(rw,p->swap_device,offset,buf);
	} else if (p->swap_file) {
		struct inode *swapf = p->swap_file;
		unsigned int zones[8];
		int i;
		if (swapf->i_op->bmap == NULL
			&& swapf->i_op->smap != NULL){
			/*
				With MsDOS, we use msdos_smap which return
				a sector number (not a cluster or block number).
				It is a patch to enable the UMSDOS project.
				Other people are working on better solution.

				It sounds like ll_rw_swap_file defined
				it operation size (sector size) based on
				PAGE_SIZE and the number of block to read.
				So using bmap or smap should work even if
				smap will require more blocks.
			*/
			int j;
			unsigned int block = offset << 3;

			for (i=0, j=0; j< PAGE_SIZE ; i++, j += 512){
				if (!(zones[i] = swapf->i_op->smap(swapf,block++))) {
					printk("rw_swap_page: bad swap file\n");
					return;
				}
			}
		}else{
			int j;
			unsigned int block = offset
				<< (12 - swapf->i_sb->s_blocksize_bits);

			for (i=0, j=0; j< PAGE_SIZE ; i++, j +=swapf->i_sb->s_blocksize)
				if (!(zones[i] = bmap(swapf,block++))) {
					printk("rw_swap_page: bad swap file\n");
					return;
				}
		}
		ll_rw_swap_file(rw,swapf->i_dev, zones, i,buf);
	} else
		printk("re_swap_page: no swap file or device\n");
	if (offset && !clear_bit(offset,p->swap_lockmap))
		printk("rw_swap_page: lock already cleared\n");
	wake_up(&lock_queue);
}

unsigned int get_swap_page(void)
{
	struct swap_info_struct * p;
	unsigned int offset, type;

	p = swap_info;
	for (type = 0 ; type < nr_swapfiles ; type++,p++) {
		if ((p->flags & SWP_WRITEOK) != SWP_WRITEOK)
			continue;
		for (offset = p->lowest_bit; offset <= p->highest_bit ; offset++) {
			if (p->swap_map[offset])
				continue;
			if (test_bit(offset, p->swap_lockmap))
				continue;
			p->swap_map[offset] = 1;
			nr_swap_pages--;
			if (offset == p->highest_bit)
				p->highest_bit--;
			p->lowest_bit = offset;
			return SWP_ENTRY(type,offset);
		}
	}
	return 0;
}

void swap_duplicate(unsigned long entry)
{
	struct swap_info_struct * p;
	unsigned long offset, type;

	if (!entry)
		return;
	offset = SWP_OFFSET(entry);
	type = SWP_TYPE(entry);
	if (type == SHM_SWP_TYPE)
		return;
	if (type >= nr_swapfiles) {
		printk("Trying to duplicate nonexistent swap-page\n");
		return;
	}
	p = type + swap_info;
	if (offset >= p->max) {
		printk("swap_duplicate: weirdness\n");
		return;
	}
	if (!p->swap_map[offset]) {
		printk("swap_duplicate: trying to duplicate unused page\n");
		return;
	}
	p->swap_map[offset]++;
	return;
}

void swap_free(unsigned long entry)
{
	struct swap_info_struct * p;
	unsigned long offset, type;

	if (!entry)
		return;
	type = SWP_TYPE(entry);
	if (type == SHM_SWP_TYPE)
		return;
	if (type >= nr_swapfiles) {
		printk("Trying to free nonexistent swap-page\n");
		return;
	}
	p = & swap_info[type];
	offset = SWP_OFFSET(entry);
	if (offset >= p->max) {
		printk("swap_free: weirdness\n");
		return;
	}
	if (!(p->flags & SWP_USED)) {
		printk("Trying to free swap from unused swap-device\n");
		return;
	}
	if (offset < p->lowest_bit)
		p->lowest_bit = offset;
	if (offset > p->highest_bit)
		p->highest_bit = offset;
	if (!p->swap_map[offset])
		printk("swap_free: swap-space map bad (entry %08lx)\n",entry);
	else
		if (!--p->swap_map[offset])
			nr_swap_pages++;
}

/*
 * The tests may look silly, but it essentially makes sure that
 * no other process did a swap-in on us just as we were waiting.
 *
 * Also, don't bother to add to the swap cache if this page-in
 * was due to a write access.
 */
void swap_in(struct vm_area_struct * vma, pte_t * page_table,
	unsigned long entry, int write_access)
{
	unsigned long page = get_free_page(GFP_KERNEL);

	if (pte_val(*page_table) != entry) {
		free_page(page);
		return;
	}
	if (!page) {
		*page_table = BAD_PAGE;
		swap_free(entry);
		oom(current);
		return;
	}
	read_swap_page(entry, (char *) page);
	if (pte_val(*page_table) != entry) {
		free_page(page);
		return;
	}
	vma->vm_task->mm->rss++;
	vma->vm_task->mm->maj_flt++;
	if (!write_access && add_to_swap_cache(page, entry)) {
		*page_table = mk_pte(page, vma->vm_page_prot);
		return;
	}
	*page_table = pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
  	swap_free(entry);
  	return;
}

/*
 * The swap-out functions return 1 of they successfully
 * threw something out, and we got a free page. It returns
 * zero if it couldn't do anything, and any other value
 * indicates it decreased rss, but the page was shared.
 *
 * NOTE! If it sleeps, it *must* return 1 to make sure we
 * don't continue with the swap-out. Otherwise we may be
 * using a process that no longer actually exists (it might
 * have died while we slept).
 */
static inline int try_to_swap_out(struct vm_area_struct* vma, unsigned long address, pte_t * page_table)
{
	pte_t pte;
	unsigned long entry;
	unsigned long page;

	pte = *page_table;
	if (!pte_present(pte))
		return 0;
	page = pte_page(pte);
	if (page >= high_memory)
		return 0;
	if (mem_map[MAP_NR(page)] & MAP_PAGE_RESERVED)
		return 0;
	if ((pte_dirty(pte) && delete_from_swap_cache(page)) || pte_young(pte))  {
		*page_table = pte_mkold(pte);
		return 0;
	}	
	if (pte_dirty(pte)) {
		if (mem_map[MAP_NR(page)] != 1)
			return 0;
		if (vma->vm_ops && vma->vm_ops->swapout) {
			vma->vm_task->mm->rss--;
			vma->vm_ops->swapout(vma, address-vma->vm_start, page_table);
		} else {
			if (!(entry = get_swap_page()))
				return 0;
			vma->vm_task->mm->rss--;
			pte_val(*page_table) = entry;
			invalidate();
			write_swap_page(entry, (char *) page);
		}
		free_page(page);
		return 1;	/* we slept: the process may not exist any more */
	}
        if ((entry = find_in_swap_cache(page)))  {
		if (mem_map[MAP_NR(page)] != 1) {
			*page_table = pte_mkdirty(pte);
			printk("Aiee.. duplicated cached swap-cache entry\n");
			return 0;
		}
		vma->vm_task->mm->rss--;
		pte_val(*page_table) = entry;
		invalidate();
		free_page(page);
		return 1;
	} 
	vma->vm_task->mm->rss--;
	pte_clear(page_table);
	invalidate();
	entry = mem_map[MAP_NR(page)];
	free_page(page);
	return entry;
}

/*
 * A new implementation of swap_out().  We do not swap complete processes,
 * but only a small number of blocks, before we continue with the next
 * process.  The number of blocks actually swapped is determined on the
 * number of page faults, that this process actually had in the last time,
 * so we won't swap heavily used processes all the time ...
 *
 * Note: the priority argument is a hint on much CPU to waste with the
 *       swap block search, not a hint, of how much blocks to swap with
 *       each process.
 *
 * (C) 1993 Kai Petzke, wpp@marie.physik.tu-berlin.de
 */

/*
 * These are the minimum and maximum number of pages to swap from one process,
 * before proceeding to the next:
 */
#define SWAP_MIN	4
#define SWAP_MAX	32

/*
 * The actual number of pages to swap is determined as:
 * SWAP_RATIO / (number of recent major page faults)
 */
#define SWAP_RATIO	128

static inline int swap_out_pmd(struct vm_area_struct * vma, pmd_t *dir,
	unsigned long address, unsigned long end)
{
	pte_t * pte;
	unsigned long pmd_end;

	if (pmd_none(*dir))
		return 0;
	if (pmd_bad(*dir)) {
		printk("swap_out_pmd: bad pmd (%08lx)\n", pmd_val(*dir));
		pmd_clear(dir);
		return 0;
	}
	
	pte = pte_offset(dir, address);
	
	pmd_end = (address + PMD_SIZE) & PMD_MASK;
	if (end > pmd_end)
		end = pmd_end;

	do {
		int result;
		vma->vm_task->mm->swap_address = address + PAGE_SIZE;
		result = try_to_swap_out(vma, address, pte);
		if (result)
			return result;
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
	return 0;
}

static inline int swap_out_pgd(struct vm_area_struct * vma, pgd_t *dir,
	unsigned long address, unsigned long end)
{
	pmd_t * pmd;
	unsigned long pgd_end;

	if (pgd_none(*dir))
		return 0;
	if (pgd_bad(*dir)) {
		printk("swap_out_pgd: bad pgd (%08lx)\n", pgd_val(*dir));
		pgd_clear(dir);
		return 0;
	}

	pmd = pmd_offset(dir, address);

	pgd_end = (address + PGDIR_SIZE) & PGDIR_MASK;	
	if (end > pgd_end)
		end = pgd_end;
	
	do {
		int result = swap_out_pmd(vma, pmd, address, end);
		if (result)
			return result;
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

static int swap_out_vma(struct vm_area_struct * vma, pgd_t *pgdir,
	unsigned long start)
{
	unsigned long end;

	/* Don't swap out areas like shared memory which have their
	    own separate swapping mechanism. */
	if (vma->vm_flags & VM_SHM)
		return 0;

	end = vma->vm_end;
	while (start < end) {
		int result = swap_out_pgd(vma, pgdir, start, end);
		if (result)
			return result;
		start = (start + PGDIR_SIZE) & PGDIR_MASK;
		pgdir++;
	}
	return 0;
}

static int swap_out_process(struct task_struct * p)
{
	unsigned long address;
	struct vm_area_struct* vma;

	/*
	 * Go through process' page directory.
	 */
	address = p->mm->swap_address;
	p->mm->swap_address = 0;

	/*
	 * Find the proper vm-area
	 */
	vma = find_vma(p, address);
	if (!vma)
		return 0;
	if (address < vma->vm_start)
		address = vma->vm_start;

	for (;;) {
		int result = swap_out_vma(vma, pgd_offset(p, address), address);
		if (result)
			return result;
		vma = vma->vm_next;
		if (!vma)
			break;
		address = vma->vm_start;
	}
	p->mm->swap_address = 0;
	return 0;
}

static int swap_out(unsigned int priority)
{
	static int swap_task;
	int loop, counter;
	struct task_struct *p;

	counter = 6*nr_tasks >> priority;
	for(; counter >= 0; counter--) {
		/*
		 * Check that swap_task is suitable for swapping.  If not, look for
		 * the next suitable process.
		 */
		loop = 0;
		while(1) {
			if (swap_task >= NR_TASKS) {
				swap_task = 1;
				if (loop)
					/* all processes are unswappable or already swapped out */
					return 0;
				loop = 1;
			}

			p = task[swap_task];
			if (p && p->mm->swappable && p->mm->rss)
				break;

			swap_task++;
		}

		/*
		 * Determine the number of pages to swap from this process.
		 */
		if (!p->mm->swap_cnt) {
			p->mm->dec_flt = (p->mm->dec_flt * 3) / 4 + p->mm->maj_flt - p->mm->old_maj_flt;
			p->mm->old_maj_flt = p->mm->maj_flt;

			if (p->mm->dec_flt >= SWAP_RATIO / SWAP_MIN) {
				p->mm->dec_flt = SWAP_RATIO / SWAP_MIN;
				p->mm->swap_cnt = SWAP_MIN;
			} else if (p->mm->dec_flt <= SWAP_RATIO / SWAP_MAX)
				p->mm->swap_cnt = SWAP_MAX;
			else
				p->mm->swap_cnt = SWAP_RATIO / p->mm->dec_flt;
		}
		if (!--p->mm->swap_cnt)
			swap_task++;
		switch (swap_out_process(p)) {
			case 0:
				if (p->mm->swap_cnt)
					swap_task++;
				break;
			case 1:
				return 1;
			default:
				break;
		}
	}
	return 0;
}

/*
 * we keep on shrinking one resource until it's considered "too hard",
 * and then switch to the next one (priority being an indication on how
 * hard we should try with the resource).
 *
 * This should automatically find the resource that can most easily be
 * free'd, so hopefully we'll get reasonable behaviour even under very
 * different circumstances.
 */
static int try_to_free_page(int priority)
{
	static int state = 0;
	int i=6;

	switch (state) {
		do {
		case 0:
			if (priority != GFP_NOBUFFER && shrink_buffers(i))
				return 1;
			state = 1;
		case 1:
			if (shm_swap(i))
				return 1;
			state = 2;
		default:
			if (swap_out(i))
				return 1;
			state = 0;
		} while(i--);
	}
	return 0;
}

static inline void add_mem_queue(struct mem_list * head, struct mem_list * entry)
{
	entry->prev = head;
	(entry->next = head->next)->prev = entry;
	head->next = entry;
}

static inline void remove_mem_queue(struct mem_list * head, struct mem_list * entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
}

/*
 * Free_page() adds the page to the free lists. This is optimized for
 * fast normal cases (no error jumps taken normally).
 *
 * The way to optimize jumps for gcc-2.2.2 is to:
 *  - select the "normal" case and put it inside the if () { XXX }
 *  - no else-statements if you can avoid them
 *
 * With the above two rules, you get a straight-line execution path
 * for the normal case, giving better asm-code.
 *
 * free_page() may sleep since the page being freed may be a buffer
 * page or present in the swap cache. It will not sleep, however,
 * for a freshly allocated page (get_free_page()).
 */

/*
 * Buddy system. Hairy. You really aren't expected to understand this
 */
static inline void free_pages_ok(unsigned long addr, unsigned long order)
{
	unsigned long index = MAP_NR(addr) >> (1 + order);
	unsigned long mask = PAGE_MASK << order;

	addr &= mask;
	nr_free_pages += 1 << order;
	while (order < NR_MEM_LISTS-1) {
		if (!change_bit(index, free_area_map[order]))
			break;
		remove_mem_queue(free_area_list+order, (struct mem_list *) (addr ^ (1+~mask)));
		order++;
		index >>= 1;
		mask <<= 1;
		addr &= mask;
	}
	add_mem_queue(free_area_list+order, (struct mem_list *) addr);
}

static inline void check_free_buffers(unsigned long addr)
{
	struct buffer_head * bh;

	bh = buffer_pages[MAP_NR(addr)];
	if (bh) {
		struct buffer_head *tmp = bh;
		do {
			if (tmp->b_list == BUF_SHARED && tmp->b_dev != 0xffff)
				refile_buffer(tmp);
			tmp = tmp->b_this_page;
		} while (tmp != bh);
	}
}

void free_pages(unsigned long addr, unsigned long order)
{
	if (addr < high_memory) {
		unsigned long flag;
		mem_map_t * map = mem_map + MAP_NR(addr);
		if (*map) {
			if (!(*map & MAP_PAGE_RESERVED)) {
				save_flags(flag);
				cli();
				if (!--*map)  {
					free_pages_ok(addr, order);
					delete_from_swap_cache(addr);
				}
				restore_flags(flag);
				if (*map == 1)
					check_free_buffers(addr);
			}
			return;
		}
		printk("Trying to free free memory (%08lx): memory probably corrupted\n",addr);
		printk("PC = %p\n", __builtin_return_address(0));
		return;
	}
}

/*
 * Some ugly macros to speed up __get_free_pages()..
 */
#define RMQUEUE(order) \
do { struct mem_list * queue = free_area_list+order; \
     unsigned long new_order = order; \
	do { struct mem_list *next = queue->next; \
		if (queue != next) { \
			(queue->next = next->next)->prev = queue; \
			mark_used((unsigned long) next, new_order); \
			nr_free_pages -= 1 << order; \
			restore_flags(flags); \
			EXPAND(next, order, new_order); \
			return (unsigned long) next; \
		} new_order++; queue++; \
	} while (new_order < NR_MEM_LISTS); \
} while (0)

static inline int mark_used(unsigned long addr, unsigned long order)
{
	return change_bit(MAP_NR(addr) >> (1+order), free_area_map[order]);
}

#define EXPAND(addr,low,high) \
do { unsigned long size = PAGE_SIZE << high; \
	while (high > low) { \
		high--; size >>= 1; cli(); \
		add_mem_queue(free_area_list+high, addr); \
		mark_used((unsigned long) addr, high); \
		restore_flags(flags); \
		addr = (struct mem_list *) (size + (unsigned long) addr); \
	} mem_map[MAP_NR((unsigned long) addr)] = 1; \
} while (0)

unsigned long __get_free_pages(int priority, unsigned long order)
{
	unsigned long flags;
	int reserved_pages;

	if (intr_count && priority != GFP_ATOMIC) {
		static int count = 0;
		if (++count < 5) {
			printk("gfp called nonatomically from interrupt %p\n",
				__builtin_return_address(0));
			priority = GFP_ATOMIC;
		}
	}
	reserved_pages = 5;
	if (priority != GFP_NFS)
		reserved_pages = min_free_pages;
	save_flags(flags);
repeat:
	cli();
	if ((priority==GFP_ATOMIC) || nr_free_pages > reserved_pages) {
		RMQUEUE(order);
		restore_flags(flags);
		return 0;
	}
	restore_flags(flags);
	if (priority != GFP_BUFFER && try_to_free_page(priority))
		goto repeat;
	return 0;
}

/*
 * Yes, I know this is ugly. Don't tell me.
 */
unsigned long __get_dma_pages(int priority, unsigned long order)
{
	unsigned long list = 0;
	unsigned long result;
	unsigned long limit = MAX_DMA_ADDRESS;

	/* if (EISA_bus) limit = ~0UL; */
	if (priority != GFP_ATOMIC)
		priority = GFP_BUFFER;
	for (;;) {
		result = __get_free_pages(priority, order);
		if (result < limit) /* covers failure as well */
			break;
		*(unsigned long *) result = list;
		list = result;
	}
	while (list) {
		unsigned long tmp = list;
		list = *(unsigned long *) list;
		free_pages(tmp, order);
	}
	return result;
}

/*
 * Show free area list (used inside shift_scroll-lock stuff)
 * We also calculate the percentage fragmentation. We do this by counting the
 * memory on each free list with the exception of the first item on the list.
 */
void show_free_areas(void)
{
 	unsigned long order, flags;
 	unsigned long total = 0;

	printk("Free pages:      %6dkB\n ( ",nr_free_pages<<(PAGE_SHIFT-10));
	save_flags(flags);
	cli();
 	for (order=0 ; order < NR_MEM_LISTS; order++) {
		struct mem_list * tmp;
		unsigned long nr = 0;
		for (tmp = free_area_list[order].next ; tmp != free_area_list + order ; tmp = tmp->next) {
			nr ++;
		}
		total += nr * ((PAGE_SIZE>>10) << order);
		printk("%lu*%lukB ", nr, (PAGE_SIZE>>10) << order);
	}
	restore_flags(flags);
	printk("= %lukB)\n", total);
#ifdef SWAP_CACHE_INFO
	show_swap_cache_info();
#endif	
}

/*
 * Trying to stop swapping from a file is fraught with races, so
 * we repeat quite a bit here when we have to pause. swapoff()
 * isn't exactly timing-critical, so who cares (but this is /really/
 * inefficient, ugh).
 *
 * We return 1 after having slept, which makes the process start over
 * from the beginning for this process..
 */
static inline int unuse_pte(struct vm_area_struct * vma, unsigned long address,
	pte_t *dir, unsigned int type, unsigned long page)
{
	pte_t pte = *dir;

	if (pte_none(pte))
		return 0;
	if (pte_present(pte)) {
		unsigned long page = pte_page(pte);
		if (page >= high_memory)
			return 0;
		if (!in_swap_cache(page))
			return 0;
		if (SWP_TYPE(in_swap_cache(page)) != type)
			return 0;
		delete_from_swap_cache(page);
		*dir = pte_mkdirty(pte);
		return 0;
	}
	if (SWP_TYPE(pte_val(pte)) != type)
		return 0;
	read_swap_page(pte_val(pte), (char *) page);
	if (pte_val(*dir) != pte_val(pte)) {
		free_page(page);
		return 1;
	}
	*dir = pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	++vma->vm_task->mm->rss;
	swap_free(pte_val(pte));
	return 1;
}

static inline int unuse_pmd(struct vm_area_struct * vma, pmd_t *dir,
	unsigned long address, unsigned long size, unsigned long offset,
	unsigned int type, unsigned long page)
{
	pte_t * pte;
	unsigned long end;

	if (pmd_none(*dir))
		return 0;
	if (pmd_bad(*dir)) {
		printk("unuse_pmd: bad pmd (%08lx)\n", pmd_val(*dir));
		pmd_clear(dir);
		return 0;
	}
	pte = pte_offset(dir, address);
	offset += address & PMD_MASK;
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		if (unuse_pte(vma, offset+address-vma->vm_start, pte, type, page))
			return 1;
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
	return 0;
}

static inline int unuse_pgd(struct vm_area_struct * vma, pgd_t *dir,
	unsigned long address, unsigned long size,
	unsigned int type, unsigned long page)
{
	pmd_t * pmd;
	unsigned long offset, end;

	if (pgd_none(*dir))
		return 0;
	if (pgd_bad(*dir)) {
		printk("unuse_pgd: bad pgd (%08lx)\n", pgd_val(*dir));
		pgd_clear(dir);
		return 0;
	}
	pmd = pmd_offset(dir, address);
	offset = address & PGDIR_MASK;
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		if (unuse_pmd(vma, pmd, address, end - address, offset, type, page))
			return 1;
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

static int unuse_vma(struct vm_area_struct * vma, pgd_t *pgdir,
	unsigned long start, unsigned long end,
	unsigned int type, unsigned long page)
{
	while (start < end) {
		if (unuse_pgd(vma, pgdir, start, end - start, type, page))
			return 1;
		start = (start + PGDIR_SIZE) & PGDIR_MASK;
		pgdir++;
	}
	return 0;
}

static int unuse_process(struct task_struct * p, unsigned int type, unsigned long page)
{
	struct vm_area_struct* vma;

	/*
	 * Go through process' page directory.
	 */
	vma = p->mm->mmap;
	while (vma) {
		pgd_t * pgd = pgd_offset(p, vma->vm_start);
		if (unuse_vma(vma, pgd, vma->vm_start, vma->vm_end, type, page))
			return 1;
		vma = vma->vm_next;
	}
	return 0;
}

/*
 * To avoid races, we repeat for each process after having
 * swapped something in. That gets rid of a few pesky races,
 * and "swapoff" isn't exactly timing critical.
 */
static int try_to_unuse(unsigned int type)
{
	int nr;
	unsigned long page = get_free_page(GFP_KERNEL);

	if (!page)
		return -ENOMEM;
	nr = 0;
	while (nr < NR_TASKS) {
		if (task[nr]) {
			if (unuse_process(task[nr], type, page)) {
				page = get_free_page(GFP_KERNEL);
				if (!page)
					return -ENOMEM;
				continue;
			}
		}
		nr++;
	}
	free_page(page);
	return 0;
}

asmlinkage int sys_swapoff(const char * specialfile)
{
	struct swap_info_struct * p;
	struct inode * inode;
	unsigned int type;
	struct file filp;
	int i;

	if (!suser())
		return -EPERM;
	i = namei(specialfile,&inode);
	if (i)
		return i;
	p = swap_info;
	for (type = 0 ; type < nr_swapfiles ; type++,p++) {
		if ((p->flags & SWP_WRITEOK) != SWP_WRITEOK)
			continue;
		if (p->swap_file) {
			if (p->swap_file == inode)
				break;
		} else {
			if (!S_ISBLK(inode->i_mode))
				continue;
			if (p->swap_device == inode->i_rdev)
				break;
		}
	}

	if (type >= nr_swapfiles){
		iput(inode);
		return -EINVAL;
	}
	p->flags = SWP_USED;
	i = try_to_unuse(type);
	if (i) {
		iput(inode);
		p->flags = SWP_WRITEOK;
		return i;
	}

	if(p->swap_device){
		memset(&filp, 0, sizeof(filp));		
		filp.f_inode = inode;
		filp.f_mode = 3; /* read write */
		/* open it again to get fops */
		if( !blkdev_open(inode, &filp) &&
		   filp.f_op && filp.f_op->release){
			filp.f_op->release(inode,&filp);
			filp.f_op->release(inode,&filp);
		}
	}
	iput(inode);

	nr_swap_pages -= p->pages;
	iput(p->swap_file);
	p->swap_file = NULL;
	p->swap_device = 0;
	vfree(p->swap_map);
	p->swap_map = NULL;
	free_page((long) p->swap_lockmap);
	p->swap_lockmap = NULL;
	p->flags = 0;
	return 0;
}

/*
 * Written 01/25/92 by Simmule Turner, heavily changed by Linus.
 *
 * The swapon system call
 */
asmlinkage int sys_swapon(const char * specialfile)
{
	struct swap_info_struct * p;
	struct inode * swap_inode;
	unsigned int type;
	int i,j;
	int error;
	struct file filp;

	memset(&filp, 0, sizeof(filp));
	if (!suser())
		return -EPERM;
	p = swap_info;
	for (type = 0 ; type < nr_swapfiles ; type++,p++)
		if (!(p->flags & SWP_USED))
			break;
	if (type >= MAX_SWAPFILES)
		return -EPERM;
	if (type >= nr_swapfiles)
		nr_swapfiles = type+1;
	p->flags = SWP_USED;
	p->swap_file = NULL;
	p->swap_device = 0;
	p->swap_map = NULL;
	p->swap_lockmap = NULL;
	p->lowest_bit = 0;
	p->highest_bit = 0;
	p->max = 1;
	error = namei(specialfile,&swap_inode);
	if (error)
		goto bad_swap_2;
	p->swap_file = swap_inode;
	error = -EBUSY;
	if (swap_inode->i_count != 1)
		goto bad_swap_2;
	error = -EINVAL;

	if (S_ISBLK(swap_inode->i_mode)) {
		p->swap_device = swap_inode->i_rdev;

		filp.f_inode = swap_inode;
		filp.f_mode = 3; /* read write */
		error = blkdev_open(swap_inode, &filp);
		p->swap_file = NULL;
		iput(swap_inode);
		if(error)
			goto bad_swap_2;
		error = -ENODEV;
		if (!p->swap_device)
			goto bad_swap;
		error = -EBUSY;
		for (i = 0 ; i < nr_swapfiles ; i++) {
			if (i == type)
				continue;
			if (p->swap_device == swap_info[i].swap_device)
				goto bad_swap;
		}
	} else if (!S_ISREG(swap_inode->i_mode))
		goto bad_swap;
	p->swap_lockmap = (unsigned char *) get_free_page(GFP_USER);
	if (!p->swap_lockmap) {
		printk("Unable to start swapping: out of memory :-)\n");
		error = -ENOMEM;
		goto bad_swap;
	}
	read_swap_page(SWP_ENTRY(type,0), (char *) p->swap_lockmap);
	if (memcmp("SWAP-SPACE",p->swap_lockmap+4086,10)) {
		printk("Unable to find swap-space signature\n");
		error = -EINVAL;
		goto bad_swap;
	}
	memset(p->swap_lockmap+PAGE_SIZE-10,0,10);
	j = 0;
	p->lowest_bit = 0;
	p->highest_bit = 0;
	for (i = 1 ; i < 8*PAGE_SIZE ; i++) {
		if (test_bit(i,p->swap_lockmap)) {
			if (!p->lowest_bit)
				p->lowest_bit = i;
			p->highest_bit = i;
			p->max = i+1;
			j++;
		}
	}
	if (!j) {
		printk("Empty swap-file\n");
		error = -EINVAL;
		goto bad_swap;
	}
	p->swap_map = (unsigned char *) vmalloc(p->max);
	if (!p->swap_map) {
		error = -ENOMEM;
		goto bad_swap;
	}
	for (i = 1 ; i < p->max ; i++) {
		if (test_bit(i,p->swap_lockmap))
			p->swap_map[i] = 0;
		else
			p->swap_map[i] = 0x80;
	}
	p->swap_map[0] = 0x80;
	memset(p->swap_lockmap,0,PAGE_SIZE);
	p->flags = SWP_WRITEOK;
	p->pages = j;
	nr_swap_pages += j;
	printk("Adding Swap: %dk swap-space\n",j<<2);
	return 0;
bad_swap:
	if(filp.f_op && filp.f_op->release)
		filp.f_op->release(filp.f_inode,&filp);
bad_swap_2:
	free_page((long) p->swap_lockmap);
	vfree(p->swap_map);
	iput(p->swap_file);
	p->swap_device = 0;
	p->swap_file = NULL;
	p->swap_map = NULL;
	p->swap_lockmap = NULL;
	p->flags = 0;
	return error;
}

void si_swapinfo(struct sysinfo *val)
{
	unsigned int i, j;

	val->freeswap = val->totalswap = 0;
	for (i = 0; i < nr_swapfiles; i++) {
		if ((swap_info[i].flags & SWP_WRITEOK) != SWP_WRITEOK)
			continue;
		for (j = 0; j < swap_info[i].max; ++j)
			switch (swap_info[i].swap_map[j]) {
				case 128:
					continue;
				case 0:
					++val->freeswap;
				default:
					++val->totalswap;
			}
	}
	val->freeswap <<= PAGE_SHIFT;
	val->totalswap <<= PAGE_SHIFT;
	return;
}

/*
 * set up the free-area data structures:
 *   - mark all pages MAP_PAGE_RESERVED
 *   - mark all memory queues empty
 *   - clear the memory bitmaps
 */
unsigned long free_area_init(unsigned long start_mem, unsigned long end_mem)
{
	mem_map_t * p;
	unsigned long mask = PAGE_MASK;
	int i;

	/*
	 * select nr of pages we try to keep free for important stuff
	 * with a minimum of 16 pages. This is totally arbitrary
	 */
	i = (end_mem - PAGE_OFFSET) >> (PAGE_SHIFT+6);
	if (i < 16)
		i = 16;
	min_free_pages = i;
	start_mem = init_swap_cache(start_mem, end_mem);
	mem_map = (mem_map_t *) start_mem;
	p = mem_map + MAP_NR(end_mem);
	start_mem = (unsigned long) p;
	while (p > mem_map)
		*--p = MAP_PAGE_RESERVED;

	for (i = 0 ; i < NR_MEM_LISTS ; i++) {
		unsigned long bitmap_size;
		free_area_list[i].prev = free_area_list[i].next = &free_area_list[i];
		mask += mask;
		end_mem = (end_mem + ~mask) & mask;
		bitmap_size = (end_mem - PAGE_OFFSET) >> (PAGE_SHIFT + i);
		bitmap_size = (bitmap_size + 7) >> 3;
		bitmap_size = (bitmap_size + sizeof(unsigned long) - 1) & ~(sizeof(unsigned long)-1);
		free_area_map[i] = (unsigned char *) start_mem;
		memset((void *) start_mem, 0, bitmap_size);
		start_mem += bitmap_size;
	}
	return start_mem;
}

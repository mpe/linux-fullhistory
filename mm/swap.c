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

#include <asm/system.h> /* for cli()/sti() */
#include <asm/bitops.h>

#define MAX_SWAPFILES 8

#define SWP_USED	1
#define SWP_WRITEOK	3

#define SWP_TYPE(entry) (((entry) & 0xfe) >> 1)
#define SWP_OFFSET(entry) ((entry) >> PAGE_SHIFT)
#define SWP_ENTRY(type,offset) (((type) << 1) | ((offset) << PAGE_SHIFT))

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

extern inline int add_to_swap_cache(unsigned long addr, unsigned long entry)
{
	struct swap_info_struct * p = &swap_info[SWP_TYPE(entry)];
	
#ifdef SWAP_CACHE_INFO
	swap_cache_add_total++;
#endif
	if ((p->flags & SWP_WRITEOK) == SWP_WRITEOK) { 
		__asm__ __volatile__ (
				      "xchgl %0,%1\n"
				      : "=m" (swap_cache[addr >> PAGE_SHIFT]),
				       "=r" (entry)
				      : "0" (swap_cache[addr >> PAGE_SHIFT]),
				       "1" (entry)
				      );
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
	swap_cache_size = mem_end >> PAGE_SHIFT;
	memset(swap_cache, 0, swap_cache_size * sizeof (unsigned long));
#ifdef SWAP_CACHE_INFO
	printk("%ld bytes for swap cache allocated\n",
	       swap_cache_size * sizeof(unsigned long));
#endif	
	
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
				So using bmap ou smap should work even if
				smap will requiered more blocks.
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

unsigned long swap_duplicate(unsigned long entry)
{
	struct swap_info_struct * p;
	unsigned long offset, type;

	if (!entry)
		return 0;
	offset = SWP_OFFSET(entry);
	type = SWP_TYPE(entry);
	if (type == SHM_SWP_TYPE)
		return entry;
	if (type >= nr_swapfiles) {
		printk("Trying to duplicate nonexistent swap-page\n");
		return 0;
	}
	p = type + swap_info;
	if (offset >= p->max) {
		printk("swap_duplicate: weirdness\n");
		return 0;
	}
	if (!p->swap_map[offset]) {
		printk("swap_duplicate: trying to duplicate unused page\n");
		return 0;
	}
	p->swap_map[offset]++;
	return entry;
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
	while (set_bit(offset,p->swap_lockmap))
		sleep_on(&lock_queue);
	if (offset < p->lowest_bit)
		p->lowest_bit = offset;
	if (offset > p->highest_bit)
		p->highest_bit = offset;
	if (!p->swap_map[offset])
		printk("swap_free: swap-space map bad (entry %08lx)\n",entry);
	else
		if (!--p->swap_map[offset])
			nr_swap_pages++;
	if (!clear_bit(offset,p->swap_lockmap))
		printk("swap_free: lock already cleared\n");
	wake_up(&lock_queue);
}

unsigned long swap_in(unsigned long entry)
{
	unsigned long page;

	if (!(page = get_free_page(GFP_KERNEL))) {
		oom(current);
		return BAD_PAGE;
	}
	read_swap_page(entry, (char *) page);
	if (add_to_swap_cache(page, entry))
		return page | PAGE_PRIVATE;
  	swap_free(entry);
	return page | PAGE_DIRTY | PAGE_PRIVATE;
}

static inline int try_to_swap_out(unsigned long * table_ptr)
{
	unsigned long page, entry;

	page = *table_ptr;
	if (!(PAGE_PRESENT & page))
		return 0;
	if (page >= high_memory)
		return 0;
	if (mem_map[MAP_NR(page)] & MAP_PAGE_RESERVED)
		return 0;
	
	if ((PAGE_DIRTY & page) && delete_from_swap_cache(page))  {
		return 0;
	}
	if (PAGE_ACCESSED & page) {
		*table_ptr &= ~PAGE_ACCESSED;
		return 0;
	}
	if (PAGE_DIRTY & page) {
		page &= PAGE_MASK;
		if (mem_map[MAP_NR(page)] != 1)
			return 0;
		if (!(entry = get_swap_page()))
			return 0;
		*table_ptr = entry;
		invalidate();
		write_swap_page(entry, (char *) page);
		free_page(page);
		return 1;
	}

        if ((entry = find_in_swap_cache(page)))  {
		if (mem_map[MAP_NR(page)] != 1) {
			*table_ptr |= PAGE_DIRTY;
			printk("Aiee.. duplicated cached swap-cache entry\n");
			return 0;
		}
		*table_ptr = entry;
		invalidate();
		free_page(page & PAGE_MASK);
		return 1;
	} 
	page &= PAGE_MASK;
	*table_ptr = 0;
	invalidate();
	free_page(page);
	return 1 + mem_map[MAP_NR(page)];
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
#ifdef NEW_SWAP
/*
 * These are the miminum and maximum number of pages to swap from one process,
 * before proceeding to the next:
 */
#define SWAP_MIN	4
#define SWAP_MAX	32

/*
 * The actual number of pages to swap is determined as:
 * SWAP_RATIO / (number of recent major page faults)
 */
#define SWAP_RATIO	128

static int swap_out(unsigned int priority)
{
    static int swap_task;
    int table;
    int page;
    long pg_table;
    int loop;
    int counter = NR_TASKS * 2 >> priority;
    struct task_struct *p;

    counter = NR_TASKS * 2 >> priority;
    for(; counter >= 0; counter--, swap_task++) {
	/*
	 * Check that swap_task is suitable for swapping.  If not, look for
	 * the next suitable process.
	 */
	loop = 0;
	while(1) {
	    if(swap_task >= NR_TASKS) {
		swap_task = 1;
		if(loop)
		    /* all processes are unswappable or already swapped out */
		    return 0;
		loop = 1;
	    }

	    p = task[swap_task];
	    if(p && p->mm->swappable && p->mm->rss)
		break;

	    swap_task++;
	}

	/*
	 * Determine the number of pages to swap from this process.
	 */
	if(! p->mm->swap_cnt) {
	    p->mm->dec_flt = (p->mm->dec_flt * 3) / 4 + p->mm->maj_flt - p->mm->old_maj_flt;
	    p->mm->old_maj_flt = p->mm->maj_flt;

	    if(p->mm->dec_flt >= SWAP_RATIO / SWAP_MIN) {
		p->mm->dec_flt = SWAP_RATIO / SWAP_MIN;
		p->mm->swap_cnt = SWAP_MIN;
	    } else if(p->mm->dec_flt <= SWAP_RATIO / SWAP_MAX)
		p->mm->swap_cnt = SWAP_MAX;
	    else
		p->mm->swap_cnt = SWAP_RATIO / p->mm->dec_flt;
	}

	/*
	 * Go through process' page directory.
	 */
	for(table = p->mm->swap_table; table < 1024; table++) {
	    pg_table = ((unsigned long *) p->tss.cr3)[table];
	    if(pg_table >= high_memory)
		    continue;
	    if(mem_map[MAP_NR(pg_table)] & MAP_PAGE_RESERVED)
		    continue;
	    if(!(PAGE_PRESENT & pg_table)) {
		    printk("swap_out: bad page-table at pg_dir[%d]: %08lx\n",
			    table, pg_table);
		    ((unsigned long *) p->tss.cr3)[table] = 0;
		    continue;
	    }
	    pg_table &= 0xfffff000;

	    /*
	     * Go through this page table.
	     */
	    for(page = p->mm->swap_page; page < 1024; page++) {
		switch(try_to_swap_out(page + (unsigned long *) pg_table)) {
		    case 0:
			break;

		    case 1:
			p->mm->rss--;
			/* continue with the following page the next time */
			p->mm->swap_table = table;
			p->mm->swap_page  = page + 1;
			if((--p->mm->swap_cnt) == 0)
			    swap_task++;
			return 1;

		    default:
			p->mm->rss--;
			break;
		}
	    }

	    p->mm->swap_page = 0;
	}

	/*
	 * Finish work with this process, if we reached the end of the page
	 * directory.  Mark restart from the beginning the next time.
	 */
	p->mm->swap_table = 0;
    }
    return 0;
}

#else /* old swapping procedure */

/*
 * Go through the page tables, searching for a user page that
 * we can swap out.
 * 
 * We now check that the process is swappable (normally only 'init'
 * is un-swappable), allowing high-priority processes which cannot be
 * swapped out (things like user-level device drivers (Not implemented)).
 */
static int swap_out(unsigned int priority)
{
	static int swap_task = 1;
	static int swap_table = 0;
	static int swap_page = 0;
	int counter = NR_TASKS*8;
	int pg_table;
	struct task_struct * p;

	counter >>= priority;
check_task:
	if (counter-- < 0)
		return 0;
	if (swap_task >= NR_TASKS) {
		swap_task = 1;
		goto check_task;
	}
	p = task[swap_task];
	if (!p || !p->mm->swappable) {
		swap_task++;
		goto check_task;
	}
check_dir:
	if (swap_table >= PTRS_PER_PAGE) {
		swap_table = 0;
		swap_task++;
		goto check_task;
	}
	pg_table = ((unsigned long *) p->tss.cr3)[swap_table];
	if (pg_table >= high_memory || (mem_map[MAP_NR(pg_table)] & MAP_PAGE_RESERVED)) {
		swap_table++;
		goto check_dir;
	}
	if (!(PAGE_PRESENT & pg_table)) {
		printk("bad page-table at pg_dir[%d]: %08x\n",
			swap_table,pg_table);
		((unsigned long *) p->tss.cr3)[swap_table] = 0;
		swap_table++;
		goto check_dir;
	}
	pg_table &= PAGE_MASK;
check_table:
	if (swap_page >= PTRS_PER_PAGE) {
		swap_page = 0;
		swap_table++;
		goto check_dir;
	}
	switch (try_to_swap_out(swap_page + (unsigned long *) pg_table)) {
		case 0: break;
		case 1: p->mm->rss--; return 1;
		default: p->mm->rss--;
	}
	swap_page++;
	goto check_table;
}

#endif

static int try_to_free_page(int priority)
{
	int i=6;

	while (i--) {
	        if (priority != GFP_NOBUFFER && shrink_buffers(i))
			return 1;
		if (shm_swap(i))
			return 1;
		if (swap_out(i))
			return 1;
	}
	return 0;
}

static inline void add_mem_queue(struct mem_list * head, struct mem_list * entry)
{
	entry->prev = head;
	entry->next = head->next;
	entry->next->prev = entry;
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
 */

/*
 * Buddy system. Hairy. You really aren't expected to understand this
 */
static inline void free_pages_ok(unsigned long addr, unsigned long order)
{
	unsigned long index = addr >> (PAGE_SHIFT + 1 + order);
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

void free_pages(unsigned long addr, unsigned long order)
{
	if (addr < high_memory) {
		unsigned long flag;
		unsigned short * map = mem_map + MAP_NR(addr);
		if (*map) {
			if (!(*map & MAP_PAGE_RESERVED)) {
				save_flags(flag);
				cli();
				if (!--*map)  {
					free_pages_ok(addr, order);
					delete_from_swap_cache(addr);
				}
				restore_flags(flag);
				if(*map == 1) {
				  int j;
				  struct buffer_head * bh, *tmp;

				  bh = buffer_pages[MAP_NR(addr)];
				  if(bh)
				    for(j = 0, tmp = bh; tmp && (!j || tmp != bh); 
					tmp = tmp->b_this_page, j++)
				      if(tmp->b_list == BUF_SHARED && tmp->b_dev != 0xffff)
					refile_buffer(tmp);
				}
			}
			return;
		}
		printk("Trying to free free memory (%08lx): memory probabably corrupted\n",addr);
		printk("PC = %08lx\n",*(((unsigned long *)&addr)-1));
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
			queue->next = next->next; \
			next->next->prev = queue; \
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
	return change_bit(addr >> (PAGE_SHIFT+1+order), free_area_map[order]);
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

	if (intr_count && priority != GFP_ATOMIC) {
		static int count = 0;
		if (++count < 5) {
			printk("gfp called nonatomically from interrupt %08lx\n",
				((unsigned long *)&priority)[-1]);
			priority = GFP_ATOMIC;
		}
	}
	save_flags(flags);
repeat:
	cli();
	if ((priority==GFP_ATOMIC) || nr_free_pages > MAX_SECONDARY_PAGES) {
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
		total += nr * (4 << order);
		printk("%lu*%ukB ", nr, 4 << order);
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
 * isn't exactly timing-critical, so who cares?
 */
static int try_to_unuse(unsigned int type)
{
	int nr, pgt, pg;
	unsigned long page, *ppage;
	unsigned long tmp = 0;
	struct task_struct *p;

	nr = 0;
	
/*
 * When we have to sleep, we restart the whole algorithm from the same
 * task we stopped in. That at least rids us of all races.
 */
repeat:
	for (; nr < NR_TASKS ; nr++) {
		p = task[nr];
		if (!p)
			continue;
		for (pgt = 0 ; pgt < PTRS_PER_PAGE ; pgt++) {
			ppage = pgt + ((unsigned long *) p->tss.cr3);
			page = *ppage;
			if (!page)
				continue;
			if (!(page & PAGE_PRESENT) || (page >= high_memory))
				continue;
			if (mem_map[MAP_NR(page)] & MAP_PAGE_RESERVED)
				continue;
			ppage = (unsigned long *) (page & PAGE_MASK);	
			for (pg = 0 ; pg < PTRS_PER_PAGE ; pg++,ppage++) {
				page = *ppage;
				if (!page)
					continue;
				if (page & PAGE_PRESENT) {
					if (!(page = in_swap_cache(page)))
						continue;
					if (SWP_TYPE(page) != type)
						continue;
					*ppage |= PAGE_DIRTY;
					delete_from_swap_cache(*ppage);
					continue;
				}
				if (SWP_TYPE(page) != type)
					continue;
				if (!tmp) {
					if (!(tmp = __get_free_page(GFP_KERNEL)))
						return -ENOMEM;
					goto repeat;
				}
				read_swap_page(page, (char *) tmp);
				if (*ppage == page) {
					*ppage = tmp | (PAGE_DIRTY | PAGE_PRIVATE);
					++p->mm->rss;
					swap_free(page);
					tmp = 0;
				}
				goto repeat;
			}
		}
	}
	free_page(tmp);
	return 0;
}

asmlinkage int sys_swapoff(const char * specialfile)
{
	struct swap_info_struct * p;
	struct inode * inode;
	unsigned int type;
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
	iput(inode);
	if (type >= nr_swapfiles)
		return -EINVAL;
	p->flags = SWP_USED;
	i = try_to_unuse(type);
	if (i) {
		p->flags = SWP_WRITEOK;
		return i;
	}
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
		goto bad_swap;
	p->swap_file = swap_inode;
	error = -EBUSY;
	if (swap_inode->i_count != 1)
		goto bad_swap;
	error = -EINVAL;
	if (S_ISBLK(swap_inode->i_mode)) {
		p->swap_device = swap_inode->i_rdev;
		p->swap_file = NULL;
		iput(swap_inode);
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
	unsigned short * p;
	unsigned long mask = PAGE_MASK;
	int i;

	start_mem = init_swap_cache(start_mem, end_mem);
	mem_map = (unsigned short *) start_mem;
	p = mem_map + MAP_NR(end_mem);
	start_mem = (unsigned long) p;
	while (p > mem_map)
		*--p = MAP_PAGE_RESERVED;

	for (i = 0 ; i < NR_MEM_LISTS ; i++, mask <<= 1) {
		unsigned long bitmap_size;
		free_area_list[i].prev = free_area_list[i].next = &free_area_list[i];
		end_mem = (end_mem + ~mask) & mask;
		bitmap_size = end_mem >> (PAGE_SHIFT + i);
		bitmap_size = (bitmap_size + 7) >> 3;
		free_area_map[i] = (unsigned char *) start_mem;
		memset((void *) start_mem, 0, bitmap_size);
		start_mem += bitmap_size;
	}
	return start_mem;
}

/*
 *  linux/mm/page_alloc.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/swapctl.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pagemap.h>

#include <asm/dma.h>
#include <asm/system.h> /* for cli()/sti() */
#include <asm/uaccess.h> /* for copy_to/from_user */
#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/spinlock.h>

int nr_swap_pages = 0;
int nr_free_pages = 0;

/*
 * Free area management
 *
 * The free_area_list arrays point to the queue heads of the free areas
 * of different sizes
 */

#if CONFIG_AP1000
/* the AP+ needs to allocate 8MB contiguous, aligned chunks of ram
   for the ring buffers */
#define NR_MEM_LISTS 12
#else
#define NR_MEM_LISTS 6
#endif

/* The start of this MUST match the start of "struct page" */
struct free_area_struct {
	struct page *next;
	struct page *prev;
	unsigned int * map;
};

#define memory_head(x) ((struct page *)(x))

static struct free_area_struct free_area[NR_MEM_LISTS];

static inline void init_mem_queue(struct free_area_struct * head)
{
	head->next = memory_head(head);
	head->prev = memory_head(head);
}

static inline void add_mem_queue(struct free_area_struct * head, struct page * entry)
{
	struct page * next = head->next;

	entry->prev = memory_head(head);
	entry->next = next;
	next->prev = entry;
	head->next = entry;
}

static inline void remove_mem_queue(struct page * entry)
{
	struct page * next = entry->next;
	struct page * prev = entry->prev;
	next->prev = prev;
	prev->next = next;
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
 *
 * Hint: -mask = 1+~mask
 */
spinlock_t page_alloc_lock = SPIN_LOCK_UNLOCKED;

/*
 * This routine is used by the kernel swap daemon to determine
 * whether we have "enough" free pages. It is fairly arbitrary,
 * having a low-water and high-water mark.
 *
 * This returns:
 *  0 - urgent need for memory
 *  1 - need some memory, but do it slowly in the background
 *  2 - no need to even think about it.
 */
int free_memory_available(void)
{
	static int available = 1;

	if (nr_free_pages < freepages.low) {
		available = 0;
		return 0;
	}

	if (nr_free_pages > freepages.high) {
		available = 1;
		return 2;
	}

	return available;
}

static inline void free_pages_ok(unsigned long map_nr, unsigned long order)
{
	struct free_area_struct *area = free_area + order;
	unsigned long index = map_nr >> (1 + order);
	unsigned long mask = (~0UL) << order;
	unsigned long flags;

	spin_lock_irqsave(&page_alloc_lock, flags);

#define list(x) (mem_map+(x))

	map_nr &= mask;
	nr_free_pages -= mask;
	while (mask + (1 << (NR_MEM_LISTS-1))) {
		if (!test_and_change_bit(index, area->map))
			break;
		remove_mem_queue(list(map_nr ^ -mask));
		mask <<= 1;
		area++;
		index >>= 1;
		map_nr &= mask;
	}
	add_mem_queue(area, list(map_nr));

#undef list

	spin_unlock_irqrestore(&page_alloc_lock, flags);
}

void __free_page(struct page *page)
{
	if (!PageReserved(page) && atomic_dec_and_test(&page->count)) {
		if (PageSwapCache(page))
			panic ("Freeing swap cache page");
		free_pages_ok(page->map_nr, 0);
		return;
	}
#if 0
	if (PageSwapCache(page) && atomic_read(&page->count) == 1)
		printk(KERN_WARNING "VM: Releasing swap cache page at %p",
			__builtin_return_address(0));
#endif
}

void free_pages(unsigned long addr, unsigned long order)
{
	unsigned long map_nr = MAP_NR(addr);

	if (map_nr < max_mapnr) {
		mem_map_t * map = mem_map + map_nr;
		if (PageReserved(map))
			return;
		if (atomic_dec_and_test(&map->count)) {
			if (PageSwapCache(map))
				panic ("Freeing swap cache pages");
			free_pages_ok(map_nr, order);
			return;
		}
#if 0
		if (PageSwapCache(map) && atomic_read(&map->count) == 1)
			printk(KERN_WARNING 
				"VM: Releasing swap cache pages at %p",
				__builtin_return_address(0));
#endif
	}
}

/*
 * Some ugly macros to speed up __get_free_pages()..
 */
#define MARK_USED(index, order, area) \
	change_bit((index) >> (1+(order)), (area)->map)
#define CAN_DMA(x) (PageDMA(x))
#define ADDRESS(x) (PAGE_OFFSET + ((x) << PAGE_SHIFT))
#define RMQUEUE(order, dma) \
do { struct free_area_struct * area = free_area+order; \
     unsigned long new_order = order; \
	do { struct page *prev = memory_head(area), *ret = prev->next; \
		while (memory_head(area) != ret) { \
			if (!dma || CAN_DMA(ret)) { \
				unsigned long map_nr = ret->map_nr; \
				(prev->next = ret->next)->prev = prev; \
				MARK_USED(map_nr, new_order, area); \
				nr_free_pages -= 1 << order; \
				EXPAND(ret, map_nr, order, new_order, area); \
				spin_unlock_irqrestore(&page_alloc_lock, flags); \
				return ADDRESS(map_nr); \
			} \
			prev = ret; \
			ret = ret->next; \
		} \
		new_order++; area++; \
	} while (new_order < NR_MEM_LISTS); \
} while (0)

#define EXPAND(map,index,low,high,area) \
do { unsigned long size = 1 << high; \
	while (high > low) { \
		area--; high--; size >>= 1; \
		add_mem_queue(area, map); \
		MARK_USED(index, high, area); \
		index += size; \
		map += size; \
	} \
	atomic_set(&map->count, 1); \
} while (0)

unsigned long __get_free_pages(int gfp_mask, unsigned long order)
{
	unsigned long flags;

	if (order >= NR_MEM_LISTS)
		goto nopage;

	if (gfp_mask & __GFP_WAIT) {
		if (in_interrupt()) {
			static int count = 0;
			if (++count < 5) {
				printk("gfp called nonatomically from interrupt %p\n",
					__builtin_return_address(0));
			}
			goto nopage;
		}

		if (freepages.min > nr_free_pages) {
			int freed;
			freed = try_to_free_pages(gfp_mask, SWAP_CLUSTER_MAX);
			/*
			 * Low priority (user) allocations must not
			 * succeed if we didn't have enough memory
			 * and we couldn't get more..
			 */
			if (!freed && !(gfp_mask & (__GFP_MED | __GFP_HIGH)))
				goto nopage;
		}
	}
	spin_lock_irqsave(&page_alloc_lock, flags);
	RMQUEUE(order, (gfp_mask & GFP_DMA));
	spin_unlock_irqrestore(&page_alloc_lock, flags);

	/*
	 * If we can schedule, do so, and make sure to yield.
	 * We may be a real-time process, and if kswapd is
	 * waiting for us we need to allow it to run a bit.
	 */
	if (gfp_mask & __GFP_WAIT) {
		current->policy |= SCHED_YIELD;
		schedule();
	}

nopage:
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
	printk("Free: %d (%d %d %d)\n",
		nr_free_pages,
		freepages.min,
		freepages.low,
		freepages.high);
	spin_lock_irqsave(&page_alloc_lock, flags);
 	for (order=0 ; order < NR_MEM_LISTS; order++) {
		struct page * tmp;
		unsigned long nr = 0;
		for (tmp = free_area[order].next ; tmp != memory_head(free_area+order) ; tmp = tmp->next) {
			nr ++;
		}
		total += nr * ((PAGE_SIZE>>10) << order);
		printk("%lu*%lukB ", nr, (unsigned long)((PAGE_SIZE>>10) << order));
	}
	spin_unlock_irqrestore(&page_alloc_lock, flags);
	printk("= %lukB)\n", total);
#ifdef SWAP_CACHE_INFO
	show_swap_cache_info();
#endif	
}

#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

/*
 * set up the free-area data structures:
 *   - mark all pages reserved
 *   - mark all memory queues empty
 *   - clear the memory bitmaps
 */
unsigned long __init free_area_init(unsigned long start_mem, unsigned long end_mem)
{
	mem_map_t * p;
	unsigned long mask = PAGE_MASK;
	unsigned long i;

	/*
	 * Select nr of pages we try to keep free for important stuff
	 * with a minimum of 10 pages and a maximum of 256 pages, so
	 * that we don't waste too much memory on large systems.
	 * This is fairly arbitrary, but based on some behaviour
	 * analysis.
	 */
	i = (end_mem - PAGE_OFFSET) >> (PAGE_SHIFT+7);
	if (i < 10)
		i = 10;
	if (i > 256)
		i = 256;
	freepages.min = i;
	freepages.low = i * 2;
	freepages.high = i * 3;
	mem_map = (mem_map_t *) LONG_ALIGN(start_mem);
	p = mem_map + MAP_NR(end_mem);
	start_mem = LONG_ALIGN((unsigned long) p);
	memset(mem_map, 0, start_mem - (unsigned long) mem_map);
	do {
		--p;
		atomic_set(&p->count, 0);
		p->flags = (1 << PG_DMA) | (1 << PG_reserved);
		p->map_nr = p - mem_map;
	} while (p > mem_map);

	for (i = 0 ; i < NR_MEM_LISTS ; i++) {
		unsigned long bitmap_size;
		init_mem_queue(free_area+i);
		mask += mask;
		end_mem = (end_mem + ~mask) & mask;
		bitmap_size = (end_mem - PAGE_OFFSET) >> (PAGE_SHIFT + i);
		bitmap_size = (bitmap_size + 7) >> 3;
		bitmap_size = LONG_ALIGN(bitmap_size);
		free_area[i].map = (unsigned int *) start_mem;
		memset((void *) start_mem, 0, bitmap_size);
		start_mem += bitmap_size;
	}
	return start_mem;
}

/*
 * The tests may look silly, but it essentially makes sure that
 * no other process did a swap-in on us just as we were waiting.
 *
 * Also, don't bother to add to the swap cache if this page-in
 * was due to a write access.
 */
void swap_in(struct task_struct * tsk, struct vm_area_struct * vma,
	pte_t * page_table, unsigned long entry, int write_access)
{
	unsigned long page;
	struct page *page_map;
	
	page_map = read_swap_cache(entry);

	if (pte_val(*page_table) != entry) {
		if (page_map)
			free_page_and_swap_cache(page_address(page_map));
		return;
	}
	if (!page_map) {
		set_pte(page_table, BAD_PAGE);
		swap_free(entry);
		oom(tsk);
		return;
	}

	page = page_address(page_map);
	vma->vm_mm->rss++;
	tsk->min_flt++;
	swap_free(entry);

	if (!write_access || is_page_shared(page_map)) {
		set_pte(page_table, mk_pte(page, vma->vm_page_prot));
		return;
	}

	/* The page is unshared, and we want write access.  In this
	   case, it is safe to tear down the swap cache and give the
	   page over entirely to this process. */

	if (PageSwapCache(page_map))
		delete_from_swap_cache(page_map);
	set_pte(page_table, pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot))));
  	return;
}

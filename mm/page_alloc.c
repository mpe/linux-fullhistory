/*
 *  linux/mm/page_alloc.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/bootmem.h>

#include <asm/dma.h>
#include <asm/uaccess.h> /* for copy_to/from_user */
#include <asm/pgtable.h>

int nr_swap_pages = 0;
int nr_free_pages = 0;
int nr_lru_pages;
LIST_HEAD(lru_cache);

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
#define NR_MEM_LISTS 10
#endif

struct free_area_struct {
	struct list_head free_list;
	unsigned int * map;
	unsigned long count;
};

#define MEM_TYPE_DMA		0
#define MEM_TYPE_NORMAL		1
#define MEM_TYPE_HIGH		2

static const char *mem_type_strs[] = {"DMA", "Normal", "High"};

#ifdef CONFIG_HIGHMEM
#define NR_MEM_TYPES		3
#else
#define NR_MEM_TYPES		2
#endif

static struct free_area_struct free_area[NR_MEM_TYPES][NR_MEM_LISTS];

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

#define memlist_init(x) INIT_LIST_HEAD(x)
#define memlist_add_head list_add
#define memlist_add_tail list_add_tail
#define memlist_del list_del
#define memlist_entry list_entry
#define memlist_next(x) ((x)->next)
#define memlist_prev(x) ((x)->prev)

static inline void free_pages_ok(struct page *page, unsigned long map_nr, unsigned long order)
{
	struct free_area_struct *area;
	unsigned long index = map_nr >> (1 + order);
	unsigned long mask = (~0UL) << order;
	unsigned long flags;
	struct page *buddy;

	spin_lock_irqsave(&page_alloc_lock, flags);

#define list(x) (mem_map+(x))

#ifdef CONFIG_HIGHMEM
	if (map_nr >= highmem_mapnr) {
		area = free_area[MEM_TYPE_HIGH];
		nr_free_highpages -= mask;
	} else
#endif
	if (PageDMA(page))
		area = free_area[MEM_TYPE_DMA];
	else
		area = free_area[MEM_TYPE_NORMAL];

	area += order;

	map_nr &= mask;
	nr_free_pages -= mask;

	while (mask + (1 << (NR_MEM_LISTS-1))) {
		if (!test_and_change_bit(index, area->map))
			/*
			 * the buddy page is still allocated.
			 */
			break;
		/*
		 * Move the buddy up one level.
		 */
		buddy = list(map_nr ^ -mask);
		page = list(map_nr);

		area->count--;
		memlist_del(&buddy->list);
		mask <<= 1;
		area++;
		index >>= 1;
		map_nr &= mask;
	}
	area->count++;
	memlist_add_head(&(list(map_nr))->list, &area->free_list);
#undef list

	spin_unlock_irqrestore(&page_alloc_lock, flags);
}

/*
 * Some ugly macros to speed up __get_free_pages()..
 */
#define MARK_USED(index, order, area) \
	change_bit((index) >> (1+(order)), (area)->map)
#define CAN_DMA(x) (PageDMA(x))
#define ADDRESS(x) (PAGE_OFFSET + ((x) << PAGE_SHIFT))

int __free_page(struct page *page)
{
	if (!PageReserved(page) && put_page_testzero(page)) {
		if (PageSwapCache(page))
			PAGE_BUG(page);
		if (PageLocked(page))
			PAGE_BUG(page);

		free_pages_ok(page, page-mem_map, 0);
		return 1;
	}
	return 0;
}

int free_pages(unsigned long addr, unsigned long order)
{
	unsigned long map_nr = MAP_NR(addr);

	if (map_nr < max_mapnr) {
		mem_map_t * map = mem_map + map_nr;
		if (!PageReserved(map) && put_page_testzero(map)) {
			if (PageSwapCache(map))
				PAGE_BUG(map);
			if (PageLocked(map))
				PAGE_BUG(map);
			free_pages_ok(map, map_nr, order);
			return 1;
		}
	}
	return 0;
}

static inline unsigned long EXPAND (struct page *map, unsigned long index,
		 int low, int high, struct free_area_struct * area)
{
	unsigned long size = 1 << high;

	while (high > low) {
		area--;
		high--;
		size >>= 1;
		area->count++;
		memlist_add_head(&(map)->list, &(area)->free_list);
		MARK_USED(index, high, area);
		index += size;
		map += size;
	}
	set_page_count(map, 1);
	return index;
}

static inline struct page * rmqueue (int order, unsigned type)
{
	struct free_area_struct * area = free_area[type]+order;
	unsigned long curr_order = order, map_nr;
	struct page *page;
	struct list_head *head, *curr;

	do {
		head = &area->free_list;
		curr = memlist_next(head);

		if (curr != head) {
			page = memlist_entry(curr, struct page, list);
			memlist_del(curr);
			area->count--;
			map_nr = page - mem_map;	
			MARK_USED(map_nr, curr_order, area);
			nr_free_pages -= 1 << order;
			map_nr = EXPAND(page, map_nr, order, curr_order, area);
			page = mem_map + map_nr;
			return page;	
		}
		curr_order++;
		area++;
	} while (curr_order < NR_MEM_LISTS);

	return NULL;
}

static inline int balance_lowmemory (int gfp_mask)
{
	int freed;
	static int low_on_memory = 0;

#ifndef CONFIG_HIGHMEM
	if (nr_free_pages > freepages.min) {
		if (!low_on_memory)
			return 1;
		if (nr_free_pages >= freepages.high) {
			low_on_memory = 0;
			return 1;
		}
	}

	low_on_memory = 1;
#else
	static int low_on_highmemory = 0;

	if (gfp_mask & __GFP_HIGHMEM)
	{
		if (nr_free_pages > freepages.min) {
			if (!low_on_highmemory) {
				return 1;
			}
			if (nr_free_pages >= freepages.high) {
				low_on_highmemory = 0;
				return 1;
			}
		}
		low_on_highmemory = 1;
	} else {
		if (nr_free_pages+nr_free_highpages > freepages.min) {
			if (!low_on_memory) {
				return 1;
			}
			if (nr_free_pages+nr_free_highpages >= freepages.high) {
				low_on_memory = 0;
				return 1;
			}
		}
		low_on_memory = 1;
	}
#endif
	current->flags |= PF_MEMALLOC;
	freed = try_to_free_pages(gfp_mask);
	current->flags &= ~PF_MEMALLOC;

	if (!freed && !(gfp_mask & (__GFP_MED | __GFP_HIGH)))
		return 0;
	return 1;
}

struct page * __get_pages(int gfp_mask, unsigned long order)
{
	unsigned long flags;
	struct page *page;
	unsigned type;

	if (order >= NR_MEM_LISTS)
		goto nopage;

	/*
	 * If anyone calls gfp from interrupts nonatomically then it
	 * will sooner or later tripped up by a schedule().
	 */

	/*
	 * If this is a recursive call, we'd better
	 * do our best to just allocate things without
	 * further thought.
	 */
	if (!(current->flags & PF_MEMALLOC))
		goto lowmemory;

ok_to_allocate:
#ifdef CONFIG_HIGHMEM
	if (gfp_mask & __GFP_HIGHMEM)
		type = MEM_TYPE_HIGH;
	else
#endif
	if (gfp_mask & __GFP_DMA)
		type = MEM_TYPE_DMA;
	else
		type = MEM_TYPE_NORMAL;

	spin_lock_irqsave(&page_alloc_lock, flags);
	do {
		page = rmqueue(order, type);
		if (page) {
			spin_unlock_irqrestore(&page_alloc_lock, flags);
			return page;
		}
	} while (type-- > 0) ;
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
	return NULL;

lowmemory:
	if (balance_lowmemory(gfp_mask))
		goto ok_to_allocate;
	goto nopage;
}

unsigned long __get_free_pages(int gfp_mask, unsigned long order)
{
	struct page *page;
	page = __get_pages(gfp_mask, order);
	if (!page)
		return 0;
	return page_address(page);
}

struct page * get_free_highpage(int gfp_mask)
{
	return __get_pages(gfp_mask, 0);
}

/*
 * Show free area list (used inside shift_scroll-lock stuff)
 * We also calculate the percentage fragmentation. We do this by counting the
 * memory on each free list with the exception of the first item on the list.
 */
void show_free_areas(void)
{
 	unsigned long order, flags;
	unsigned type;

	spin_lock_irqsave(&page_alloc_lock, flags);
	printk("Free pages:      %6dkB (%6ldkB HighMem)\n",
		nr_free_pages<<(PAGE_SHIFT-10),
		nr_free_highpages<<(PAGE_SHIFT-10));
	printk("( Free: %d, lru_cache: %d (%d %d %d) )\n",
		nr_free_pages,
		nr_lru_pages,
		freepages.min,
		freepages.low,
		freepages.high);

	for (type = 0; type < NR_MEM_TYPES; type++) {
 		unsigned long total = 0;
		printk("  %s: ", mem_type_strs[type]);
	 	for (order = 0; order < NR_MEM_LISTS; order++) {
			unsigned long nr = free_area[type][order].count;

			total += nr * ((PAGE_SIZE>>10) << order);
			printk("%lu*%lukB ", nr, (unsigned long)((PAGE_SIZE>>10) << order));
		}
		printk("= %lukB)\n", total);
	}
	spin_unlock_irqrestore(&page_alloc_lock, flags);

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
volatile int data;
void __init free_area_init(unsigned long end_mem_pages)
{
	mem_map_t * p;
	unsigned long i, j;
	unsigned long map_size;

	/*
	 * Select nr of pages we try to keep free for important stuff
	 * with a minimum of 10 pages and a maximum of 256 pages, so
	 * that we don't waste too much memory on large systems.
	 * This is fairly arbitrary, but based on some behaviour
	 * analysis.
	 */
	i = end_mem_pages >> 7;
	if (i < 10)
		i = 10;
	if (i > 256)
		i = 256;
	freepages.min = i;
	freepages.low = i * 2;
	freepages.high = i * 3;

	/*
	 * Most architectures just pick 'start_mem'. Some architectures
	 * (with lots of mem and discontinous memory maps) have to search
	 * for a good area.
	 */
	map_size = end_mem_pages*sizeof(struct page);
	mem_map = (struct page *) alloc_bootmem(map_size);
	memset(mem_map, 0, map_size);

	/*
	 * Initially all pages are reserved - free ones are freed
	 * up by free_all_bootmem() once the early boot process is
	 * done.
	 */
	for (p = mem_map; p < mem_map + end_mem_pages; p++) {
		set_page_count(p, 0);
		p->flags = (1 << PG_DMA);
		SetPageReserved(p);
		init_waitqueue_head(&p->wait);
		memlist_init(&p->list);
	}
	
	for (j = 0 ; j < NR_MEM_TYPES ; j++) {
		unsigned long mask = -1;
		for (i = 0 ; i < NR_MEM_LISTS ; i++) {
			unsigned long bitmap_size;
			unsigned int * map;
			memlist_init(&free_area[j][i].free_list);
			mask += mask;
			end_mem_pages = (end_mem_pages + ~mask) & mask;
			bitmap_size = end_mem_pages >> i;
			bitmap_size = (bitmap_size + 7) >> 3;
			bitmap_size = LONG_ALIGN(bitmap_size);
			map = (unsigned int *) alloc_bootmem(bitmap_size);
			free_area[j][i].map = map;
			memset((void *) map, 0, bitmap_size);
		}
	}
}

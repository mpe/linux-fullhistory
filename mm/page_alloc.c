/*
 *  linux/mm/page_alloc.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *  Reshaped it to be a zoned allocator, Ingo Molnar, Red Hat, 1999
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
#define MAX_ORDER 12
#else
#define MAX_ORDER 10
#endif

typedef struct free_area_struct {
	struct list_head free_list;
	unsigned int * map;
} free_area_t;

#define ZONE_DMA		0
#define ZONE_NORMAL		1

#ifdef CONFIG_HIGHMEM
# define ZONE_HIGHMEM		2
# define NR_ZONES		3
#else
# define NR_ZONES		2
#endif

typedef struct zone_struct {
	spinlock_t lock;
	unsigned long offset;
	unsigned long size;
	free_area_t free_area[MAX_ORDER];

	unsigned long free_pages;
	unsigned long pages_low, pages_high;
	int low_on_memory;
	char * name;
} zone_t;

static zone_t zones[NR_ZONES] =
	{
		{ name: "DMA" },
		{ name: "Normal" },
#ifdef CONFIG_HIGHMEM
		{ name: "HighMem" }
#endif
	};

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

/*
 * Temporary debugging check.
 */
#define BAD_RANGE(zone,x) ((((x)-mem_map) < zone->offset) || (((x)-mem_map) >= zone->offset+zone->size))


static inline void free_pages_ok(struct page *page, unsigned long map_nr, unsigned long order)
{
	struct free_area_struct *area;
	unsigned long index, page_idx, mask, offset;
	unsigned long flags;
	struct page *buddy;
	zone_t *zone;
	int i;

	/*
	 * Which zone is this page belonging to.
	 *
	 * (NR_ZONES is low, and we do not want (yet) to introduce
	 * put page->zone, it increases the size of mem_map[]
	 * unnecesserily. This small loop is basically equivalent
	 * to the previous #ifdef jungle, speed-wise.)
	 */
	i = NR_ZONES-1;
	zone = zones + i;
	for ( ; i >= 0; i--, zone--)
		if (map_nr >= zone->offset)
			break;

	mask = (~0UL) << order;
	offset = zone->offset;
	area = zone->free_area;
	area += order;
	page_idx = map_nr - zone->offset;
	page_idx &= mask;
	index = page_idx >> (1 + order);
	mask = (~0UL) << order;

	spin_lock_irqsave(&zone->lock, flags);

	zone->free_pages -= mask;

	while (mask + (1 << (MAX_ORDER-1))) {
		if (!test_and_change_bit(index, area->map))
			/*
			 * the buddy page is still allocated.
			 */
			break;
		/*
		 * Move the buddy up one level.
		 */
		buddy = mem_map + offset + (page_idx ^ -mask);
		page = mem_map + offset + page_idx;
		if (BAD_RANGE(zone,buddy))
			BUG();
		if (BAD_RANGE(zone,page))
			BUG();

		memlist_del(&buddy->list);
		mask <<= 1;
		area++;
		index >>= 1;
		page_idx &= mask;
	}
	memlist_add_head(&mem_map[offset + page_idx].list, &area->free_list);

	spin_unlock_irqrestore(&zone->lock, flags);
}

/*
 * Some ugly macros to speed up __get_free_pages()..
 */
#define MARK_USED(index, order, area) \
	change_bit((index) >> (1+(order)), (area)->map)
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

static inline unsigned long EXPAND (zone_t *zone, struct page *map, unsigned long index,
		 int low, int high, struct free_area_struct * area)
{
	unsigned long size = 1 << high;

	while (high > low) {
		if (BAD_RANGE(zone,map))
			BUG();
		area--;
		high--;
		size >>= 1;
		memlist_add_head(&(map)->list, &(area)->free_list);
		MARK_USED(index, high, area);
		index += size;
		map += size;
	}
	set_page_count(map, 1);
	return index;
}

static inline struct page * rmqueue (zone_t *zone, int order)
{
	struct free_area_struct * area = zone->free_area + order;
	unsigned long curr_order = order, map_nr;
	struct list_head *head, *curr;
	unsigned long flags;
	struct page *page;

	spin_lock_irqsave(&zone->lock, flags);
	do {
		head = &area->free_list;
		curr = memlist_next(head);

		if (curr != head) {
			unsigned int index;

			page = memlist_entry(curr, struct page, list);
			memlist_del(curr);
			map_nr = page - mem_map;
			index = map_nr - zone->offset;
			MARK_USED(index, curr_order, area);
			zone->free_pages -= 1 << order;
			map_nr = zone->offset + EXPAND(zone, page, index, order, curr_order, area);
			spin_unlock_irqrestore(&zone->lock, flags);

			page = mem_map + map_nr;
			if (BAD_RANGE(zone,page))
				BUG();
			return page;	
		}
		curr_order++;
		area++;
	} while (curr_order < MAX_ORDER);
	spin_unlock_irqrestore(&zone->lock, flags);

	return NULL;
}

static inline int balance_memory (zone_t *zone, int gfp_mask)
{
	int freed;

	if (zone->free_pages > zone->pages_low) {
		if (!zone->low_on_memory)
			return 1;
		/*
		 * Simple hysteresis: exit 'low memory mode' if
		 * the upper limit has been reached:
		 */
		if (zone->free_pages >= zone->pages_high) {
			zone->low_on_memory = 0;
			return 1;
		}
	}
	zone->low_on_memory = 1;

	current->flags |= PF_MEMALLOC;
	freed = try_to_free_pages(gfp_mask);
	current->flags &= ~PF_MEMALLOC;

	if (!freed && !(gfp_mask & (__GFP_MED | __GFP_HIGH)))
		return 0;
	return 1;
}

struct page * __get_pages(zone_t *zone, unsigned int gfp_mask,
			unsigned long order)
{
	struct page *page;

	if (order >= MAX_ORDER)
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
		if (!balance_memory(zone, gfp_mask))
			goto nopage;
	/*
	 * We are falling back to lower-level zones if allocation
	 * in a higher zone fails. This assumes a hierarchical
	 * dependency between zones, which is true currently. If
	 * you need something else then move this loop outside
	 * this function, into the zone-specific allocator.
	 */
	do {
		page = rmqueue(zone, order);
		if (page)
			return page;
	} while (zone-- != zones) ;

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
}

unsigned long __get_free_pages(int gfp_mask, unsigned long order)
{
	struct page *page;
	zone_t *zone;

	if (gfp_mask & __GFP_DMA)
		zone = zones + ZONE_DMA;
	else
		zone = zones + ZONE_NORMAL;
	page = __get_pages(zone, gfp_mask, order);
	if (gfp_mask & __GFP_DMA)
		if (!PageDMA(page))
			BUG();
	if (!page)
		return 0;
	return page_address(page);
}

struct page * get_free_highpage(int gfp_mask)
{
	return __get_pages(zones + NR_ZONES-1, gfp_mask, 0);
}

/*
 * Total amount of free (allocatable) RAM:
 */
unsigned int nr_free_pages (void)
{
	unsigned int sum;
	zone_t *zone;

	sum = 0;
	for (zone = zones; zone < zones+NR_ZONES; zone++)
		sum += zone->free_pages;
	return sum;
}

/*
 * Amount of free RAM allocatable as buffer memory:
 */
unsigned int nr_free_buffer_pages (void)
{
	unsigned int sum;
	zone_t *zone;

	sum = nr_lru_pages;
	for (zone = zones; zone <= zones+ZONE_NORMAL; zone++)
		sum += zone->free_pages;
	return sum;
}

#if CONFIG_HIGHMEM
unsigned int nr_free_highpages (void)
{
	return zones[ZONE_HIGHMEM].free_pages;
}
#endif

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
	printk("Free pages:      %6dkB (%6dkB HighMem)\n",
		nr_free_pages()<<(PAGE_SHIFT-10),
		nr_free_highpages()<<(PAGE_SHIFT-10));
	printk("( Free: %d, lru_cache: %d (%d %d %d) )\n",
		nr_free_pages(),
		nr_lru_pages,
		freepages.min,
		freepages.low,
		freepages.high);

	for (type = 0; type < NR_ZONES; type++) {
		zone_t *zone = zones + type;
 		unsigned long total = 0;

		printk("  %s: ", zone->name);
	 	for (order = 0; order < MAX_ORDER; order++) {
			unsigned long i, nr;

			nr = 0;
			for (i = 0; i < zone->size; i += 1<<order) {
				struct page * page;
				page = mem_map + zone->offset + i;
				if (!page_count(page))
					nr++;
			}
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
 * Set up the zone data structures:
 *   - mark all pages reserved
 *   - mark all memory queues empty
 *   - clear the memory bitmaps
 */
void __init free_area_init(unsigned int *zones_size)
{
	mem_map_t * p;
	unsigned long i, j;
	unsigned long map_size;
	unsigned int totalpages, offset;

	totalpages = 0;
	for (i = 0; i < NR_ZONES; i++)
		totalpages += zones_size[i];
	printk("totalpages: %08x\n", totalpages);

	i = totalpages >> 7;
	/*
	 * Select nr of pages we try to keep free for important stuff
	 * with a minimum of 10 pages and a maximum of 256 pages, so
	 * that we don't waste too much memory on large systems.
	 * This is fairly arbitrary, but based on some behaviour
	 * analysis.
	 */
	i = totalpages >> 7;
	if (i < 10)
		i = 10;
	if (i > 256)
		i = 256;
	freepages.min = i;
	freepages.low = i * 2;
	freepages.high = i * 3;

	/*
	 * Some architectures (with lots of mem and discontinous memory
	 * maps) have to search for a good mem_map area:
	 */
	map_size = totalpages*sizeof(struct page);
	mem_map = (struct page *) alloc_bootmem(map_size);
	memset(mem_map, 0, map_size);

	/*
	 * Initially all pages are reserved - free ones are freed
	 * up by free_all_bootmem() once the early boot process is
	 * done.
	 */
	for (p = mem_map; p < mem_map + totalpages; p++) {
		set_page_count(p, 0);
		p->flags = (1 << PG_DMA);
		SetPageReserved(p);
		init_waitqueue_head(&p->wait);
		memlist_init(&p->list);
	}

	offset = 0;	
	for (j = 0; j < NR_ZONES; j++) {
		zone_t *zone = zones + j;
		unsigned long mask = -1;
		unsigned long size;

		size = zones_size[j];
		zone->size = size;
		zone->offset = offset;
		zone->pages_low = freepages.low;
		zone->pages_high = freepages.high;
		zone->low_on_memory = 0;

		offset += size;
		for (i = 0; i < MAX_ORDER; i++) {
			unsigned long bitmap_size;
			unsigned int * map;
			memlist_init(&zone->free_area[i].free_list);
			mask += mask;
			size = (size + ~mask) & mask;
			bitmap_size = size >> i;
			bitmap_size = (bitmap_size + 7) >> 3;
			bitmap_size = LONG_ALIGN(bitmap_size);
			map = (unsigned int *) alloc_bootmem(bitmap_size);
			zone->free_area[i].map = map;
			memset((void *) map, 0, bitmap_size);
		}
	}
}

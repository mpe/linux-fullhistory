/*
 *  linux/mm/page_alloc.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *  Reshaped it to be a zoned allocator, Ingo Molnar, Red Hat, 1999
 *  Discontiguous memory support, Kanoj Sarcar, SGI, Nov 1999
 *  Zone balancing, Kanoj Sarcar, SGI, Jan 2000
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>

/* Use NUMNODES instead of numnodes for better code inside kernel APIs */
#ifndef CONFIG_DISCONTIGMEM
#define NUMNODES 1
#else
#define NUMNODES numnodes
#endif

int nr_swap_pages = 0;
int nr_lru_pages;
pg_data_t *pgdat_list = (pg_data_t *)0;

static char *zone_names[MAX_NR_ZONES] = { "DMA", "Normal", "HighMem" };
static int zone_balance_ratio[MAX_NR_ZONES] = { 128, 128, 128, };
static int zone_balance_min[MAX_NR_ZONES] = { 10 , 10, 10, };
static int zone_balance_max[MAX_NR_ZONES] = { 255 , 255, 255, };

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
#define BAD_RANGE(zone,x) (((zone) != (x)->zone) || (((x)-mem_map) < (zone)->offset) || (((x)-mem_map) >= (zone)->offset+(zone)->size))

static inline unsigned long classfree(zone_t *zone)
{
	unsigned long free = 0;
	zone_t *z = zone->zone_pgdat->node_zones;

	while (z != zone) {
		free += z->free_pages;
		z++;
	}
	free += zone->free_pages;
	return(free);
}

/*
 * Buddy system. Hairy. You really aren't expected to understand this
 *
 * Hint: -mask = 1+~mask
 */

void __free_pages_ok (struct page *page, unsigned long order)
{
	unsigned long index, page_idx, mask, flags;
	free_area_t *area;
	struct page *base;
	zone_t *zone;

	/*
	 * Subtle. We do not want to test this in the inlined part of
	 * __free_page() - it's a rare condition and just increases
	 * cache footprint unnecesserily. So we do an 'incorrect'
	 * decrement on page->count for reserved pages, but this part
	 * makes it safe.
	 */
	if (PageReserved(page))
		return;

	if (page->buffers)
		BUG();
	if (page-mem_map >= max_mapnr)
		BUG();
	if (PageSwapCache(page))
		BUG();
	if (PageLocked(page))
		BUG();

	zone = page->zone;

	mask = (~0UL) << order;
	base = mem_map + zone->offset;
	page_idx = page - base;
	if (page_idx & ~mask)
		BUG();
	index = page_idx >> (1 + order);

	area = zone->free_area + order;

	spin_lock_irqsave(&zone->lock, flags);

	zone->free_pages -= mask;

	while (mask + (1 << (MAX_ORDER-1))) {
		struct page *buddy1, *buddy2;

		if (area >= zone->free_area + MAX_ORDER)
			BUG();
		if (!test_and_change_bit(index, area->map))
			/*
			 * the buddy page is still allocated.
			 */
			break;
		/*
		 * Move the buddy up one level.
		 */
		buddy1 = base + (page_idx ^ -mask);
		buddy2 = base + page_idx;
		if (BAD_RANGE(zone,buddy1))
			BUG();
		if (BAD_RANGE(zone,buddy2))
			BUG();

		memlist_del(&buddy1->list);
		mask <<= 1;
		area++;
		index >>= 1;
		page_idx &= mask;
	}
	memlist_add_head(&(base + page_idx)->list, &area->free_list);

	spin_unlock_irqrestore(&zone->lock, flags);

	if (classfree(zone) > zone->pages_high)
		zone->zone_wake_kswapd = 0;
}

#define MARK_USED(index, order, area) \
	change_bit((index) >> (1+(order)), (area)->map)

static inline struct page * expand (zone_t *zone, struct page *page,
	 unsigned long index, int low, int high, free_area_t * area)
{
	unsigned long size = 1 << high;

	while (high > low) {
		if (BAD_RANGE(zone,page))
			BUG();
		area--;
		high--;
		size >>= 1;
		memlist_add_head(&(page)->list, &(area)->free_list);
		MARK_USED(index, high, area);
		index += size;
		page += size;
	}
	if (BAD_RANGE(zone,page))
		BUG();
	return page;
}

static inline struct page * rmqueue (zone_t *zone, unsigned long order)
{
	free_area_t * area = zone->free_area + order;
	unsigned long curr_order = order;
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
			if (BAD_RANGE(zone,page))
				BUG();
			memlist_del(curr);
			index = (page - mem_map) - zone->offset;
			MARK_USED(index, curr_order, area);
			zone->free_pages -= 1 << order;

			page = expand(zone, page, index, order, curr_order, area);
			spin_unlock_irqrestore(&zone->lock, flags);

			set_page_count(page, 1);
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

static inline int zone_balance_memory (zone_t *zone, int gfp_mask)
{
	int freed;

	/*
	 * In the atomic allocation case we only 'kick' the
	 * state machine, but do not try to free pages
	 * ourselves.
	 */
	freed = try_to_free_pages(gfp_mask, zone);

	if (!freed && !(gfp_mask & __GFP_HIGH))
		return 0;
	return 1;
}

/*
 * This is the 'heart' of the zoned buddy allocator:
 */
struct page * __alloc_pages (zonelist_t *zonelist, unsigned long order)
{
	zone_t **zone, *z;
	struct page *page;
	int gfp_mask;

	/*
	 * (If anyone calls gfp from interrupts nonatomically then it
	 * will sooner or later tripped up by a schedule().)
	 *
	 * We are falling back to lower-level zones if allocation
	 * in a higher zone fails.
	 */
	zone = zonelist->zones;
	gfp_mask = zonelist->gfp_mask;
	for (;;) {
		z = *(zone++);
		if (!z)
			break;
		if (!z->size)
			BUG();
		/*
		 * If this is a recursive call, we'd better
		 * do our best to just allocate things without
		 * further thought.
		 */
		if (!(current->flags & PF_MEMALLOC))
		{
			unsigned long free = classfree(z);

			if (free <= z->pages_high)
			{
				extern wait_queue_head_t kswapd_wait;

				z->zone_wake_kswapd = 1;
				wake_up_interruptible(&kswapd_wait);

				if (free <= z->pages_min)
					z->low_on_memory = 1;

				if (z->low_on_memory)
					goto balance;
			}
		}
		/*
		 * This is an optimization for the 'higher order zone
		 * is empty' case - it can happen even in well-behaved
		 * systems, think the page-cache filling up all RAM.
		 * We skip over empty zones. (this is not exact because
		 * we do not take the spinlock and it's not exact for
		 * the higher order case, but will do it for most things.)
		 */
ready:
		if (z->free_pages) {
			page = rmqueue(z, order);
			if (page)
				return page;
		}
	}

nopage:
	return NULL;

/*
 * The main chunk of the balancing code is in this offline branch:
 */
balance:
	if (!zone_balance_memory(z, gfp_mask))
		goto nopage;
	goto ready;
}

/*
 * Total amount of free (allocatable) RAM:
 */
unsigned int nr_free_pages (void)
{
	unsigned int sum;
	zone_t *zone;
	int i;

	sum = 0;
	for (i = 0; i < NUMNODES; i++)
		for (zone = NODE_DATA(i)->node_zones; zone < NODE_DATA(i)->node_zones + MAX_NR_ZONES; zone++)
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
	int i;

	sum = nr_lru_pages;
	for (i = 0; i < NUMNODES; i++)
		for (zone = NODE_DATA(i)->node_zones; zone <= NODE_DATA(i)->node_zones+ZONE_NORMAL; zone++)
			sum += zone->free_pages;
	return sum;
}

#if CONFIG_HIGHMEM
unsigned int nr_free_highpages (void)
{
	int i;
	unsigned int pages = 0;

	for (i = 0; i < NUMNODES; i++)
		pages += NODE_DATA(i)->node_zones[ZONE_HIGHMEM].free_pages;
	return pages;
}
#endif

/*
 * Show free area list (used inside shift_scroll-lock stuff)
 * We also calculate the percentage fragmentation. We do this by counting the
 * memory on each free list with the exception of the first item on the list.
 */
void show_free_areas_core(int nid)
{
 	unsigned long order;
	unsigned type;

	printk("Free pages:      %6dkB (%6dkB HighMem)\n",
		nr_free_pages() << (PAGE_SHIFT-10),
		nr_free_highpages() << (PAGE_SHIFT-10));

	printk("( Free: %d, lru_cache: %d (%d %d %d) )\n",
		nr_free_pages(),
		nr_lru_pages,
		freepages.min,
		freepages.low,
		freepages.high);

	for (type = 0; type < MAX_NR_ZONES; type++) {
		struct list_head *head, *curr;
		zone_t *zone = NODE_DATA(nid)->node_zones + type;
 		unsigned long nr, total, flags;

		printk("  %s: ", zone->name);

		total = 0;
		if (zone->size) {
			spin_lock_irqsave(&zone->lock, flags);
		 	for (order = 0; order < MAX_ORDER; order++) {
				head = &(zone->free_area + order)->free_list;
				curr = head;
				nr = 0;
				for (;;) {
					curr = memlist_next(curr);
					if (curr == head)
						break;
					nr++;
				}
				total += nr * (1 << order);
				printk("%lu*%lukB ", nr,
						(PAGE_SIZE>>10) << order);
			}
			spin_unlock_irqrestore(&zone->lock, flags);
		}
		printk("= %lukB)\n", total * (PAGE_SIZE>>10));
	}

#ifdef SWAP_CACHE_INFO
	show_swap_cache_info();
#endif	
}

void show_free_areas(void)
{
	show_free_areas_core(0);
}

/*
 * Builds allocation fallback zone lists.
 */
static inline void build_zonelists(pg_data_t *pgdat)
{
	int i, j, k;

	for (i = 0; i < NR_GFPINDEX; i++) {
		zonelist_t *zonelist;
		zone_t *zone;

		zonelist = pgdat->node_zonelists + i;
		memset(zonelist, 0, sizeof(*zonelist));

		zonelist->gfp_mask = i;
		j = 0;
		k = ZONE_NORMAL;
		if (i & __GFP_HIGHMEM)
			k = ZONE_HIGHMEM;
		if (i & __GFP_DMA)
			k = ZONE_DMA;

		switch (k) {
			default:
				BUG();
			/*
			 * fallthrough:
			 */
			case ZONE_HIGHMEM:
				zone = pgdat->node_zones + ZONE_HIGHMEM;
				if (zone->size) {
#ifndef CONFIG_HIGHMEM
					BUG();
#endif
					zonelist->zones[j++] = zone;
				}
			case ZONE_NORMAL:
				zone = pgdat->node_zones + ZONE_NORMAL;
				if (zone->size)
					zonelist->zones[j++] = zone;
			case ZONE_DMA:
				zone = pgdat->node_zones + ZONE_DMA;
				if (zone->size)
					zonelist->zones[j++] = zone;
		}
		zonelist->zones[j++] = NULL;
	} 
}

#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

/*
 * Set up the zone data structures:
 *   - mark all pages reserved
 *   - mark all memory queues empty
 *   - clear the memory bitmaps
 */
void __init free_area_init_core(int nid, pg_data_t *pgdat, struct page **gmap,
	unsigned long *zones_size, unsigned long zone_start_paddr)
{
	struct page *p, *lmem_map;
	unsigned long i, j;
	unsigned long map_size;
	unsigned long totalpages, offset;
	unsigned int cumulative = 0;

	pgdat->node_next = pgdat_list;
	pgdat_list = pgdat;

	totalpages = 0;
	for (i = 0; i < MAX_NR_ZONES; i++) {
		unsigned long size = zones_size[i];
		totalpages += size;
	}
	printk("On node %d totalpages: %lu\n", nid, totalpages);

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
	freepages.min += i;
	freepages.low += i * 2;
	freepages.high += i * 3;

	/*
	 * Some architectures (with lots of mem and discontinous memory
	 * maps) have to search for a good mem_map area:
	 * For discontigmem, the conceptual mem map array starts from 
	 * PAGE_OFFSET, we need to align the actual array onto a mem map 
	 * boundary, so that MAP_NR works.
	 */
	map_size = (totalpages + 1)*sizeof(struct page);
	lmem_map = (struct page *) alloc_bootmem_node(nid, map_size);
	lmem_map = (struct page *)(PAGE_OFFSET + 
			MAP_ALIGN((unsigned long)lmem_map - PAGE_OFFSET));
	*gmap = pgdat->node_mem_map = lmem_map;
	pgdat->node_size = totalpages;
	pgdat->node_start_paddr = zone_start_paddr;
	pgdat->node_start_mapnr = (lmem_map - mem_map);

	/*
	 * Initially all pages are reserved - free ones are freed
	 * up by free_all_bootmem() once the early boot process is
	 * done.
	 */
	for (p = lmem_map; p < lmem_map + totalpages; p++) {
		set_page_count(p, 0);
		SetPageReserved(p);
		init_waitqueue_head(&p->wait);
		memlist_init(&p->list);
	}

	offset = lmem_map - mem_map;	
	for (j = 0; j < MAX_NR_ZONES; j++) {
		zone_t *zone = pgdat->node_zones + j;
		unsigned long mask;
		unsigned long size;

		size = zones_size[j];

		printk("zone(%lu): %lu pages.\n", j, size);
		zone->size = size;
		zone->name = zone_names[j];
		zone->lock = SPIN_LOCK_UNLOCKED;
		zone->zone_pgdat = pgdat;
		if (!size)
			continue;

		zone->offset = offset;
		cumulative += size;
		mask = (cumulative / zone_balance_ratio[j]);
		if (mask < zone_balance_min[j])
			mask = zone_balance_min[j];
		else if (mask > zone_balance_max[j])
			mask = zone_balance_max[j];
		zone->pages_min = mask;
		zone->pages_low = mask*2;
		zone->pages_high = mask*3;
		zone->low_on_memory = 0;
		zone->zone_wake_kswapd = 0;
		zone->zone_mem_map = mem_map + offset;
		zone->zone_start_mapnr = offset;
		zone->zone_start_paddr = zone_start_paddr;

		for (i = 0; i < size; i++) {
			struct page *page = mem_map + offset + i;
			page->zone = zone;
			if (j != ZONE_HIGHMEM) {
				page->virtual = (unsigned long)(__va(zone_start_paddr));
				zone_start_paddr += PAGE_SIZE;
			}
		}

		offset += size;
		mask = -1;
		for (i = 0; i < MAX_ORDER; i++) {
			unsigned long bitmap_size;

			memlist_init(&zone->free_area[i].free_list);
			memlist_init(&zone->lru_cache);
			mask += mask;
			size = (size + ~mask) & mask;
			bitmap_size = size >> i;
			bitmap_size = (bitmap_size + 7) >> 3;
			bitmap_size = LONG_ALIGN(bitmap_size);
			zone->free_area[i].map = 
			  (unsigned int *) alloc_bootmem_node(nid, bitmap_size);
		}
	}
	build_zonelists(pgdat);
}

void __init free_area_init(unsigned long *zones_size)
{
	free_area_init_core(0, NODE_DATA(0), &mem_map, zones_size, 0);
}

static int __init setup_mem_frac(char *str)
{
	int j = 0;

	while (get_option(&str, &zone_balance_ratio[j++]) == 2);
	printk("setup_mem_frac: ");
	for (j = 0; j < MAX_NR_ZONES; j++) printk("%d  ", zone_balance_ratio[j]);
	printk("\n");
	return 1;
}

__setup("memfrac=", setup_mem_frac);

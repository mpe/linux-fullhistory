#ifndef _LINUX_MMZONE_H
#define _LINUX_MMZONE_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/list.h>

/*
 * Free memory management - zoned buddy allocator.
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

typedef struct zone_struct {
	/*
	 * Commonly accessed fields:
	 */
	spinlock_t lock;
	unsigned long offset;
	unsigned long free_pages;
	int low_on_memory;
	unsigned long pages_low, pages_high;

	/*
	 * free areas of different sizes
	 */
	free_area_t free_area[MAX_ORDER];

	/*
	 * rarely used fields:
	 */
	char * name;
	unsigned long size;
} zone_t;

#define ZONE_DMA		0
#define ZONE_NORMAL		1
#define ZONE_HIGHMEM		2
#define MAX_NR_ZONES		3

/*
 * One allocation request operates on a zonelist. A zonelist
 * is a list of zones, the first one is the 'goal' of the
 * allocation, the other zones are fallback zones, in decreasing
 * priority.
 *
 * Right now a zonelist takes up less than a cacheline. We never
 * modify it apart from boot-up, and only a few indices are used,
 * so despite the zonelist table being relatively big, the cache
 * footprint of this construct is very small.
 */
typedef struct zonelist_struct {
	zone_t * zones [MAX_NR_ZONES+1]; // NULL delimited
	int gfp_mask;
} zonelist_t;

#define NR_GFPINDEX		0x100

struct bootmem_data;
typedef struct pglist_data {
	zone_t node_zones[MAX_NR_ZONES];
	zonelist_t node_zonelists[NR_GFPINDEX];
	struct page *node_mem_map;
	unsigned long *valid_addr_bitmap;
	struct bootmem_data *bdata;
} pg_data_t;

extern int numnodes;

#ifndef CONFIG_DISCONTIGMEM

extern pg_data_t contig_page_data;

#define NODE_DATA(nid)		(&contig_page_data)
#define NODE_MEM_MAP(nid)	mem_map

#else /* !CONFIG_DISCONTIGMEM */

#include <asm/mmzone.h>

#endif /* !CONFIG_DISCONTIGMEM */

#define MAP_ALIGN(x)	((((x) % sizeof(mem_map_t)) == 0) ? (x) : ((x) + \
		sizeof(mem_map_t) - ((x) % sizeof(mem_map_t))))

#ifdef CONFIG_DISCONTIGMEM

#define LOCAL_MAP_NR(kvaddr) \
	(((unsigned long)(kvaddr)-LOCAL_BASE_ADDR((kvaddr))) >> PAGE_SHIFT)
#define MAP_NR(kaddr)	(LOCAL_MAP_NR((kaddr)) + \
		(((unsigned long)ADDR_TO_MAPBASE((kaddr)) - PAGE_OFFSET) / \
		sizeof(mem_map_t)))
#define kern_addr_valid(addr)	((KVADDR_TO_NID((unsigned long)addr) >= \
	numnodes) ? 0 : (test_bit(LOCAL_MAP_NR((addr)), \
	NODE_DATA(KVADDR_TO_NID((unsigned long)addr))->valid_addr_bitmap)))

#endif /* CONFIG_DISCONTIGMEM */

#endif /* !__ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _LINUX_MMZONE_H */

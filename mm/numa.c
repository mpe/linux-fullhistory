/*
 * Written by Kanoj Sarcar, SGI, Aug 1999
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/spinlock.h>

int numnodes = 1;	/* Initialized for UMA platforms */

#ifndef CONFIG_DISCONTIGMEM

static bootmem_data_t contig_bootmem_data;
pg_data_t contig_page_data = { bdata: &contig_bootmem_data };

#endif /* !CONFIG_DISCONTIGMEM */

struct page * alloc_pages_node(int nid, int gfp_mask, unsigned long order)
{
	return __alloc_pages(NODE_DATA(nid)->node_zonelists + gfp_mask, order);
}

#ifdef CONFIG_DISCONTIGMEM

#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

static spinlock_t node_lock = SPIN_LOCK_UNLOCKED;

extern void show_free_areas_core(int);
extern void __init free_area_init_core(int nid, pg_data_t *pgdat, 
	struct page **gmap, unsigned int *zones_size, unsigned long paddr);

void show_free_areas_node(int nid)
{
	unsigned long flags;

	spin_lock_irqsave(&node_lock, flags);
	printk("Memory information for node %d:\n", nid);
	show_free_areas_core(nid);
	spin_unlock_irqrestore(&node_lock, flags);
}

/*
 * Nodes can be initialized parallely, in no particular order.
 */
void __init free_area_init_node(int nid, pg_data_t *pgdat, 
		unsigned int *zones_size, unsigned long zone_start_paddr)
{
	int i, size = 0;
	struct page *discard;

	if (mem_map == (mem_map_t *)NULL)
		mem_map = (mem_map_t *)PAGE_OFFSET;

	free_area_init_core(nid, pgdat, &discard, zones_size, zone_start_paddr);

	/*
	 * Get space for the valid bitmap.
	 */
	for (i = 0; i < MAX_NR_ZONES; i++)
		size += zones_size[i];
	size = LONG_ALIGN((size + 7) >> 3);
	pgdat->valid_addr_bitmap = (unsigned long *)alloc_bootmem_node(nid, size);
	memset(pgdat->valid_addr_bitmap, 0, size);
}

/*
 * This can be refined. Currently, tries to do round robin, instead
 * should do concentratic circle search, starting from current node.
 */
struct page * alloc_pages(int gfp_mask, unsigned long order)
{
	struct page *ret = 0;
	unsigned long flags;
	int startnode, tnode;
	static int nextnid = 0;

	if (order >= MAX_ORDER)
		return NULL;
	spin_lock_irqsave(&node_lock, flags);
	tnode = nextnid;
	nextnid++;
	if (nextnid == numnodes)
		nextnid = 0;
	spin_unlock_irqrestore(&node_lock, flags);
	startnode = tnode;
	while (tnode < numnodes) {
		if ((ret = alloc_pages_node(tnode++, gfp_mask, order)))
			return(ret);
	}
	tnode = 0;
	while (tnode != startnode) {
		if ((ret = alloc_pages_node(tnode++, gfp_mask, order)))
			return(ret);
	}
	return(0);
}

#endif /* CONFIG_DISCONTIGMEM */

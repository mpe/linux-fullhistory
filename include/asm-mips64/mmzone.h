/*
 * Written by Kanoj Sarcar (kanoj@sgi.com) Aug 99
 */
#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_

#include <linux/config.h>
#include <asm/sn/types.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>

typedef struct plat_pglist_data {
	pg_data_t	gendata;
	unsigned long	physstart;
	unsigned long	size;
	unsigned long	start_mapnr;
} plat_pg_data_t;

/*
 * Following are macros that are specific to this numa platform.
 */

extern int numa_debug(void);
extern plat_pg_data_t *plat_node_data[];

#define PHYSADDR_TO_NID(pa)		NASID_TO_COMPACT_NODEID(NASID_GET(pa))
#define PLAT_NODE_DATA(n)		(plat_node_data[n])
#define PLAT_NODE_DATA_STARTNR(n)	(PLAT_NODE_DATA(n)->start_mapnr)
#define PLAT_NODE_DATA_LOCALNR(p, n) \
			(((p) - PLAT_NODE_DATA(n)->physstart) >> PAGE_SHIFT)
#define PAGE_TO_PLAT_NODE(p)		(plat_pg_data_t *)((p)->zone->zone_pgdat)

#ifdef CONFIG_DISCONTIGMEM

/*
 * Following are macros that each numa implmentation must define.
 */

/*
 * Given a kernel address, find the home node of the underlying memory.
 * For production kern_addr_valid, change to return "numnodes" instead
 * of panicing.
 */
#define KVADDR_TO_NID(kaddr) \
	((NASID_TO_COMPACT_NODEID(NASID_GET(__pa(kaddr))) != -1) ? \
	(NASID_TO_COMPACT_NODEID(NASID_GET(__pa(kaddr)))) : \
	(printk("NUMABUG: %s line %d addr 0x%lx", __FILE__, __LINE__, kaddr), \
	numa_debug(), -1))

/*
 * Return a pointer to the node data for node n.
 */
#define NODE_DATA(n)	(&((PLAT_NODE_DATA(n))->gendata))

/*
 * NODE_MEM_MAP gives the kaddr for the mem_map of the node.
 */
#define NODE_MEM_MAP(nid)	(NODE_DATA(nid)->node_mem_map)

/*
 * Given a mem_map_t, LOCAL_MAP_BASE finds the owning node for the
 * physical page and returns the kaddr for the mem_map of that node.
 */
#define LOCAL_MAP_BASE(page) \
			NODE_MEM_MAP(KVADDR_TO_NID((unsigned long)(page)))

/*
 * Given a kaddr, ADDR_TO_MAPBASE finds the owning node of the memory
 * and returns the the mem_map of that node.
 */
#define ADDR_TO_MAPBASE(kaddr) \
			NODE_MEM_MAP(KVADDR_TO_NID((unsigned long)(kaddr)))

/*
 * Given a kaddr, LOCAL_BASE_ADDR finds the owning node of the memory
 * and returns the kaddr corresponding to first physical page in the
 * node's mem_map.
 */
#define LOCAL_BASE_ADDR(kaddr)	((unsigned long)(kaddr) & ~(NODE_MAX_MEM_SIZE-1))

#endif /* CONFIG_DISCONTIGMEM */

#endif /* _ASM_MMZONE_H_ */

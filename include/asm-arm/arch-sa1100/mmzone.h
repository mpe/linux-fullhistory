/*
 * linux/include/asm-arm/arch-sa1100/mmzone.h
 *
 * (C) 1999-2000, Nicolas Pitre <nico@cam.org>
 * (inspired by Kanoj Sarcar's code)
 *
 * Because of the wide memory address space between physical RAM banks on the 
 * SA1100, it's much convenient to use Linux's NUMA support to implement our 
 * memory map representation.  Assuming all memory nodes have equal access 
 * characteristics, we then have generic discontigous memory support.
 *
 * Of course, all this isn't mandatory for SA1100 implementations with only
 * one used memory bank.  For those, simply undefine CONFIG_DISCONTIGMEM.
 */


/*
 * Currently defined in arch/arm/mm/mm-sa1100.c
 */
extern pg_data_t sa1100_node_data[];

/*
 * 32MB max in each bank, must fit with __virt_to_phys() & __phys_to_virt()
 */
#define NODE_MAX_MEM_SHIFT	25
#define NODE_MAX_MEM_SIZE	(1<<NODE_MAX_MEM_SHIFT)

/*
 * Given a kernel address, find the home node of the underlying memory.
 */
#define KVADDR_TO_NID(addr) \
		(((unsigned long)(addr) - PAGE_OFFSET) >> NODE_MAX_MEM_SHIFT)

/*
 * Return a pointer to the node data for node n.
 */
#define NODE_DATA(nid)	(&sa1100_node_data[nid])

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

/*
 * Given a kaddr, LOCAL_MEM_MAP finds the owning node of the memory
 * and returns the index corresponding to the appropriate page in the
 * node's mem_map.
 */
#define LOCAL_MAP_NR(kvaddr) \
	(((unsigned long)(kvaddr)-LOCAL_BASE_ADDR((kvaddr))) >> PAGE_SHIFT)

/* 
 * With discontigmem, the conceptual mem_map array starts from PAGE_OFFSET.
 * Given a kaddr, MAP_NR returns the appropriate global mem_map index so 
 * it matches the corresponding node's local mem_map.
 */
#define MAP_NR(kaddr)	(LOCAL_MAP_NR((kaddr)) + \
		(((unsigned long)ADDR_TO_MAPBASE((kaddr)) - PAGE_OFFSET) / \
		sizeof(mem_map_t)))


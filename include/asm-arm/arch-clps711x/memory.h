/*
 *  linux/include/asm-arm/arch-clps711x/mmu.h
 *
 *  Copyright (C) 1999 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_ARCH_MMU_H
#define __ASM_ARCH_MMU_H

#include <linux/config.h>

/*
 * Task size: 3GB
 */
#define TASK_SIZE	(0xc0000000UL)
#define TASK_SIZE_26	(0x04000000UL)

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE (TASK_SIZE / 3)

/*
 * Page offset: 3GB
 */
#define PAGE_OFFSET	(0xc0000000UL)
#define PHYS_OFFSET	(0xc0000000UL)

/*
 * On integrator, the dram is contiguous
 */
#define __virt_to_phys__is_a_macro
#define __virt_to_phys(vpage) ((vpage) - PAGE_OFFSET + PHYS_OFFSET)
#define __phys_to_virt__is_a_macro
#define __phys_to_virt(ppage) ((ppage) + PAGE_OFFSET - PHYS_OFFSET)

/*
 * Virtual view <-> DMA view memory address translations
 * virt_to_bus: Used to translate the virtual address to an
 *              address suitable to be passed to set_dma_addr
 * bus_to_virt: Used to convert an address for DMA operations
 *              to an address that the kernel can use.
 */

#if defined(CONFIG_ARCH_CDB89712)

#define __virt_to_bus__is_a_macro
#define __virt_to_bus(x)	(x)
#define __bus_to_virt__is_a_macro
#define __bus_to_virt(x)	(x)

#elif defined (CONFIG_ARCH_AUTCPU12)

#define __virt_to_bus__is_a_macro
#define __virt_to_bus(x)	(x)
#define __bus_to_virt__is_a_macro
#define __bus_to_virt(x)	(x)

#else

#define __virt_to_bus__is_a_macro
#define __virt_to_bus(x)	((x) - PAGE_OFFSET)
#define __bus_to_virt__is_a_macro
#define __bus_to_virt(x)	((x) + PAGE_OFFSET)

#endif


/*
 * Like the SA1100, the EDB7211 has a large gap between physical RAM
 * banks.  In 2.2, the Psion (CL-PS7110) port added custom support for
 * discontiguous physical memory.  In 2.4, we can use the standard
 * Linux NUMA support.
 *
 * This is not necessary for EP7211 implementations with only one used
 * memory bank.  For those systems, simply undefine CONFIG_DISCONTIGMEM.
 */

#ifdef CONFIG_ARCH_EDB7211

#ifdef CONFIG_DISCONTIGMEM
/*
 * Because of the wide memory address space between physical RAM banks on the 
 * SA1100, it's much more convenient to use Linux's NUMA support to implement
 * our memory map representation.  Assuming all memory nodes have equal access 
 * characteristics, we then have generic discontigous memory support.
 *
 * Of course, all this isn't mandatory for SA1100 implementations with only
 * one used memory bank.  For those, simply undefine CONFIG_DISCONTIGMEM.
 *
 * The nodes are matched with the physical memory bank addresses which are 
 * incidentally the same as virtual addresses.
 * 
 * 	node 0:  0xc0000000 - 0xc7ffffff
 * 	node 1:  0xc8000000 - 0xcfffffff
 * 	node 2:  0xd0000000 - 0xd7ffffff
 * 	node 3:  0xd8000000 - 0xdfffffff
 */

#define NR_NODES	4

/*
 * Given a kernel address, find the home node of the underlying memory.
 */
#define KVADDR_TO_NID(addr) \
		(((unsigned long)(addr) - PAGE_OFFSET) >> NODE_MAX_MEM_SHIFT)

/*
 * Given a physical address, convert it to a node id.
 */
#define PHYS_TO_NID(addr) KVADDR_TO_NID(__phys_to_virt(addr))

/*
 * Given a kaddr, ADDR_TO_MAPBASE finds the owning node of the memory
 * and returns the mem_map of that node.
 */
#define ADDR_TO_MAPBASE(kaddr) \
			NODE_MEM_MAP(KVADDR_TO_NID((unsigned long)(kaddr)))

/*
 * Given a kaddr, LOCAL_MAR_NR finds the owning node of the memory
 * and returns the index corresponding to the appropriate page in the
 * node's mem_map.
 */
#define LOCAL_MAP_NR(kaddr) \
	(((unsigned long)(kaddr)-LOCAL_BASE_ADDR((kaddr))) >> PAGE_SHIFT)

/*
 * Given a kaddr, virt_to_page returns a pointer to the corresponding 
 * mem_map entry.
 */
#define virt_to_page(kaddr) \
	(ADDR_TO_MAPBASE(kaddr) + LOCAL_MAP_NR(kaddr))

/*
 * VALID_PAGE returns a non-zero value if given page pointer is valid.
 * This assumes all node's mem_maps are stored within the node they refer to.
 */
#define VALID_PAGE(page) \
({ unsigned int node = KVADDR_TO_NID(page); \
   ( (node < NR_NODES) && \
     ((unsigned)((page) - NODE_MEM_MAP(node)) < NODE_DATA(node)->node_size) ); \
})

/*
 * The PS7211 allows up to 256MB max per DRAM bank, but the EDB7211
 * uses only one of the two banks (bank #1).  However, even within
 * bank #1, memory is discontiguous.
 *
 * The EDB7211 has two 8MB DRAM areas with 8MB of empty space between
 * them, so we use 24 for the node max shift to get 16MB node sizes.
 */
#define NODE_MAX_MEM_SHIFT	24
#define NODE_MAX_MEM_SIZE	(1<<NODE_MAX_MEM_SHIFT)

/*
 * Given a mem_map_t, LOCAL_MAP_BASE finds the owning node for the
 * physical page and returns the kaddr for the mem_map of that node.
 */
#define LOCAL_MAP_BASE(page) \
			NODE_MEM_MAP(KVADDR_TO_NID((unsigned long)(page)))

/*
 * Given a kaddr, LOCAL_BASE_ADDR finds the owning node of the memory
 * and returns the kaddr corresponding to first physical page in the
 * node's mem_map.
 */
#define LOCAL_BASE_ADDR(kaddr)	((unsigned long)(kaddr) & ~(NODE_MAX_MEM_SIZE-1))

/* 
 * With discontigmem, the conceptual mem_map array starts from PAGE_OFFSET.
 * Given a kaddr, MAP_NR returns the appropriate global mem_map index so 
 * it matches the corresponding node's local mem_map.
 */
#define MAP_NR(kaddr)	(LOCAL_MAP_NR((kaddr)) + \
		(((unsigned long)ADDR_TO_MAPBASE((kaddr)) - PAGE_OFFSET) / \
		sizeof(mem_map_t)))

#else

#define PHYS_TO_NID(addr)	(0)

#endif /* CONFIG_DISCONTIGMEM */

#endif	/* CONFIG_ARCH_EDB7211 */

#ifndef PHYS_TO_NID
#define PHYS_TO_NID(addr)	(0)
#endif

#endif


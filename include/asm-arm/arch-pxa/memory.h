/*
 *  linux/include/asm-arm/arch-pxa/memory.h
 *
 * Author:	Nicolas Pitre
 * Copyright:	(C) 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H


/*
 * Task size: 3GB
 */
#define TASK_SIZE	(0xbf000000UL)
#define TASK_SIZE_26	(0x04000000UL)

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE (0x40000000)

/*
 * Page offset: 3GB
 */
#define PAGE_OFFSET	(0xc0000000UL)

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET	(0xa0000000UL)

/*
 * physical vs virtual ram conversion
 */
#define __virt_to_phys__is_a_macro
#define __phys_to_virt__is_a_macro
#define __virt_to_phys(x)	((x) - PAGE_OFFSET + PHYS_OFFSET)
#define __phys_to_virt(x)	((x) - PHYS_OFFSET + PAGE_OFFSET)

/*
 * Virtual view <-> DMA view memory address translations
 * virt_to_bus: Used to translate the virtual address to an
 *		address suitable to be passed to set_dma_addr
 * bus_to_virt: Used to convert an address for DMA operations
 *		to an address that the kernel can use.
 */
#define __virt_to_bus__is_a_macro
#define __bus_to_virt__is_a_macro
#define __virt_to_bus(x)	 __virt_to_phys(x)
#define __bus_to_virt(x)	 __phys_to_virt(x)

#ifdef CONFIG_DISCONTIGMEM
/*
 * The nodes are matched with the physical SDRAM banks as follows:
 *
 * 	node 0:  0xa0000000-0xa3ffffff	-->  0xc0000000-0xc3ffffff
 * 	node 1:  0xa4000000-0xa7ffffff	-->  0xc4000000-0xc7ffffff
 * 	node 2:  0xa8000000-0xabffffff	-->  0xc8000000-0xcbffffff
 * 	node 3:  0xac000000-0xafffffff	-->  0xcc000000-0xcfffffff
 */

#define NR_NODES	4

/*
 * Given a kernel address, find the home node of the underlying memory.
 */
#define KVADDR_TO_NID(addr) (((unsigned long)(addr) - PAGE_OFFSET) >> 26)

/*
 * Given a page frame number, convert it to a node id.
 */
#define PFN_TO_NID(pfn)		(((pfn) - PHYS_PFN_OFFSET) >> (26 - PAGE_SHIFT))

/*
 * Given a kaddr, ADDR_TO_MAPBASE finds the owning node of the memory
 * and returns the mem_map of that node.
 */
#define ADDR_TO_MAPBASE(kaddr)	NODE_MEM_MAP(KVADDR_TO_NID(kaddr))

/*
 * Given a page frame number, find the owning node of the memory
 * and returns the mem_map of that node.
 */
#define PFN_TO_MAPBASE(pfn)	NODE_MEM_MAP(PFN_TO_NID(pfn))

/*
 * Given a kaddr, LOCAL_MEM_MAP finds the owning node of the memory
 * and returns the index corresponding to the appropriate page in the
 * node's mem_map.
 */
#define LOCAL_MAP_NR(addr) \
	(((unsigned long)(addr) & 0x03ffffff) >> PAGE_SHIFT)

#else

#define PFN_TO_NID(addr)	(0)

#endif

#endif

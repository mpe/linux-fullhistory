/*
 * linux/include/asm-arm/arch-arc/memory.h
 *
 * Copyright (c) 1996-1999 Russell King.
 *
 * Changelog:
 *  22-Nov-1996	RMK	Created
 *  21-Mar-1999	RMK	Renamed to memory.h
 *		RMK	Moved PAGE_OFFSET and TASK_SIZE here
 */
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * User space: 26MB
 */
#define TASK_SIZE	(0x01a00000UL)

/*
 * Page offset: 32MB
 */
#define PAGE_OFFSET	(0x02000000UL)

#define __virt_to_phys__is_a_macro
#define __virt_to_phys(vpage) vpage
#define __phys_to_virt__is_a_macro
#define __phys_to_virt(ppage) ppage

/*
 * Virtual view <-> DMA view memory address translations
 * virt_to_bus: Used to translate the virtual address to an
 *              address suitable to be passed to set_dma_addr
 * bus_to_virt: Used to convert an address for DMA operations
 *              to an address that the kernel can use.
 */
#define __virt_to_bus__is_a_macro
#define __virt_to_bus(x)	(x)
#define __bus_to_virt__is_a_macro
#define __bus_to_virt(x)	(x)

#endif

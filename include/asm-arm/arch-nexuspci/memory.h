/*
 * linux/include/asm-arm/arch-nexuspci/memory.h
 *
 * Copyright (c) 1997, 1998 Philip Blundell.
 * Copyright (c) 1999 Russell King
 *
 */
#ifndef __ASM_ARCH_MMU_H
#define __ASM_ARCH_MMU_H

/*
 * Task size: 3GB
 */
#define TASK_SIZE	(0xc0000000UL)

/*
 * Page offset: 3GB
 */
#define PAGE_OFFSET	(0xc0000000UL)

/*
 * On NexusPCI, the DRAM is contiguous
 */
#define __virt_to_phys(vpage) ((vpage) - PAGE_OFFSET + 0x40000000)
#define __phys_to_virt(ppage) ((ppage) + PAGE_OFFSET - 0x40000000)
#define __virt_to_phys__is_a_macro
#define __phys_to_virt__is_a_macro

/*
 * On the PCI bus the DRAM appears at address 0
 */
#define __virt_to_bus__is_a_macro
#define __virt_to_bus(x) ((x) - PAGE_OFFSET)
#define __bus_to_virt__is_a_macro
#define __bus_to_virt(x) ((x) + PAGE_OFFSET)

#endif

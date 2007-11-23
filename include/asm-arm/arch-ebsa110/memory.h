/*
 * linux/include/asm-arm/arch-ebsa110/memory.h
 *
 * Copyright (c) 1996-1999 Russell King.
 *
 * Changelog:
 *  20-Oct-1996	RMK	Created
 *  31-Dec-1997	RMK	Fixed definitions to reduce warnings
 *  21-Mar-1999	RMK	Renamed to memory.h
 *		RMK	Moved TASK_SIZE and PAGE_OFFSET here
 */
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Task size: 3GB
 */
#define TASK_SIZE	(0xc0000000UL)

/*
 * Page offset: 3GB
 */
#define PAGE_OFFSET	(0xc0000000UL)

#define __virt_to_phys__is_a_macro
#define __virt_to_phys(vpage) ((vpage) - PAGE_OFFSET)
#define __phys_to_virt__is_a_macro
#define __phys_to_virt(ppage) ((ppage) + PAGE_OFFSET)

#define __virt_to_bus__is_a_macro
#define __virt_to_bus(x)	__virt_to_phys(x)
#define __bus_to_virt__is_a_macro
#define __bus_to_virt(x)	__phys_to_virt(x)

#endif

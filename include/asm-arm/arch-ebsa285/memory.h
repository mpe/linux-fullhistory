/*
 * linux/include/asm-arm/arch-ebsa285/memory.h
 *
 * Copyright (c) 1996-1999 Russell King.
 *
 * Changelog:
 *  20-Oct-1996	RMK	Created
 *  31-Dec-1997	RMK	Fixed definitions to reduce warnings.
 *  17-May-1998	DAG	Added __virt_to_bus and __bus_to_virt functions.
 *  21-Nov-1998	RMK	Changed __virt_to_bus and __bus_to_virt to macros.
 *  21-Mar-1999	RMK	Added PAGE_OFFSET for co285 architecture.
 *			Renamed to memory.h
 *			Moved PAGE_OFFSET and TASK_SIZE here
 */
#ifndef __ASM_ARCH_MMU_H
#define __ASM_ARCH_MMU_H

#include <linux/config.h>

#if defined(CONFIG_FOOTBRIDGE_ADDIN)
/*
 * If we may be using add-in footbridge mode, then we must
 * use the out-of-line translation that makes use of the
 * PCI BAR
 */
#ifndef __ASSEMBLY__
extern unsigned long __virt_to_bus(unsigned long);
extern unsigned long __bus_to_virt(unsigned long);
#endif

#elif defined(CONFIG_FOOTBRIDGE_HOST)

#define __virt_to_bus__is_a_macro
#define __virt_to_bus(x)	((x) - 0xe0000000)
#define __bus_to_virt__is_a_macro
#define __bus_to_virt(x)	((x) + 0xe0000000)

#else

#error "Undefined footbridge mode"

#endif

#if defined(CONFIG_ARCH_FOOTBRIDGE)

/* Task size and page offset at 3GB */
#define TASK_SIZE		(0xc0000000UL)
#define PAGE_OFFSET		(0xc0000000UL)

#elif defined(CONFIG_ARCH_CO285)

/* Task size and page offset at 1.5GB */
#define TASK_SIZE		(0x60000000UL)
#define PAGE_OFFSET		(0x60000000UL)

#else

#error "Undefined footbridge architecture"

#endif

#define TASK_SIZE_26		(0x04000000UL)
#define PHYS_OFFSET		(0x00000000UL)

/*
 * The DRAM is always contiguous.
 */
#define __virt_to_phys__is_a_macro
#define __virt_to_phys(vpage) ((unsigned long)(vpage) - PAGE_OFFSET)
#define __phys_to_virt__is_a_macro
#define __phys_to_virt(ppage) ((unsigned long)(ppage) + PAGE_OFFSET)

#endif

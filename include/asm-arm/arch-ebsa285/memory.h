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

#if defined(CONFIG_HOST_FOOTBRIDGE)

/*
 * Task size: 3GB
 */
#define TASK_SIZE		(0xc0000000UL)

/*
 * Page offset: 3GB
 */
#define PAGE_OFFSET		(0xc0000000UL)

#define __virt_to_bus__is_a_macro
#define __virt_to_bus(x)	((x) - 0xe0000000)
#define __bus_to_virt__is_a_macro
#define __bus_to_virt(x)	((x) + 0xe0000000)

#elif defined(CONFIG_ADDIN_FOOTBRIDGE)

#if defined(CONFIG_ARCH_CO285)

/*
 * Task size: 1.5GB
 */
#define TASK_SIZE		(0x60000000UL)

/*
 * Page offset: 1.5GB
 */
#define PAGE_OFFSET		(0x60000000UL)

#else

#error Add in your architecture here

#endif

#ifndef __ASSEMBLY__
extern unsigned long __virt_to_bus(unsigned long);
extern unsigned long __bus_to_virt(unsigned long);
#endif

#endif

/*
 * On Footbridge machines, the dram is contiguous.
 * On Host Footbridge, these conversions are constant.
 * On an add-in footbridge, these depend on register settings.
 */
#define __virt_to_phys__is_a_macro
#define __virt_to_phys(vpage) ((unsigned long)(vpage) - PAGE_OFFSET)
#define __phys_to_virt__is_a_macro
#define __phys_to_virt(ppage) ((unsigned long)(ppage) + PAGE_OFFSET)

#endif

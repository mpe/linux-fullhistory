/*
 * linux/include/asm-arm/arch-ebsa285/mmu.h
 *
 * Copyright (c) 1996,1997,1998 Russell King.
 *
 * Changelog:
 *  20-10-1996	RMK	Created
 *  31-12-1997	RMK	Fixed definitions to reduce warnings
 *  17-05-1998	DAG	Added __virt_to_bus and __bus_to_virt functions.
 *  21-11-1998	RMK	Changed __virt_to_bus and __bus_to_virt to macros.
 */
#ifndef __ASM_ARCH_MMU_H
#define __ASM_ARCH_MMU_H

/*
 * On ebsa285, the dram is contiguous
 */
#define __virt_to_phys__is_a_macro
#define __virt_to_phys(vpage) ((unsigned long)(vpage) - PAGE_OFFSET)
#define __phys_to_virt__is_a_macro
#define __phys_to_virt(ppage) ((unsigned long)(ppage) + PAGE_OFFSET)

#define __virt_to_bus__is_a_macro
#define __virt_to_bus(x)	((x) - 0xe0000000)
#define __bus_to_virt__is_a_macro
#define __bus_to_virt(x)	((x) + 0xe0000000)

#endif

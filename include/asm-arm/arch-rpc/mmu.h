/*
 * linux/include/asm-arm/arch-rpc/mmu.h
 *
 * Copyright (c) 1996,1997,1998 Russell King.
 *
 * Changelog:
 *  20-10-1996	RMK	Created
 *  31-12-1997	RMK	Fixed definitions to reduce warnings
 *  11-01-1998	RMK	Uninlined to reduce hits on cache
 *  08-02-1998	RMK	Added __virt_to_bus and __bus_to_virt
 */
#ifndef __ASM_ARCH_MMU_H
#define __ASM_ARCH_MMU_H

extern unsigned long __virt_to_phys(unsigned long vpage);
extern unsigned long __phys_to_virt(unsigned long ppage);

/*
 * These are exactly the same on the RiscPC as the
 * physical memory view.
 */
#define __virt_to_bus__is_a_macro
#define __virt_to_bus(x) __virt_to_phys(x)
#define __bus_to_virt__is_a_macro
#define __bus_to_virt(x) __phys_to_virt(x)

#endif

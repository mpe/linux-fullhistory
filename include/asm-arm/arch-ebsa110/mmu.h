/*
 * linux/include/asm-arm/arch-ebsa110/mmu.h
 *
 * Copyright (c) 1996,1997,1998 Russell King.
 *
 * Changelog:
 *  20-10-1996	RMK	Created
 *  31-12-1997	RMK	Fixed definitions to reduce warnings
 */
#ifndef __ASM_ARCH_MMU_H
#define __ASM_ARCH_MMU_H

/*
 * On ebsa, the dram is contiguous
 */
#define __virt_to_phys(vpage) ((vpage) - PAGE_OFFSET)
#define __phys_to_virt(ppage) ((ppage) + PAGE_OFFSET)

#endif

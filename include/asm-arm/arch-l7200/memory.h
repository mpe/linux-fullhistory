/*
 * linux/include/asm-arm/arch-l7200/memory.h
 *
 * Copyright (c) 2000 Steven Hill (sjhill@cotw.com)
 * Copyright (c) 2000 Rob Scott (rscott@mtrob.fdns.net)
 *
 * Changelog:
 *  03-13-2000	SJH	Created
 *  04-13-2000  RS      Changed bus macros for new addr
 *  05-03-2000  SJH     Removed bus macros and fixed virt_to_phys macro
 */
#ifndef __ASM_ARCH_MMU_H
#define __ASM_ARCH_MMU_H

/*
 * Task size: 3GB
 */
#define TASK_SIZE       (0xc0000000UL)
#define TASK_SIZE_26    (0x04000000UL)

/*
 * Page offset: 3GB
 */
#define PAGE_OFFSET     (0xc0000000UL)

/*
 * Physical DRAM offset on the L7200 SDB.
 */
#define PHYS_OFFSET     (0xf0000000UL)

/*
 * The DRAM is contiguous.
 */
#define __virt_to_phys__is_a_macro
#define __virt_to_phys(vpage) ((vpage) - PAGE_OFFSET + PHYS_OFFSET)
#define __phys_to_virt__is_a_macro
#define __phys_to_virt(ppage) ((ppage) + PAGE_OFFSET - PHYS_OFFSET)

#define __virt_to_bus__is_a_macro
#define __virt_to_bus(x) __virt_to_phys(x)
#define __bus_to_virt__is_a_macro
#define __bus_to_virt(x) __phys_to_virt(x)

#endif

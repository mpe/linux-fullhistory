/*
 * linux/include/asm-arm/arch-nexuspci/mmu.h
 *
 * Copyright (c) 1997 Philip Blundell.
 *
 */
#ifndef __ASM_ARCH_MMU_H
#define __ASM_ARCH_MMU_H

/*
 * On NexusPCI, the dram is contiguous
 */
#define __virt_to_phys(vpage) ((vpage) - PAGE_OFFSET + 0x40000000)
#define __phys_to_virt(ppage) ((ppage) + PAGE_OFFSET - 0x40000000)

#endif

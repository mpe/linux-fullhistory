/*
 * linux/include/asm-arm/arch-cl7500/dma.h
 *
 * Copyright (C) 1999 Nexus Electronics Ltd.
 */

#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

/*
 * This is the maximum DMA address that can be DMAd to.
 * There should not be more than (0xd0000000 - 0xc0000000)
 * bytes of RAM.
 */
#define MAX_DMA_ADDRESS		0xd0000000

#define MAX_DMA_CHANNELS	1

#define DMA_S0			0

#endif /* _ASM_ARCH_DMA_H */

/*
 * linux/include/asm-arm/arch-ebsa110/dma.h
 *
 * Architecture DMA routes
 *
 * Copyright (C) 1997.1998 Russell King
 */
#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

/*
 * This is the maximum DMA address that can be DMAd to.
 * There should not be more than (0xd0000000 - 0xc0000000)
 * bytes of RAM.
 */
#define MAX_DMA_ADDRESS		0xd0000000

/*
 * DMA modes - we have two, IN and OUT
 */
typedef enum {
	DMA_MODE_READ,
	DMA_MODE_WRITE
} dmamode_t;

#define MAX_DMA_CHANNELS	8

#endif /* _ASM_ARCH_DMA_H */

/*
 * linux/include/asm-arm/arch-arc/dma.h
 *
 * Copyright (C) 1996-1998 Russell King
 *
 * Acorn Archimedes/A5000 architecture virtual DMA
 * implementation
 *
 * Modifications:
 *  04-04-1998	RMK	Merged arc and a5k versions
 */
#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

#include <linux/config.h>

#define MAX_DMA_ADDRESS		0x03000000

/*
 * DMA modes - we have two, IN and OUT
 */
typedef enum {
	DMA_MODE_READ,
	DMA_MODE_WRITE
} dmamode_t;

#define MAX_DMA_CHANNELS	4

#define DMA_0			0
#define DMA_1			1
#define DMA_VIRTUAL_FLOPPY	2
#define DMA_VIRTUAL_SOUND	3

#ifdef CONFIG_ARCH_A5K
#define DMA_FLOPPY		DMA_VIRTUAL_FLOPPY
#endif

#endif /* _ASM_ARCH_DMA_H */

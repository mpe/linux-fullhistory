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

#ifdef CONFIG_ARCH_ARC
#define MAX_DMA_CHANNELS	3

#define DMA_VIRTUAL_FLOPPY0	0
#define DMA_VIRTUAL_FLOPPY1	1
#define DMA_VIRTUAL_SOUND	2
#endif

#ifdef CONFIG_ARCH_A5K
#define MAX_DMA_CHANNELS	2

#define DMA_VIRTUAL_FLOPPY	0
#define DMA_VIRTUAL_SOUND	1
#define DMA_FLOPPY		DMA_VIRTUAL_FLOPPY
#endif

#endif /* _ASM_ARCH_DMA_H */

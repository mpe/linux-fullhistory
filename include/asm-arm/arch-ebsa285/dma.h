/*
 * linux/include/asm-arm/arch-ebsa285/dma.h
 *
 * Architecture DMA routines
 *
 * Copyright (C) 1998 Russell King
 * Copyright (C) 1998 Philip Blundell
 */
#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

/*
 * This is the maximum DMA address that can be DMAd to.
 */
#define MAX_DMA_ADDRESS		0xffffffff

/*
 * The 21285 has two internal DMA channels; we call these 0 and 1.
 * On CATS hardware we have an additional eight ISA dma channels
 * numbered 2..9.
 */
#define MAX_DMA_CHANNELS	10
#define DMA_ISA_BASE		2
#define DMA_FLOPPY		(DMA_ISA_BASE + 2)

#endif /* _ASM_ARCH_DMA_H */

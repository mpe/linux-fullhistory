/*
 * arch/arm/kernel/dma-ebsa285.c
 *
 * Copyright (C) 1998 Phil Blundell
 *
 * DMA functions specific to EBSA-285/CATS architectures
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/mman.h>
#include <linux/init.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/hardware.h>

#include "dma.h"

/* 8237 DMA controllers */
#define IO_DMA1_BASE	0x00	/* 8 bit slave DMA, channels 0..3 */
#define IO_DMA2_BASE	0xC0	/* 16 bit master DMA, ch 4(=slave input)..7 */

/* 8237 DMA controller registers */
#define DMA1_CMD_REG		0x08	/* command register (w) */
#define DMA1_STAT_REG		0x08	/* status register (r) */
#define DMA1_REQ_REG            0x09    /* request register (w) */
#define DMA1_MASK_REG		0x0A	/* single-channel mask (w) */
#define DMA1_MODE_REG		0x0B	/* mode register (w) */
#define DMA1_CLEAR_FF_REG	0x0C	/* clear pointer flip-flop (w) */
#define DMA1_TEMP_REG           0x0D    /* Temporary Register (r) */
#define DMA1_RESET_REG		0x0D	/* Master Clear (w) */
#define DMA1_CLR_MASK_REG       0x0E    /* Clear Mask */
#define DMA1_MASK_ALL_REG       0x0F    /* all-channels mask (w) */

#define DMA2_CMD_REG		0xD0	/* command register (w) */
#define DMA2_STAT_REG		0xD0	/* status register (r) */
#define DMA2_REQ_REG            0xD2    /* request register (w) */
#define DMA2_MASK_REG		0xD4	/* single-channel mask (w) */
#define DMA2_MODE_REG		0xD6	/* mode register (w) */
#define DMA2_CLEAR_FF_REG	0xD8	/* clear pointer flip-flop (w) */
#define DMA2_TEMP_REG           0xDA    /* Temporary Register (r) */
#define DMA2_RESET_REG		0xDA	/* Master Clear (w) */
#define DMA2_CLR_MASK_REG       0xDC    /* Clear Mask */
#define DMA2_MASK_ALL_REG       0xDE    /* all-channels mask (w) */

int arch_request_dma(dmach_t channel, dma_t *dma, const char *dev_name)
{
	/* 21285 internal channels */
	if (channel == 0 || channel == 1)
		return 0;

	/* ISA channels */
//	if (machine_is_cats() && ((channel >= 2 && channel <= 5) ||
//		(channel >= 7 && channel <= 9)))
//		return 0;

	return -EINVAL;
}

void arch_free_dma(dmach_t channel, dma_t *dma)
{
	/* nothing to do */
}

int arch_get_dma_residue(dmach_t channel, dma_t *dma)
{
	int residue = 0;

	switch (channel) {
	case 0:
	case 1:
		break;
#ifdef CONFIG_CATS
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
#endif
	}
	return residue;
}

void arch_enable_dma(dmach_t channel, dma_t *dma)
{
	switch (channel) {
	case 0:
	case 1:
		/*
		 * Not yet implemented
		 */
		break;
#ifdef CONFIG_CATS
	case 2:
	case 3:
	case 4:
	case 5:
	case 7:
	case 8:
	case 9:
		if (dma->invalid) {
			static unsigned char dma_page[] = { 0x87, 0x83, 0x81, 0x82,
							    0x00, 0x8b, 0x89, 0x8a };
			unsigned long int address = dma->buf.address,
				length = dma->buf.length - 1;
			outb(address >> 24, dma_page[channel - DMA_ISA_BASE] | 0x400);
			outb(address >> 16, dma_page[channel - DMA_ISA_BASE]);
			if (channel >= DMA_ISA_BASE + 5) {
				outb(0, DMA2_CLEAR_FF_REG);
				outb(address >> 1, 
				     IO_DMA2_BASE + ((channel - DMA_ISA_BASE - 4) << 2));
				outb(address >> 9, 
				     IO_DMA2_BASE + ((channel - DMA_ISA_BASE - 4) << 2));
				outb((length >> 1) & 0xfe,
				     IO_DMA2_BASE + 1 + ((channel - DMA_ISA_BASE - 4) << 2));
				outb(length >> 9,
				     IO_DMA2_BASE + 1 + ((channel - DMA_ISA_BASE - 4) << 2));
				outb(dma->dma_mode | (channel - DMA_ISA_BASE - 4), DMA2_MODE_REG);
			} else {
				outb(0, DMA1_CLEAR_FF_REG);
				outb(address >> 0, IO_DMA1_BASE + ((channel - DMA_ISA_BASE) << 1));
				outb(address >> 8, IO_DMA1_BASE + ((channel - DMA_ISA_BASE) << 1));
				outb(length >> 0,
				     IO_DMA1_BASE + 1 + ((channel - DMA_ISA_BASE) << 1));
				outb(length >> 8,
				     IO_DMA1_BASE + 1 + ((channel - DMA_ISA_BASE) << 1));
				outb(dma->dma_mode | (channel - DMA_ISA_BASE), DMA1_MODE_REG);
			}
			switch (dma->dma_mode) {
			case DMA_MODE_READ:
				dma_cache_inv(__bus_to_virt(address), length + 1);
				break;
			case DMA_MODE_WRITE:
				dma_cache_wback(__bus_to_virt(address), length + 1);
				break;
			}
			dma->invalid = 0;
		}

		if (channel >= DMA_ISA_BASE + 5)
			outb(channel - DMA_ISA_BASE - 4, DMA2_MASK_REG);
		else
			outb(channel - DMA_ISA_BASE, DMA1_MASK_REG);
#endif
	}
}

void arch_disable_dma(dmach_t channel, dma_t *dma)
{
	switch (channel) {
	case 0:
	case 1:
		/*
		 * Not yet implemented
		 */
		break;
#ifdef CONFIG_CATS
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
		if (channel >= DMA_ISA_BASE + 5)
			outb(channel - DMA_ISA_BASE, DMA2_MASK_REG);
		else
			outb((channel - DMA_ISA_BASE) | 4, DMA1_MASK_REG);
#endif
	}
}

__initfunc(void arch_dma_init(dma_t *dma))
{
	/* Nothing to do */
}

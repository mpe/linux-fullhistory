/*
 * arch/arm/kernel/dma-isa.c: ISA DMA primitives
 *
 * Copyright (C) Russell King
 *
 * Taken from various sources, including:
 *  linux/include/asm/dma.h: Defines for using and allocating dma channels.
 *    Written by Hennus Bergman, 1992.
 *    High DMA channel support & info by Hannu Savolainen and John Boyd, Nov. 1992.
 *  arch/arm/kernel/dma-ebsa285.c
 *  Copyright (C) 1998 Phil Blundell
 */
#include <linux/sched.h>

#include <asm/dma.h>
#include <asm/io.h>

#include "dma.h"
#include "dma-isa.h"

#define ISA_DMA_MASK		0
#define ISA_DMA_MODE		1
#define ISA_DMA_CLRFF		2
#define ISA_DMA_PGHI		3
#define ISA_DMA_PGLO		4
#define ISA_DMA_ADDR		5
#define ISA_DMA_COUNT		6

static unsigned int isa_dma_port[8][7] = {
	/* MASK   MODE   CLRFF  PAGE_HI PAGE_LO ADDR COUNT */
	{  0x0a,  0x0b,  0x0c,  0x487,  0x087,  0x00, 0x01 },
	{  0x0a,  0x0b,  0x0c,  0x483,  0x083,  0x02, 0x03 },
	{  0x0a,  0x0b,  0x0c,  0x481,  0x081,  0x04, 0x05 },
	{  0x0a,  0x0b,  0x0c,  0x482,  0x082,  0x06, 0x07 },
	{  0xd4,  0xd6,  0xd8,  0x000,  0x000,  0xc0, 0xc2 },
	{  0xd4,  0xd6,  0xd8,  0x48b,  0x08b,  0xc4, 0xc6 },
	{  0xd4,  0xd6,  0xd8,  0x489,  0x089,  0xc8, 0xca },
	{  0xd4,  0xd6,  0xd8,  0x48a,  0x08a,  0xcc, 0xce }
};

int isa_request_dma(int channel, dma_t *dma, const char *dev_name)
{
	if (channel != 4)
		return 0;

	return -EINVAL;
}

void isa_free_dma(int channel, dma_t *dma)
{
	/* nothing to do */
}

int isa_get_dma_residue(int channel, dma_t *dma)
{
	unsigned int io_port = isa_dma_port[channel][ISA_DMA_COUNT];
	int count;

	count = 1 + inb(io_port) + (inb(io_port) << 8);

	return channel < 4 ? count : (count << 1);
}

void isa_enable_dma(int channel, dma_t *dma)
{
	unsigned long address, length;

	if (dma->invalid) {
		address = dma->buf.address;
		length  = dma->buf.length - 1;

		outb(address >> 24, isa_dma_port[channel][ISA_DMA_PGHI]);
		outb(address >> 16, isa_dma_port[channel][ISA_DMA_PGLO]);

		if (channel >= 4) {
			address >>= 1;
			length = (length >> 1) & 0xfe; /* why &0xfe? */
		}

		outb(0, isa_dma_port[channel][ISA_DMA_CLRFF]);

		outb(address, isa_dma_port[channel][ISA_DMA_ADDR]);
		outb(address >> 8, isa_dma_port[channel][ISA_DMA_ADDR]);

		outb(length, isa_dma_port[channel][ISA_DMA_COUNT]);
		outb(length >> 8, isa_dma_port[channel][ISA_DMA_COUNT]);

		outb(dma->dma_mode | (channel & 3), isa_dma_port[channel][ISA_DMA_MODE]);

		switch (dma->dma_mode) {
		case DMA_MODE_READ:
			dma_cache_inv(__bus_to_virt(dma->buf.address), dma->buf.length);
			break;

		case DMA_MODE_WRITE:
			dma_cache_wback(__bus_to_virt(dma->buf.address), dma->buf.length);
			break;
		}
		dma->invalid = 0;
	}
	outb(channel & 3, isa_dma_port[channel][ISA_DMA_MASK]);
}

void isa_disable_dma(int channel, dma_t *dma)
{
	outb(channel | 4, isa_dma_port[channel][ISA_DMA_MASK]);
}

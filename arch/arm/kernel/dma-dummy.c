/*
 * arch/arm/kernel/dma-dummy.c
 *
 * Copyright (C) 1998 Russell King
 *
 * Dummy DMA functions
 */
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/hardware.h>

#include "dma.h"

int arch_request_dma(dmach_t channel, dma_t *dma, const char *devname)
{
	return -EINVAL;
}

void arch_free_dma(dmach_t channel, dma_t *dma)
{
	printk ("arch_free_dma: invalid channel %d\n", channel);
}

void arch_enable_dma(dmach_t channel, dma_t *dma)
{
	printk ("arch_enable_dma: invalid channel %d\n", channel);
}

void arch_disable_dma(dmach_t channel, dma_t *dma)
{
	printk ("arch_disable_dma: invalid channel %d\n", channel);
}

int arch_get_dma_residue(dmach_t channel, dma_t *dma)
{
	printk ("arch_get_dma_residue: invalid channel %d\n", channel);
	return 0;
}

__initfunc(void arch_dma_init(dma_t *dma))
{
}

/*
 * arch/arm/kernel/dma-vnc.c
 *
 * Copyright (C) 1998 Russell King
 */
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
#include "dma-isa.h"

int arch_request_dma(dmach_t channel, dma_t *dma, const char *dev_name)
{
	if (channel < 8)
		return isa_request_dma(channel, dma, dev_name);
	return -EINVAL;
}

void arch_free_dma(dmach_t channel, dma_t *dma)
{
	isa_free_dma(channel, dma);
}

int arch_get_dma_residue(dmach_t channel, dma_t *dma)
{
	return isa_get_dma_residue(channel, dma);
}

void arch_enable_dma(dmach_t channel, dma_t *dma)
{
	isa_enable_dma(channel, dma);
}

void arch_disable_dma(dmach_t channel, dma_t *dma)
{
	isa_disable_dma(channel, dma);
}

__initfunc(void arch_dma_init(dma_t *dma))
{
	/* Nothing to do */
}


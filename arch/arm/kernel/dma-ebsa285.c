/*
 * arch/arm/kernel/dma-ebsa285.c
 *
 * Copyright (C) 1998 Phil Blundell
 *
 * DMA functions specific to EBSA-285/CATS architectures
 *
 * Changelog:
 *  09/11/1998	RMK	Split out ISA DMA functions to dma-isa.c
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
#include "dma-isa.h"

int arch_request_dma(dmach_t channel, dma_t *dma, const char *dev_name)
{
	switch (channel) {
	case 0:
	case 1:	/* 21285 internal channels */
		return 0;

	case 2 ... 9:
		if (machine_is_cats())
			return isa_request_dma(channel - 2, dma, dev_name);
	}

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
	case 2 ... 9:
		if (machine_is_cats())
			residue = isa_get_dma_residue(channel - 2);
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
	case 2 ... 9:
		if (machine_is_cats())
			isa_enable_dma(channel - 2, dma);
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
	case 2 ... 9:
		if (machine_is_cats())
			isa_disable_dma(channel - 2, dma);
#endif
	}
}

__initfunc(void arch_dma_init(dma_t *dma))
{
	/* Nothing to do */
}

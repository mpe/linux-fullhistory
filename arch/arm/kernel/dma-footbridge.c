/*
 * arch/arm/kernel/dma-ebsa285.c
 *
 * Copyright (C) 1998 Phil Blundell
 *
 * DMA functions specific to EBSA-285/CATS architectures
 *
 * Changelog:
 *  09-Nov-1998	RMK	Split out ISA DMA functions to dma-isa.c
 *  17-Mar-1999	RMK	Allow any EBSA285-like architecture to have
 *			ISA DMA controllers.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/mman.h>
#include <linux/init.h>

#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/hardware.h>

#include "dma.h"
#include "dma-isa.h"

#ifdef CONFIG_ISA_DMA
static int has_isa_dma;
#else
#define has_isa_dma 0
#endif

int arch_request_dma(dmach_t channel, dma_t *dma, const char *dev_name)
{
	switch (channel) {
	case _DC21285_DMA(0):
	case _DC21285_DMA(1):	/* 21285 internal channels */
		return 0;

	case _ISA_DMA(0) ... _ISA_DMA(7):
		if (has_isa_dma)
			return isa_request_dma(channel - _ISA_DMA(0), dma, dev_name);
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
	case _DC21285_DMA(0):
	case _DC21285_DMA(1):
		break;

	case _ISA_DMA(0) ... _ISA_DMA(7):
		if (has_isa_dma)
			residue = isa_get_dma_residue(channel - _ISA_DMA(0), dma);
	}
	return residue;
}

void arch_enable_dma(dmach_t channel, dma_t *dma)
{
	switch (channel) {
	case _DC21285_DMA(0):
	case _DC21285_DMA(1):
		/*
		 * Not yet implemented
		 */
		break;

	case _ISA_DMA(0) ... _ISA_DMA(7):
		if (has_isa_dma)
			isa_enable_dma(channel - _ISA_DMA(0), dma);
	}
}

void arch_disable_dma(dmach_t channel, dma_t *dma)
{
	switch (channel) {
	case _DC21285_DMA(0):
	case _DC21285_DMA(1):
		/*
		 * Not yet implemented
		 */
		break;

	case _ISA_DMA(0) ... _ISA_DMA(7):
		if (has_isa_dma)
			isa_disable_dma(channel - _ISA_DMA(0), dma);
	}
}

int arch_set_dma_speed(dmach_t channel, dma_t *dma, int cycle_ns)
{
	return 0;
}

void __init arch_dma_init(dma_t *dma)
{
#ifdef CONFIG_ISA_DMA
	has_isa_dma = isa_init_dma();
#endif
}

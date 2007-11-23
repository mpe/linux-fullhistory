/*
 * arch/arm/kernel/dma-a5k.c
 *
 * Copyright (C) 1998 Russell King
 *
 * DMA functions specific to A5000 architecture
 */
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/dma.h>
#include <asm/fiq.h>
#include <asm/io.h>
#include <asm/hardware.h>

#include "dma.h"

static struct fiq_handler fh = { NULL, "floppydma", NULL, NULL };

int arch_request_dma(dmach_t channel, dma_t *dma, const char *dev_id)
{
	if (channel == DMA_VIRTUAL_FLOPPY)
		return 0;
	else
		return -EINVAL;
}

void arch_free_dma(dmach_t channel, dma_t *dma)
{
	if (channel != DMA_VIRTUAL_FLOPPY)
		printk("arch_free_dma: invalid channel %d\n", channel);
}

int arch_get_dma_residue(dmach_t channel, dma_t *dma)
{
	if (channel != DMA_VIRTUAL_FLOPPY)
		printk("arch_dma_count: invalid channel %d\n", channel);
	else {
		struct pt_regs regs;
		get_fiq_regs(&regs);
		return regs.ARM_r9;
	}
	return 0;
}

void arch_enable_dma(dmach_t channel, dma_t *dma)
{
	if (channel != DMA_VIRTUAL_FLOPPY)
		printk("arch_enable_dma: invalid channel %d\n", channel);
	else {
		struct pt_regs regs;
		void *fiqhandler_start;
		unsigned int fiqhandler_length;
		extern void floppy_fiqsetup(unsigned long len, unsigned long addr,
					     unsigned long port);

		if (dma->dma_mode == DMA_MODE_READ) {
			extern unsigned char floppy_fiqin_start, floppy_fiqin_end;
			fiqhandler_start = &floppy_fiqin_start;
			fiqhandler_length = &floppy_fiqin_end - &floppy_fiqin_start;
		} else {
			extern unsigned char floppy_fiqout_start, floppy_fiqout_end;
			fiqhandler_start = &floppy_fiqout_start;
			fiqhandler_length = &floppy_fiqout_end - &floppy_fiqout_start;
		}
		if (claim_fiq(&fh)) {
			printk("floppydma: couldn't claim FIQ.\n");
			return;
		}
		memcpy((void *)0x1c, fiqhandler_start, fiqhandler_length);
		regs.ARM_r9 = dma->buf.length;
		regs.ARM_r10 = __bus_to_virt(dma->buf.address);
		regs.ARM_fp = (int)PCIO_FLOPPYDMABASE;
		set_fiq_regs(&regs);
		enable_irq(dma->dma_irq);
	}
}

void arch_disable_dma(dmach_t channel, dma_t *dma)
{
	if (channel != DMA_VIRTUAL_FLOPPY)
		printk("arch_disable_dma: invalid channel %d\n", channel);
	else {
		disable_irq(dma->dma_irq);
		release_fiq(&fh);
	}
}

int arch_set_dma_speed(dmach_t channel, dma_t *dma, int cycle_ns)
{
	return 0;
}

void __init arch_dma_init(dma_t *dma)
{
	dma[DMA_VIRTUAL_FLOPPY].dma_irq = 64;
}

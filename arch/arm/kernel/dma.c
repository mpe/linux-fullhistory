/*
 * linux/arch/arm/kernel/dma.c
 *
 * Copyright (C) 1995-1998 Russell King
 *
 * Front-end to the DMA handling.  You must provide the following
 * architecture-specific routines:
 *
 *  int arch_request_dma(dmach_t channel, dma_t *dma, const char *dev_id);
 *  void arch_free_dma(dmach_t channel, dma_t *dma);
 *  void arch_enable_dma(dmach_t channel, dma_t *dma);
 *  void arch_disable_dma(dmach_t channel, dma_t *dma);
 *  int arch_get_dma_residue(dmach_t channel, dma_t *dma);
 *
 * Moved DMA resource allocation here...
 */
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/mman.h>
#include <linux/init.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/spinlock.h>


/* A note on resource allocation:
 *
 * All drivers needing DMA channels, should allocate and release them
 * through the public routines `request_dma()' and `free_dma()'.
 *
 * In order to avoid problems, all processes should allocate resources in
 * the same sequence and release them in the reverse order.
 *
 * So, when allocating DMAs and IRQs, first allocate the IRQ, then the DMA.
 * When releasing them, first release the DMA, then release the IRQ.
 * If you don't, you may cause allocation requests to fail unnecessarily.
 * This doesn't really matter now, but it will once we get real semaphores
 * in the kernel.
 */


spinlock_t dma_spin_lock = SPIN_LOCK_UNLOCKED;

#include "dma.h"

const char dma_str[] = "%s: dma %d not supported\n";

static dma_t dma_chan[MAX_DMA_CHANNELS];

/* Get dma list
 * for /proc/dma
 */
int get_dma_list(char *buf)
{
	int i, len = 0;

	for (i = 0; i < MAX_DMA_CHANNELS; i++) {
		if (dma_chan[i].lock)
			len += sprintf(buf + len, "%2d: %s\n",
				       i, dma_chan[i].device_id);
	}
	return len;
}

/* Request DMA channel
 *
 * On certain platforms, we have to allocate an interrupt as well...
 */
int request_dma(dmach_t channel, const char *device_id)
{
	if (channel < MAX_DMA_CHANNELS) {
		int ret;

		if (xchg(&dma_chan[channel].lock, 1) != 0)
			return -EBUSY;

		ret = arch_request_dma(channel, &dma_chan[channel], device_id);
		if (!ret) {
			dma_chan[channel].device_id = device_id;
			dma_chan[channel].active    = 0;
			dma_chan[channel].invalid   = 1;
		} else
			xchg(&dma_chan[channel].lock, 0);

		return ret;
	} else {
		printk (KERN_ERR "Trying to allocate DMA%d\n", channel);
		return -EINVAL;
	}
}

/* Free DMA channel
 *
 * On certain platforms, we have to free interrupt as well...
 */
void free_dma(dmach_t channel)
{
	if (channel >= MAX_DMA_CHANNELS) {
		printk (KERN_ERR "Trying to free DMA%d\n", channel);
		return;
	}

	if (xchg(&dma_chan[channel].lock, 0) == 0) {
		if (dma_chan[channel].active) {
			printk (KERN_ERR "Freeing active DMA%d\n", channel);
			arch_disable_dma(channel, &dma_chan[channel]);
			dma_chan[channel].active = 0;
		}

		printk (KERN_ERR "Trying to free free DMA%d\n", channel);
		return;
	}
	arch_free_dma(channel, &dma_chan[channel]);
}

/* Set DMA Scatter-Gather list
 */
void set_dma_sg (dmach_t channel, dmasg_t *sg, int nr_sg)
{
	dma_chan[channel].sg = sg;
	dma_chan[channel].sgcount = nr_sg;
	dma_chan[channel].invalid = 1;
}

/* Set DMA address
 *
 * Copy address to the structure, and set the invalid bit
 */
void set_dma_addr (dmach_t channel, unsigned long physaddr)
{
	if (dma_chan[channel].active)
		printk(KERN_ERR "set_dma_addr: altering DMA%d"
		       " address while DMA active\n",
		       channel);

	dma_chan[channel].sg = &dma_chan[channel].buf;
	dma_chan[channel].sgcount = 1;
	dma_chan[channel].buf.address = physaddr;
	dma_chan[channel].invalid = 1;
}

/* Set DMA byte count
 *
 * Copy address to the structure, and set the invalid bit
 */
void set_dma_count (dmach_t channel, unsigned long count)
{
	if (dma_chan[channel].active)
		printk(KERN_ERR "set_dma_count: altering DMA%d"
		       " count while DMA active\n",
		       channel);

	dma_chan[channel].sg = &dma_chan[channel].buf;
	dma_chan[channel].sgcount = 1;
	dma_chan[channel].buf.length = count;
	dma_chan[channel].invalid = 1;
}

/* Set DMA direction mode
 */
void set_dma_mode (dmach_t channel, dmamode_t mode)
{
	if (dma_chan[channel].active)
		printk(KERN_ERR "set_dma_mode: altering DMA%d"
		       " mode while DMA active\n",
		       channel);

	dma_chan[channel].dma_mode = mode;
	dma_chan[channel].invalid = 1;
}

/* Enable DMA channel
 */
void enable_dma (dmach_t channel)
{
	if (dma_chan[channel].lock) {
		if (dma_chan[channel].active == 0) {
			dma_chan[channel].active = 1;
			arch_enable_dma(channel, &dma_chan[channel]);
		}
	} else
		printk (KERN_ERR "Trying to enable free DMA%d\n", channel);
}

/* Disable DMA channel
 */
void disable_dma (dmach_t channel)
{
	if (dma_chan[channel].lock) {
		if (dma_chan[channel].active == 1) {
			dma_chan[channel].active = 0;
			arch_disable_dma(channel, &dma_chan[channel]);
		}
	} else
		printk (KERN_ERR "Trying to disable free DMA%d\n", channel);
}

int get_dma_residue(dmach_t channel)
{
	return arch_get_dma_residue(channel, &dma_chan[channel]);
}

EXPORT_SYMBOL(dma_str);
EXPORT_SYMBOL(enable_dma);
EXPORT_SYMBOL(disable_dma);
EXPORT_SYMBOL(set_dma_addr);
EXPORT_SYMBOL(set_dma_count);
EXPORT_SYMBOL(set_dma_mode);
EXPORT_SYMBOL(get_dma_residue);
EXPORT_SYMBOL(set_dma_sg);

__initfunc(void init_dma(void))
{
	arch_dma_init(dma_chan);
}

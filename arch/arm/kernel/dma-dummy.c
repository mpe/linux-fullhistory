/*
 * arch/arm/kernel/dma-dummy.c
 *
 * Copyright (C) 1998 Philip Blundell
 * Copyright (c) 1998 Russell King
 *
 * Dummy DMA functions
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/spinlock.h>

spinlock_t dma_spin_lock = SPIN_LOCK_UNLOCKED;

int request_dma(int channel, const char *device_id)
{
	return -EINVAL;
}

int no_dma(void)
{
	return 0;
}

#define GLOBAL_ALIAS(_a,_b) asm (".set " #_a "," #_b "; .globl " #_a)
GLOBAL_ALIAS(disable_dma, no_dma);
GLOBAL_ALIAS(enable_dma, no_dma);
GLOBAL_ALIAS(free_dma, no_dma);
GLOBAL_ALIAS(get_dma_residue, no_dma);
GLOBAL_ALIAS(get_dma_list, no_dma);
GLOBAL_ALIAS(set_dma_mode, no_dma);
GLOBAL_ALIAS(set_dma_count, no_dma);
GLOBAL_ALIAS(set_dma_addr, no_dma);
GLOBAL_ALIAS(init_dma, no_dma);

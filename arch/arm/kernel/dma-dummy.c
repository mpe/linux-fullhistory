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

void free_dma(int channel)
{
}

int get_dma_list(char *buf)
{
	return 0;
}

void __init init_dma(void)
{
}

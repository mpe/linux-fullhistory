/*
 * Tiny Tyne DMA buffer allocator
 *
 * Copyright (C) 1995 Ralf Baechle
 */
#include <linux/autoconf.h>
#include <linux/types.h>
#include <asm/bootinfo.h>

#ifdef CONFIG_DESKSTATION_TYNE

static unsigned long allocated;

/*
 * Not very sophisticated, but should suffice for now...
 */
unsigned long deskstation_tyne_dma_alloc(size_t size)
{
	unsigned long ret = allocated;
	allocated += size;
	if (allocated > boot_info.dma_cache_size)
		ret = -1;
	return ret;
}

void deskstation_tyne_dma_init(void)
{
	if (boot_info.machtype != MACH_DESKSTATION_TYNE)
		return;
	allocated = 0;
	printk ("Deskstation Tyne DMA (%luk) buffer initialized.\n",
	        boot_info.dma_cache_size >> 10);
}

#endif /* CONFIG_DESKSTATION_TYNE */

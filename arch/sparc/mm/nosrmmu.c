/* $Id: nosrmmu.c,v 1.2 1999/03/30 10:17:39 jj Exp $
 * nosrmmu.c: This file is a bunch of dummies for sun4 compiles, 
 *         so that it does not need srmmu and avoid ifdefs.
 *
 * Copyright (C) 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <asm/mbus.h>

static char shouldnothappen[] __initdata = "SUN4 kernel can only run on SUN4\n";

enum mbus_module srmmu_modtype;

static void __init should_not_happen(void)
{
	prom_printf(shouldnothappen);
	prom_halt();
}

void __init srmmu_frob_mem_map(unsigned long start_mem)
{
	should_not_happen();
}

unsigned long __init srmmu_paging_init(unsigned long start_mem, unsigned long end_mem)
{
	should_not_happen();
	return 0;
}

void __init ld_mmu_srmmu(void)
{
	should_not_happen();
}

void srmmu_mapioaddr(unsigned long physaddr, unsigned long virt_addr, int bus_type, int rdonly)
{
}

void srmmu_unmapioaddr(unsigned long virt_addr)
{
}

void __init srmmu_end_memory(unsigned long memory_size, unsigned long *mem_end_p)
{
	return 0;
}

__u32 iounit_map_dma_init(struct linux_sbus *sbus, int size)
{
	return 0;
}

__u32 iounit_map_dma_page(__u32 vaddr, void *addr, struct linux_sbus *sbus)
{
	return 0;
}

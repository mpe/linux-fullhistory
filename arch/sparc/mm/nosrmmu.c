/* $Id: nosrmmu.c,v 1.1 1998/03/09 14:04:15 jj Exp $
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

__initfunc(static void should_not_happen(void))
{
	prom_printf(shouldnothappen);
	prom_halt();
}

__initfunc(void srmmu_frob_mem_map(unsigned long start_mem))
{
	should_not_happen();
}

__initfunc(unsigned long srmmu_paging_init(unsigned long start_mem, unsigned long end_mem))
{
	should_not_happen();
	return 0;
}

__initfunc(void ld_mmu_srmmu(void))
{
	should_not_happen();
}

void srmmu_mapioaddr(unsigned long physaddr, unsigned long virt_addr, int bus_type, int rdonly)
{
}

void srmmu_unmapioaddr(unsigned long virt_addr)
{
}

__initfunc(void srmmu_end_memory(unsigned long memory_size, unsigned long *mem_end_p))
{
	return 0;
}

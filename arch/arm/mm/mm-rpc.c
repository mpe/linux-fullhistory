/*
 * arch/arm/mm/mm-rpc.c
 *
 * Extra MM routines for RiscPC architecture
 *
 * Copyright (C) 1998-1999 Russell King
 */
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/page.h>
#include <asm/proc/domain.h>
#include <asm/setup.h>

#include "map.h"

#define SIZE(x) (sizeof(x) / sizeof(x[0]))

struct mem_desc mem_desc[] __initdata = {
	{ 0xc0000000, 0xc0000000 },
	{ 0xc4000000, 0xc4000000 },
	{ 0xc8000000, 0xc8000000 },
	{ 0xcc000000, 0xcc000000 }
};

unsigned int __initdata mem_desc_size = SIZE(mem_desc);

void __init
init_dram_banks(struct param_struct *params)
{
	unsigned int bank;

	for (bank = 0; bank < mem_desc_size; bank++)
		mem_desc[bank].virt_end += PAGE_SIZE *
				  params->u1.s.pages_in_bank[bank];

	params->u1.s.nr_pages = mem_desc[3].virt_end - PAGE_OFFSET;
	params->u1.s.nr_pages /= PAGE_SIZE;
}

struct map_desc io_desc[] __initdata = {
	/* VRAM		*/
	{ SCREEN2_BASE,	SCREEN_START,	2*1048576, DOMAIN_IO, 0, 1, 0, 0 },
	/* IO space	*/
	{ IO_BASE,	IO_START,	IO_SIZE	 , DOMAIN_IO, 0, 1, 0, 0 },
	/* EASI space	*/
	{ EASI_BASE,	EASI_START,	EASI_SIZE, DOMAIN_IO, 0, 1, 0, 0 }
};

unsigned int __initdata io_desc_size = SIZE(io_desc);

/*
 * arch/arm/mm/mm-nexuspci.c
 *  from arch/arm/mm/mm-ebsa110.c
 *
 * Extra MM routines for the NexusPCI architecture
 *
 * Copyright (C) 1998 Russell King
 */

#include <asm/io.h>
 
/* map in IO */
void setup_io_pagetables(void)
{
	unsigned long address = IO_START;
	int spi = IO_BASE >> PGDIR_SHIFT;

	pgd_val(swapper_pg_dir[spi-1]) = 0xc0000000 | PMD_TYPE_SECT |
					 PMD_DOMAIN(DOMAIN_KERNEL) | PMD_SECT_AP_WRITE;

	while (address < IO_START + IO_SIZE && address) {
		pgd_val(swapper_pg_dir[spi++]) = address | PMD_TYPE_SECT |
						PMD_DOMAIN(DOMAIN_IO) |
						PMD_SECT_AP_WRITE;
		address += PGDIR_SIZE;
	}
}


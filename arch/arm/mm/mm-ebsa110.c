/*
 * arch/arm/mm/mm-ebsa110.c
 *
 * Extra MM routines for the EBSA-110 architecture
 *
 * Copyright (C) 1998-1999 Russell King
 */
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/pgtable.h>
#include <asm/page.h>

#include "map.h"
 
#define SIZE(x) (sizeof(x) / sizeof(x[0]))

struct map_desc io_desc[] __initdata = {
	{ IO_BASE - PGDIR_SIZE, 0xc0000000, PGDIR_SIZE, DOMAIN_IO, 0, 1, 0, 0 },
	{ IO_BASE             , IO_START  , IO_SIZE   , DOMAIN_IO, 0, 1, 0, 0 }
};

unsigned int __initdata io_desc_size = SIZE(io_desc);

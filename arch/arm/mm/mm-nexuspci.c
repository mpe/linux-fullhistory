/*
 * arch/arm/mm/mm-nexuspci.c
 *  from arch/arm/mm/mm-ebsa110.c
 *
 * Extra MM routines for the FTV/PCI architecture
 *
 * Copyright (C) 1998-1999 Phil Blundell
 * Copyright (C) 1998-1999 Russell King
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>
 
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/io.h>

#include "map.h"
 
struct map_desc io_desc[] __initdata = {
 	{ INTCONT_BASE,	INTCONT_START,	0x00001000, DOMAIN_IO, 0, 1, 0, 0 },
 	{ PLX_BASE,	PLX_START,	0x00001000, DOMAIN_IO, 0, 1, 0, 0 },
 	{ PCIO_BASE,	PLX_IO_START,	0x00100000, DOMAIN_IO, 0, 1, 0, 0 },
 	{ DUART_BASE,	DUART_START,	0x00001000, DOMAIN_IO, 0, 1, 0, 0 },
	{ STATUS_BASE,	STATUS_START,	0x00001000, DOMAIN_IO, 0, 1, 0, 0 }
};

#define SIZE(x) (sizeof(x) / sizeof(x[0]))

unsigned int __initdata io_desc_size = SIZE(io_desc);

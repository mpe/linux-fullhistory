/*
 * arch/arm/mm/mm-lusl7200.c
 *
 * Extra MM routines for LUSL7200 architecture
 *
 * Copyright (C) 2000 Steven J. Hill
 */

#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/page.h>
#include <asm/proc/domain.h>
#include <asm/setup.h>

#include "map.h"

#define SIZE(x) (sizeof(x) / sizeof(x[0]))

struct map_desc io_desc[] __initdata = {
	{ IO_BASE,	IO_START,	IO_SIZE,	DOMAIN_IO, 0, 1 ,0 ,0},
	{ IO_BASE_2,	IO_START_2,	IO_SIZE_2,	DOMAIN_IO, 0, 1 ,0 ,0},
};

unsigned int __initdata io_desc_size = SIZE(io_desc);

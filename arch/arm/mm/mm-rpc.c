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

struct map_desc io_desc[] __initdata = {
	/* VRAM		*/
	{ SCREEN_BASE,	SCREEN_START,	2*1048576, DOMAIN_IO, 0, 1, 0, 0 },
	/* IO space	*/
	{ IO_BASE,	IO_START,	IO_SIZE	 , DOMAIN_IO, 0, 1, 0, 0 },
	/* EASI space	*/
	{ EASI_BASE,	EASI_START,	EASI_SIZE, DOMAIN_IO, 0, 1, 0, 0 }
};

unsigned int __initdata io_desc_size = SIZE(io_desc);

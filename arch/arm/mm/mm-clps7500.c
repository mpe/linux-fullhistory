/*
 * arch/arm/mm/mm-cl7500.c
 *
 * Extra MM routines for CL7500 architecture
 *
 * Copyright (C) 1998 Russell King
 * Copyright (C) 1999 Nexus Electronics Ltd
 */

#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/page.h>
#include <asm/proc/domain.h>
#include <asm/setup.h>

#include "map.h"

#define SIZE(x) (sizeof(x) / sizeof(x[0]))

struct map_desc io_desc[] __initdata = {
	{ IO_BASE,	IO_START,	IO_SIZE	 , DOMAIN_IO, 0, 1 },	/* IO space	*/
	{ ISA_BASE,	ISA_START,	ISA_SIZE , DOMAIN_IO, 0, 1 },	/* ISA space	*/
	{ FLASH_BASE,	FLASH_START,	FLASH_SIZE, DOMAIN_IO, 0, 1 },	/* Flash	*/
	{ LED_BASE,	LED_START,	LED_SIZE , DOMAIN_IO, 0, 1 }	/* LED		*/
};

unsigned int __initdata io_desc_size = SIZE(io_desc);

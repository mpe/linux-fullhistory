/*
 * arch/arm/mm/mm-sa1100.c
 *
 * Extra MM routines for the SA1100 architecture
 *
 * Copyright (C) 1998-1999 Russell King
 * Copyright (C) 1999 Hugo Fiennes
 *
 * 1999/12/04 Nicolas Pitre <nico@cam.org>
 *	Converted memory definition for struct meminfo initialisations.
 *	Memory is listed physically now.
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/page.h>

#include "map.h"
 
#define SIZE(x) (sizeof(x) / sizeof(x[0]))


/*
 * These are the RAM memory mappings for SA1100 implementations.
 * Note that LART is a special case - it doesn't use physical 
 * address line A23 on the DRAM, so we effectively have 4 * 8MB
 * in two banks.
 */
struct mem_desc { 
	unsigned long phys_start;
	unsigned long length;
} mem_desc[] __initdata = {
#if defined(CONFIG_SA1100_BRUTUS)
	{ 0xc0000000, 0x00400000 },	/* 4MB */
	{ 0xc8000000, 0x00400000 },	/* 4MB */
#if 0	/* only two banks until the bootmem stuff is fixed... */
	{ 0xd0000000, 0x00400000 },	/* 4MB */
	{ 0xd8000000, 0x00400000 }	/* 4MB */
#endif
#elif defined(CONFIG_SA1100_EMPEG)
	{ 0xc0000000, 0x00400000 },	/* 4MB */
	{ 0xc8000000, 0x00400000 }	/* 4MB */
#elif defined(CONFIG_SA1100_LART)
	{ 0xc0000000, 0x00800000 },	/* 8MB */
	{ 0xc1000000, 0x00800000 },	/* 8MB */
	{ 0xc8000000, 0x00800000 },	/* 8MB */
	{ 0xc9000000, 0x00800000 }	/* 8MB */
#elif defined(CONFIG_SA1100_VICTOR)
	{ 0xc0000000, 0x00400000 }	/* 4MB */
#elif defined(CONFIG_SA1100_TIFON)
	{ 0xc0000000, 0x01000000 },	/* 16MB */
	{ 0xc8000000, 0x01000000 }	/* 16MB */
#else
#error missing memory configuration
#endif
};

unsigned int __initdata mem_desc_size = SIZE(mem_desc);


struct map_desc io_desc[] __initdata = {
	/* virtual           physical     length     domain    r  w  c  b */
#if defined(CONFIG_SA1100_VICTOR)
	{ 0xd0000000,      0x00000000,  0x00200000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash */
#elif defined(CONFIG_SA1100_EMPEG)
	{ EMPEG_FLASHBASE, 0x00000000,  0x00200000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash */
#elif defined(CONFIG_SA1100_TIFON)
	{ 0xd0000000,      0x00000000,  0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 1 */
	{ 0xd0800000,      0x08000000,  0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 2 */
#endif
#ifdef CONFIG_SA1101
	{ 0xdc000000,      SA1101_BASE, 0x00400000, DOMAIN_IO, 1, 1, 0, 0 }, /* SA1101 */
#endif
	{ 0xe0000000,      0x20000000,  0x04000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCMCIA0 IO */
	{ 0xe4000000,      0x30000000,  0x04000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCMCIA1 IO */
	{ 0xe8000000,      0x28000000,  0x04000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCMCIA0 attr */
	{ 0xec000000,      0x38000000,  0x04000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCMCIA1 attr */
	{ 0xf0000000,      0x2c000000,  0x04000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCMCIA0 mem */
	{ 0xf4000000,      0x3c000000,  0x04000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCMCIA1 mem */
	{ 0xf8000000,      0x80000000,  0x02000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCM */
	{ 0xfa000000,      0x90000000,  0x02000000, DOMAIN_IO, 0, 1, 0, 0 }, /* SCM */
	{ 0xfc000000,      0xa0000000,  0x02000000, DOMAIN_IO, 0, 1, 0, 0 }, /* MER */
	{ 0xfe000000,      0xb0000000,  0x02000000, DOMAIN_IO, 0, 1, 0, 0 }  /* LCD + DMA */
};

unsigned int __initdata io_desc_size = SIZE(io_desc);

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
 *
 * 2000/04/07 Nicolas Pitre <nico@cam.org>
 *	Reworked for real-time selection of memory definitions
 *
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/page.h>

#include "map.h"
 
#define SIZE(x) (sizeof(x) / sizeof(x[0]))


#define SA1100_STD_IO_MAPPING \
 /* virtual     physical    length      domain     r  w  c  b */ \
  { 0xe0000000, 0x20000000, 0x04000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCMCIA0 IO */ \
  { 0xe4000000, 0x30000000, 0x04000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCMCIA1 IO */ \
  { 0xe8000000, 0x28000000, 0x04000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCMCIA0 attr */ \
  { 0xec000000, 0x38000000, 0x04000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCMCIA1 attr */ \
  { 0xf0000000, 0x2c000000, 0x04000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCMCIA0 mem */ \
  { 0xf4000000, 0x3c000000, 0x04000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCMCIA1 mem */ \
  { 0xf8000000, 0x80000000, 0x02000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCM */ \
  { 0xfa000000, 0x90000000, 0x02000000, DOMAIN_IO, 0, 1, 0, 0 }, /* SCM */ \
  { 0xfc000000, 0xa0000000, 0x02000000, DOMAIN_IO, 0, 1, 0, 0 }, /* MER */ \
  { 0xfe000000, 0xb0000000, 0x02000000, DOMAIN_IO, 0, 1, 0, 0 }  /* LCD + DMA */


static struct map_desc assabet_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_ASSABET
  { 0xd0000000, 0x00000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 */
  { 0xdc000000, 0x12000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* Board Control Register */
  SA1100_STD_IO_MAPPING
#endif
};

static struct map_desc bitsy_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_BITSY
  { 0xd0000000, 0x00000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 */
  SA1100_STD_IO_MAPPING
#endif
};

static struct map_desc empeg_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_EMPEG
  { EMPEG_FLASHBASE, 0x00000000, 0x00200000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash */
  SA1100_STD_IO_MAPPING
#endif
};

static struct map_desc thinclient_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_THINCLIENT
#if 1
  /* ThinClient: only one of those... */
//  { 0xd0000000, 0x00000000, 0x01000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 when JP1 2-4 */
  { 0xd0000000, 0x08000000, 0x01000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 1 when JP1 3-4 */
#else
  /* GraphicsClient: */
  { 0xd0000000, 0x08000000, 0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 1 */
  { 0xd0800000, 0x18000000, 0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 3 */
#endif
  { 0xdc000000, 0x10000000, 0x00400000, DOMAIN_IO, 0, 1, 0, 0 }, /* CPLD */
  SA1100_STD_IO_MAPPING
#endif
};

static struct map_desc tifon_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_TIFON
  { 0xd0000000, 0x00000000, 0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 1 */
  { 0xd0800000, 0x08000000, 0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 2 */
  SA1100_STD_IO_MAPPING
#endif
};

static struct map_desc victor_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_VICTOR
  { 0xd0000000, 0x00000000, 0x00200000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash */
  SA1100_STD_IO_MAPPING
#endif
};


static struct map_desc default_io_desc[] __initdata = {
  SA1100_STD_IO_MAPPING
};


/*
 * Here it would be wiser to simply assign a pointer to the appropriate 
 * list, but io_desc is already declared as an array in "map.h".
 */
struct map_desc io_desc[20] __initdata = { { 0, }, };
unsigned int io_desc_size;

void __init select_sa1100_io_desc(void)
{
	if( machine_is_assabet() ) {
		memcpy( io_desc, assabet_io_desc, sizeof(assabet_io_desc) );
		io_desc_size = SIZE(assabet_io_desc);
	} else if( machine_is_bitsy() ) {
		memcpy( io_desc, bitsy_io_desc, sizeof(bitsy_io_desc) );
		io_desc_size = SIZE(bitsy_io_desc);
	} else if( machine_is_empeg() ) {
		memcpy( io_desc, empeg_io_desc, sizeof(empeg_io_desc) );
		io_desc_size = SIZE(empeg_io_desc);
	} else if( machine_is_thinclient() ) {
		memcpy( io_desc, thinclient_io_desc, sizeof(thinclient_io_desc) );
		io_desc_size = SIZE(thinclient_io_desc);
	} else if( machine_is_tifon() ) {
		memcpy( io_desc, tifon_io_desc, sizeof(tifon_io_desc) );
		io_desc_size = SIZE(tifon_io_desc);
	} else if( machine_is_victor() ) {
		memcpy( io_desc, victor_io_desc, sizeof(victor_io_desc) );
		io_desc_size = SIZE(victor_io_desc);
	} else {
		memcpy( io_desc, default_io_desc, sizeof(default_io_desc) );
		io_desc_size = SIZE(default_io_desc);
	}
}


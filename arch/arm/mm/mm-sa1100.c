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
 *	Reworked for run-time selection of memory definitions
 *
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/hardware.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/mach-types.h>

#include "map.h"
 
#define SIZE(x) (sizeof(x) / sizeof(x[0]))


#define SA1100_STD_IO_MAPPING \
 /* virtual     physical    length      domain     r  w  c  b */ \
  { 0xe0000000, 0x20000000, 0x04000000, DOMAIN_IO, 1, 1, 0, 0 }, /* PCMCIA0 IO */ \
  { 0xe4000000, 0x30000000, 0x04000000, DOMAIN_IO, 1, 1, 0, 0 }, /* PCMCIA1 IO */ \
  { 0xe8000000, 0x28000000, 0x04000000, DOMAIN_IO, 1, 1, 0, 0 }, /* PCMCIA0 attr */ \
  { 0xec000000, 0x38000000, 0x04000000, DOMAIN_IO, 1, 1, 0, 0 }, /* PCMCIA1 attr */ \
  { 0xf0000000, 0x2c000000, 0x04000000, DOMAIN_IO, 1, 1, 0, 0 }, /* PCMCIA0 mem */ \
  { 0xf4000000, 0x3c000000, 0x04000000, DOMAIN_IO, 1, 1, 0, 0 }, /* PCMCIA1 mem */ \
  { 0xf8000000, 0x80000000, 0x02000000, DOMAIN_IO, 0, 1, 0, 0 }, /* PCM */ \
  { 0xfa000000, 0x90000000, 0x02000000, DOMAIN_IO, 0, 1, 0, 0 }, /* SCM */ \
  { 0xfc000000, 0xa0000000, 0x02000000, DOMAIN_IO, 0, 1, 0, 0 }, /* MER */ \
  { 0xfe000000, 0xb0000000, 0x02000000, DOMAIN_IO, 0, 1, 0, 0 }  /* LCD + DMA */


static struct map_desc assabet_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_ASSABET
  { 0xd0000000, 0x00000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 */
  { 0xd4000000, 0x10000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* System Registers */
  { 0xdc000000, 0x12000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* Board Control Register */
  { 0xd8000000, 0x40000000, 0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* SA-1111 */
  SA1100_STD_IO_MAPPING
#endif
};

static struct map_desc nanoengine_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_NANOENGINE
  { 0xd0000000, 0x00000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 */
  { 0xd4000000, 0x10000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* System Registers */
  { 0xdc000000, 0x18A00000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* Internal PCI Config Space */
  SA1100_STD_IO_MAPPING
#endif
};

static struct map_desc bitsy_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_BITSY
  { 0xd0000000, 0x00000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 */
  { 0xdc000000, 0x49000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* EGPIO 0 */
  SA1100_STD_IO_MAPPING
#endif
};

static struct map_desc cerf_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_CERF
  { 0xd8000000, 0x08000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* Crystal Chip */
  { 0xd0000000, 0x00000000, 0x01000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 */
  SA1100_STD_IO_MAPPING
#endif
};

static struct map_desc empeg_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_EMPEG
  { EMPEG_FLASHBASE, 0x00000000, 0x00200000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash */
  SA1100_STD_IO_MAPPING
#endif
};

static struct map_desc graphicsclient_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_GRAPHICSCLIENT
  { 0xd0000000, 0x08000000, 0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 1 */
  { 0xd0800000, 0x18000000, 0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 3 */
  { 0xdc000000, 0x10000000, 0x00400000, DOMAIN_IO, 0, 1, 0, 0 }, /* CPLD */
  SA1100_STD_IO_MAPPING
#endif
};

static struct map_desc lart_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_LART
  { 0xd0000000, 0x00000000, 0x00400000, DOMAIN_IO, 1, 1, 0, 0 }, /* main flash memory */
  { 0xd8000000, 0x08000000, 0x00400000, DOMAIN_IO, 1, 1, 0, 0 }, /* main flash, alternative location */
  SA1100_STD_IO_MAPPING
#endif
};

static struct map_desc thinclient_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_THINCLIENT
#if 0
  { 0xd0000000, 0x00000000, 0x01000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 when JP1 2-4 */
#else
  { 0xd0000000, 0x08000000, 0x01000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 1 when JP1 3-4 */
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

static struct map_desc xp860_io_desc[] __initdata = {
#ifdef CONFIG_SA1100_XP860
  { 0xd8000000, 0x40000000, 0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* SA-1111 */
  { 0xda000000, 0x10000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* SCSI */
  { 0xdc000000, 0x18000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* LAN */
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
struct map_desc io_desc[20] __initdata = {};
unsigned int io_desc_size;

void __init select_sa1100_io_desc(void)
{
	if( machine_is_assabet() ) {
		memcpy( io_desc, assabet_io_desc, sizeof(assabet_io_desc) );
		io_desc_size = SIZE(assabet_io_desc);
	} else if( machine_is_nanoengine() ) {
		memcpy( io_desc, nanoengine_io_desc, sizeof(nanoengine_io_desc) );
		io_desc_size = SIZE(nanoengine_io_desc);
	} else if( machine_is_bitsy() ) {
		memcpy( io_desc, bitsy_io_desc, sizeof(bitsy_io_desc) );
		io_desc_size = SIZE(bitsy_io_desc);
	} else if( machine_is_cerf() ) {
		memcpy( io_desc, cerf_io_desc, sizeof(cerf_io_desc) );
		io_desc_size = SIZE(cerf_io_desc);
	} else if( machine_is_empeg() ) {
		memcpy( io_desc, empeg_io_desc, sizeof(empeg_io_desc) );
		io_desc_size = SIZE(empeg_io_desc);
	} else if( machine_is_graphicsclient() ) {
		memcpy( io_desc, graphicsclient_io_desc, sizeof(graphicsclient_io_desc) );
		io_desc_size = SIZE(graphicsclient_io_desc);
	} else if( machine_is_lart() ) {
	        memcpy( io_desc, lart_io_desc, sizeof(lart_io_desc) );
		io_desc_size = SIZE(lart_io_desc);
	} else if( machine_is_thinclient() ) {
		memcpy( io_desc, thinclient_io_desc, sizeof(thinclient_io_desc) );
		io_desc_size = SIZE(thinclient_io_desc);
	} else if( machine_is_tifon() ) {
		memcpy( io_desc, tifon_io_desc, sizeof(tifon_io_desc) );
		io_desc_size = SIZE(tifon_io_desc);
	} else if( machine_is_victor() ) {
		memcpy( io_desc, victor_io_desc, sizeof(victor_io_desc) );
		io_desc_size = SIZE(victor_io_desc);
	} else if( machine_is_xp860() ) {
		memcpy( io_desc, xp860_io_desc, sizeof(xp860_io_desc) );
		io_desc_size = SIZE(xp860_io_desc);
	} else {
		memcpy( io_desc, default_io_desc, sizeof(default_io_desc) );
		io_desc_size = SIZE(default_io_desc);
	}
}


#ifdef CONFIG_DISCONTIGMEM

/*
 * Our node_data structure for discontigous memory.
 * There is 4 possible nodes i.e. the 4 SA1100 RAM banks.
 */

static bootmem_data_t node_bootmem_data[4];

pg_data_t sa1100_node_data[4] =
{ { bdata: &node_bootmem_data[0] },
  { bdata: &node_bootmem_data[1] },
  { bdata: &node_bootmem_data[2] },
  { bdata: &node_bootmem_data[3] } };

#endif

  
/*
 * On Assabet, we must probe for the Neponset board *before* paging_init() 
 * has occured to actually determine the amount of RAM available.  To do so, 
 * we map the appropriate IO section in the page table here in order to 
 * access GPIO registers.
 */
void __init map_sa1100_gpio_regs( void )
{
	unsigned long phys = _GPLR & PMD_MASK;
	unsigned long virt = io_p2v(phys);
	int prot = PMD_TYPE_SECT | PMD_SECT_AP_WRITE | PMD_DOMAIN(DOMAIN_IO);
	pmd_t pmd;
	pmd_val(pmd) = phys | prot;
	set_pmd(pmd_offset(pgd_offset_k(virt), virt), pmd);
}


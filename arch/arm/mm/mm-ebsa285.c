/*
 * arch/arm/mm/mm-ebsa285.c
 *
 * Extra MM routines for the EBSA285 architecture
 *
 * Copyright (C) 1998 Russell King
 */
#include <linux/sched.h>
#include <linux/mm.h>
 
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/proc/mm-init.h>
 
/*    Logical    Physical
 * 0xfff00000	0x40000000	X-Bus
 * 0xffe00000	0x7c000000	PCI I/O space
 * 0xfe000000	0x42000000	CSR
 * 0xfd000000	0x78000000	Outbound write flush
 * 0xfc000000	0x79000000	PCI IACK/special space
 * 0xf9000000	0x7a000000	PCI Config type 1
 * 0xf8000000	0x7b000000	PCI Config type 0
 */

static struct mapping {
	unsigned long virtual;
	unsigned long physical;
	unsigned long length;
} io_mapping[] = {
	/*
	 * This is to allow us to fiddle with the EEPROM
	 *  This entry will go away in time
	 */
	{ 0xd8000000, 0x41000000, 0x00400000 },

	/*
	 * These ones are so that we can fiddle
	 *  with the various cards (eg VGA)
	 *  until we're happy with them...
	 */
	{ 0xdc000000, 0x7c000000, 0x00100000 },
	{ 0xe0000000, 0x80000000, 0x10000000 },

	{ 0xf8000000, 0x7b000000, 0x01000000 },	/* Type 0 Config */

	{ 0xf9000000, 0x7a000000, 0x01000000 },	/* Type 1 Config */

	{ 0xfc000000, 0x79000000, 0x01000000 },	/* PCI IACK	 */
	{ 0xfd000000, 0x78000000, 0x01000000 },	/* Outbound wflsh*/
	{ 0xfe000000, 0x42000000, 0x01000000 },	/* CSR		 */
	{ 0xffe00000, 0x7c000000, 0x00100000 },	/* PCI I/O	 */
	{ 0xfff00000, 0x40000000, 0x00100000 },	/* X-Bus	 */
};

#define SIZEOFIO (sizeof(io_mapping) / sizeof(io_mapping[0]))

/* map in IO */
unsigned long setup_io_pagetables(unsigned long start_mem)
{
	struct mapping *mp;
	int i;

	for (i = 0, mp = io_mapping; i < SIZEOFIO; i++, mp++) {
		while ((mp->virtual & 1048575 || mp->physical & 1048575) && mp->length >= PAGE_SIZE) {
			alloc_init_page(&start_mem, mp->virtual, mp->physical, DOMAIN_IO,
					PTE_AP_WRITE);

			mp->length -= PAGE_SIZE;
			mp->virtual += PAGE_SIZE;
			mp->physical += PAGE_SIZE;
		}

		while (mp->length >= 1048576) {
if (mp->virtual > 0xf0000000)
			alloc_init_section(&start_mem, mp->virtual, mp->physical, DOMAIN_IO,
					   PMD_SECT_AP_WRITE);
else
alloc_init_section(&start_mem, mp->virtual, mp->physical, DOMAIN_USER, PMD_SECT_AP_WRITE | PMD_SECT_AP_READ);

			mp->length -= 1048576;
			mp->virtual += 1048576;
			mp->physical += 1048576;
		}

		while (mp->length >= PAGE_SIZE) {
			alloc_init_page(&start_mem, mp->virtual, mp->physical, DOMAIN_IO,
					PTE_AP_WRITE);

			mp->length -= PAGE_SIZE;
			mp->virtual += PAGE_SIZE;
			mp->physical += PAGE_SIZE;
		}
	}
	return start_mem;
}


/*
 * arch/arm/mm/mm-ebsa285.c
 *
 * Extra MM routines for the EBSA285 architecture
 *
 * Copyright (C) 1998 Russell King, Dave Gilbert.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>
 
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/proc/mm-init.h>

/*
 * These two functions convert PCI bus addresses to virtual addresses
 * and back again.
 */ 
unsigned long __virt_to_bus(unsigned long res)
{
	if (res < PAGE_OFFSET || res >= 0xD0000000) {
		printk("__virt_to_bus: invalid address 0x%08lx\n", res);
#ifdef CONFIG_DEBUG_ERRORS
		__backtrace();
#endif
	} else
		res = (res - PAGE_OFFSET) + 0x10000000;

	return res;
}

unsigned long __bus_to_virt(unsigned long res)
{
	if (res < 0x10000000 || res >= 0x20000000) {
		printk("__bus_to_virt: invalid address 0x%08lx\n", res);
#ifdef CONFIG_DEBUG_ERRORS
		__backtrace();
#endif
	} else
		res = (res - 0x10000000) + PAGE_OFFSET;

	return res;
}

/*    Logical    Physical
 * 0xfff00000	0x40000000	X-Bus
 * 0xffe00000	0x7c000000	PCI I/O space
 * 0xfe000000	0x42000000	CSR
 * 0xfd000000	0x78000000	Outbound write flush
 * 0xfc000000	0x79000000	PCI IACK/special space
 * 0xf9000000	0x7a000000	PCI Config type 1
 * 0xf8000000	0x7b000000	PCI Config type 0
 */

/*
 * This is to allow us to fiddle with the EEPROM
 *  This entry will go away in time, once the fmu
 *  can mmap() the flash.
 *
 * These ones are so that we can fiddle
 *  with the various cards (eg VGA)
 *  until we're happy with them...
 */
#define MAPPING \
	{ 0xd8000000, 0x41000000, 0x00400000, DOMAIN_USER, 1, 1 },	/* EEPROM */	    \
	{ 0xdc000000, 0x7c000000, 0x00100000, DOMAIN_USER, 1, 1 },	/* VGA */	    \
	{ 0xe0000000, 0x80000000, 0x10000000, DOMAIN_USER, 1, 1 },	/* VGA */	    \
	{ 0xf8000000, 0x7b000000, 0x01000000, DOMAIN_IO  , 0, 1 },	/* Type 0 Config */ \
	{ 0xf9000000, 0x7a000000, 0x01000000, DOMAIN_IO  , 0, 1 },	/* Type 1 Config */ \
	{ 0xfc000000, 0x79000000, 0x01000000, DOMAIN_IO  , 0, 1 },	/* PCI IACK	 */ \
	{ 0xfd000000, 0x78000000, 0x01000000, DOMAIN_IO  , 0, 1 },	/* Outbound wflsh*/ \
	{ 0xfe000000, 0x42000000, 0x01000000, DOMAIN_IO  , 0, 1 },	/* CSR		 */ \
	{ 0xffe00000, 0x7c000000, 0x00100000, DOMAIN_IO  , 0, 1 },	/* PCI I/O	 */ \
	{ 0xfff00000, 0x40000000, 0x00100000, DOMAIN_IO  , 0, 1 },	/* X-Bus	 */

#include "mm-armv.c"

/*
 * arch/arm/mm/mm-ebsa285.c
 *
 * Extra MM routines for the EBSA285 architecture
 *
 * Copyright (C) 1998 Russell King, Dave Gilbert.
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>
 
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/proc/mm-init.h>
#include <asm/dec21285.h>

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
  { 0xd8000000, DC21285_FLASH,		      0x00400000, DOMAIN_USER, 1, 1 },	/* EEPROM */	    \
  { 0xdc000000, 0x7c000000,		      0x00100000, DOMAIN_USER, 1, 1 },	/* VGA */	    \
  { 0xe0000000, DC21285_PCI_MEM,	      0x18000000, DOMAIN_USER, 1, 1 },	/* VGA */	    \
  { 0xf8000000, DC21285_PCI_TYPE_0_CONFIG,    0x01000000, DOMAIN_IO  , 0, 1 },	/* Type 0 Config */ \
  { 0xf9000000, DC21285_PCI_TYPE_1_CONFIG,    0x01000000, DOMAIN_IO  , 0, 1 },	/* Type 1 Config */ \
  { PCI_IACK,	DC21285_PCI_IACK,	      0x01000000, DOMAIN_IO  , 0, 1 },	/* PCI IACK	 */ \
  { 0xfd000000, DC21285_OUTBOUND_WRITE_FLUSH, 0x01000000, DOMAIN_IO  , 0, 1 },	/* Out wrflsh	 */ \
  { 0xfe000000, DC21285_ARMCSR_BASE,	      0x01000000, DOMAIN_IO  , 0, 1 },	/* CSR		 */ \
  { 0xffe00000, DC21285_PCI_IO, 	      0x00100000, DOMAIN_IO  , 0, 1 },	/* PCI I/O	 */ \
  { 0xfff00000, 0x40000000,		      0x00100000, DOMAIN_IO  , 0, 1 },	/* X-Bus	 */

#include "mm-armv.c"

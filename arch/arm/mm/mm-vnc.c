/*
 * arch/arm/mm/mm-vnc.c
 *
 * Extra MM routines for the Corel VNC architecture
 *
 * Copyright (C) 1998 Russell King
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>
 
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/proc/mm-init.h>
#include <asm/dec21285.h>

/* Table describing the MMU translation mapping
 * mainly used to set up the I/O mappings.
 */ 
#define MAPPING \
  { 0xd0000000, DC21285_FLASH,		      0x00800000, DOMAIN_IO  , 0, 1 },	/* Flash	 */ \
  { 0xe0000000, DC21285_PCI_MEM,	      0x18000000, DOMAIN_IO  , 0, 1 },	/* PCI Mem 	 */ \
  { 0xf8000000, DC21285_PCI_TYPE_0_CONFIG,    0x01000000, DOMAIN_IO  , 0, 1 },	/* Type 0 Config */ \
  { 0xf9000000, DC21285_PCI_TYPE_1_CONFIG,    0x01000000, DOMAIN_IO  , 0, 1 },	/* Type 1 Config */ \
  { PCI_IACK,	DC21285_PCI_IACK,	      0x01000000, DOMAIN_IO  , 0, 1 },	/* PCI IACK	 */ \
  { 0xfd000000, DC21285_OUTBOUND_WRITE_FLUSH, 0x01000000, DOMAIN_IO  , 0, 1 },	/* Out wrflsh	 */ \
  { 0xfe000000, DC21285_ARMCSR_BASE,	      0x01000000, DOMAIN_IO  , 0, 1 },	/* CSR		 */ \
  { 0xffe00000, DC21285_PCI_IO, 	      0x00100000, DOMAIN_IO  , 0, 1 },	/* PCI I/O	 */ \

#include "mm-armv.c"

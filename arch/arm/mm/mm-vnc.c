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

/* Table describing the MMU translation mapping
 * mainly used to set up the I/O mappings.
 */ 
#define MAPPING \
  { 0xe0000000, DC21285_PCI_IO,		      0x00100000, DOMAIN_IO, 0, 1 }, /* PCI I/O		*/ \
  { 0xe0100000, DC21285_PCI_TYPE_0_CONFIG,    0x00f00000, DOMAIN_IO, 0, 1 }, /* Type 0 Config	*/ \
  { 0xe1000000, DC21285_ARMCSR_BASE,	      0x00100000, DOMAIN_IO, 0, 1 }, /* ARM CSR		*/ \
  { 0xe1100000, DC21285_PCI_IACK,	      0x00100000, DOMAIN_IO, 0, 1 }, /* PCI IACK	*/ \
  { 0xe1300000, DC21285_OUTBOUND_WRITE_FLUSH, 0x00100000, DOMAIN_IO, 0, 1 }, /* Out wrflsh	*/ \
  { 0xe1400000, DC21285_OUTBOUND_WRITE_FLUSH, 0x00100000, DOMAIN_IO, 0, 1 }, /* Out wrflsh	*/ \
  { 0xe1500000, DC21285_OUTBOUND_WRITE_FLUSH, 0x00100000, DOMAIN_IO, 0, 1 }, /* Out wrflsh	*/ \
  { 0xe1600000, DC21285_OUTBOUND_WRITE_FLUSH, 0x00100000, DOMAIN_IO, 0, 1 }, /* Out wrflsh	*/ \
  { 0xe1700000, DC21285_OUTBOUND_WRITE_FLUSH, 0x00100000, DOMAIN_IO, 0, 1 }, /* Out wrflsh	*/ \
  { 0xe1800000, DC21285_FLASH,                0x00800000, DOMAIN_IO, 0, 1 }  /* Flash		*/

#include "mm-armv.c"

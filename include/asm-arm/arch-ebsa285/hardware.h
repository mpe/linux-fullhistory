/*
 * linux/include/asm-arm/arch-ebsa285/hardware.h
 *
 * Copyright (C) 1998 Russell King.
 *
 * This file contains the hardware definitions of the EBSA-285.
 */


/*    Logical    Physical
 * 0xfff00000	0x40000000	X-Bus
 * 0xffe00000	0x7c000000	PCI I/O space
 *
 * 0xfe000000	0x42000000	CSR
 * 0xfd000000	0x78000000	Outbound write flush
 * 0xfc000000	0x79000000	PCI IACK/special space
 *
 * 0xf9000000	0x7a010000	PCI Config type 1
 * 0xf8000000	0x7b010000	PCI Config type 0
 * 
 */

#include <asm/dec21285.h>
 
#define IO_BASE			0xe0000000
#define PCIO_BASE		0xffe00000
#define PCI_IACK		0xfc000000 

#define XBUS_LEDS		((volatile unsigned char *)0xfff12000)
#define XBUS_LED_AMBER		(1 << 0)
#define XBUS_LED_GREEN		(1 << 1)
#define XBUS_LED_RED		(1 << 2)
#define XBUS_LED_TOGGLE		(1 << 8)

#define XBUS_SWITCH		((volatile unsigned char *)0xfff12000)
#define XBUS_SWITCH_SWITCH	((*XBUS_SWITCH) & 15)
#define XBUS_SWITCH_J17_13	((*XBUS_SWITCH) & (1 << 4))
#define XBUS_SWITCH_J17_11	((*XBUS_SWITCH) & (1 << 5))
#define XBUS_SWITCH_J17_9	((*XBUS_SWITCH) & (1 << 6))

#define KERNTOPHYS(a)		((unsigned long)(&a))

#define PARAMS_OFFSET		0x0100
#define PARAMS_BASE		(PAGE_OFFSET + PARAMS_OFFSET)

#define FLUSH_BASE_PHYS		0x50000000


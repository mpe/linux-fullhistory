/*
 * linux/include/asm-arm/arch-nexuspci/hardware.h
 *
 * Copyright (C) 1998-1999 Philip Blundell
 *
 * This file contains the hardware definitions of the Nexus PCI card.
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

/*    Logical    Physical
 * 0xfff00000	0x10000000	SCC2691 DUART
 * 0xffe00000	0x20000000	INTCONT
 * 0xffd00000	0x30000000	Status
 * 0xffc00000	0x60000000	PLX registers
 * 0xfe000000	0x70000000	PCI I/O
 */

/*
 * Mapping areas
 */
#define PCIO_BASE		0xfe000000
#define FLUSH_BASE		0xdf000000

/*
 * RAM definitions
 */
#define RAM_BASE		0x40000000
#define FLUSH_BASE_PHYS		0x40000000

#endif

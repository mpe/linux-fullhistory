/*
 * linux/include/asm-arm/arch-brutus/hardware.h
 *
 * Copyright (C) 1998 Nicolas Pitre <nico@cam.org>
 *
 * This file contains the hardware definitions for SA1100 architecture
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

/* Flushing areas */
#define FLUSH_BASE_PHYS		0xe0000000	/* SA1100 zero bank */
#define FLUSH_BASE		0xdf000000
#define FLUSH_BASE_MINICACHE	0xdf800000

/*
 * PCMCIA IO is mapped to 0xe0000000.  We are likely to use in*()/out*()
 * IO macros for what might appear there...
 * The SA1100 PCMCIA interface can be seen like a PC ISA bus for IO.
 */
#define PCIO_BASE		0xe0000000	/* PCMCIA0 IO space */

#endif


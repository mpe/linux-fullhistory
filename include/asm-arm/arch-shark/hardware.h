/*
 * linux/include/asm-arm/arch-shark/hardware.h
 *
 * by Alexander.Schulz@stud.uni-karlsruhe.de
 *
 * derived from:
 * linux/include/asm-arm/arch-ebsa110/hardware.h
 * Copyright (C) 1996-1999 Russell King.
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#ifndef __ASSEMBLY__

/*
 * Mapping areas
 */
#define IO_BASE			0xe0000000

/*
 * RAM definitions
 */
#define FLUSH_BASE_PHYS		0x60000000

#else

#define IO_BASE			0

#endif

#define IO_SIZE			0x10000000
#define IO_START		0x40000000

#define FLUSH_BASE		0xdf000000
#define PCIO_BASE		0xe0000000


/* defines for the Framebuffer */
#define FB_BASE                 0xd0000000
#define FB_START                0x06000000
#define FB_SIZE                 0x00200000

/* Registers for Framebuffer */
#define FBREG_BASE              (FB_BASE + FB_SIZE)
#define FBREG_START             0x06800000
#define FBREG_SIZE              0x000c0000

#endif


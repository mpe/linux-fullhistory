/*
 * linux/include/asm-arm/arch-ebsa110/hardware.h
 *
 * Copyright (C) 1996,1997,1998 Russell King.
 *
 * This file contains the hardware definitions of the EBSA-110.
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

/*
 * What hardware must be present
 */
#define HAS_PCIO

#ifndef __ASSEMBLER__

/*
 * IO definitions
 */
#define PIT_CTRL		((volatile unsigned char *)0xf200000d)
#define PIT_T2			((volatile unsigned char *)0xf2000009)
#define PIT_T1			((volatile unsigned char *)0xf2000005)
#define PIT_T0			((volatile unsigned char *)0xf2000001)
#define PCIO_BASE		0xf0000000

/*
 * Mapping areas
 */
#define IO_BASE			0xe0000000
#define IO_SIZE			0x20000000
#define IO_START		0xe0000000

/*
 * RAM definitions
 */
#define MAPTOPHYS(a)		((unsigned long)(a) - PAGE_OFFSET)
#define KERNTOPHYS(a)		((unsigned long)(&a))
#define KERNEL_BASE		(0xc0008000)
#define FLUSH_BASE_PHYS		0x40000000

#else

#define PCIO_BASE		0xf0000000
#define IO_BASE			0

#endif
#endif


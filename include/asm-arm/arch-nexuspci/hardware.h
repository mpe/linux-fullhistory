/*
 * linux/include/asm-arm/arch-nexuspci/hardware.h
 *
 * Copyright (C) 1997 Philip Blundell
 *
 * This file contains the hardware definitions of the Nexus PCI card.
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

/*
 * What hardware must be present
 */

#ifndef __ASSEMBLER__

/*
 * Mapping areas
 */
#define IO_END			0xffffffff
#define IO_BASE			0xd0000000
#define IO_SIZE			(IO_END - IO_BASE)
#define IO_START		0xd0000000

/*
 * RAM definitions
 */
#define RAM_BASE		0x40000000
#define MAPTOPHYS(a)		((unsigned long)(a) - PAGE_OFFSET + RAM_BASE)
#define KERNTOPHYS(a)		((unsigned long)(&a))
#define KERNEL_BASE		(0xc0008000)

#else

#define IO_BASE			0

#endif
#endif


/*
 * linux/include/asm-arm/arch-arc/hardware.h
 *
 * Copyright (C) 1996 Russell King.
 *
 * This file contains the hardware definitions of the A3/4/5xx series machines.
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

/*
 * What hardware must be present
 */
#define HAS_IOC
#define HAS_MEMC
#define HAS_MEMC1A
#define HAS_VIDC

/*
 * Optional hardware
 */
#define HAS_EXPMASK

#ifndef __ASSEMBLER__

/*
 * for use with inb/outb
 */
#define VIDC_BASE		0x80100000
#define IOCEC4IO_BASE		0x8009c000
#define LATCHAADDR		0x80094010
#define LATCHBADDR		0x80094006
#define IOCECIO_BASE		0x80090000
#define IOC_BASE		0x80080000
#define MEMCECIO_BASE		0x80000000

/*
 * IO definitions
 */
#define EXPMASK_BASE		((volatile unsigned char *)0x03360000)
#define IOEB_BASE		((volatile unsigned char *)0x03350050)
#define PCIO_FLOPPYDMABASE	((volatile unsigned char *)0x0302a000)
#define PCIO_BASE		0x03010000

/*
 * Mapping areas
 */
#define IO_END			0x03ffffff
#define IO_BASE			0x03000000
#define IO_SIZE			(IO_END - IO_BASE)
#define IO_START		0x03000000

/*
 * Screen mapping information
 */
#define SCREEN2_END		0x02078000
#define SCREEN2_BASE		0x02000000
#define SCREEN1_END		SCREEN2_BASE
#define SCREEN1_BASE		0x01f88000
#define SCREEN_START		0x02000000

/*
 * RAM definitions
 */
#define MAPTOPHYS(a)		(((unsigned long)a & 0x007fffff) + PAGE_OFFSET)
#define KERNTOPHYS(a)		((((unsigned long)(&a)) & 0x007fffff) + PAGE_OFFSET)
#define GET_MEMORY_END(p)	(PAGE_OFFSET + (p->u1.s.page_size) * (p->u1.s.nr_pages))
#define PARAMS_BASE		(PAGE_OFFSET + 0x7c000)
#define KERNEL_BASE		(PAGE_OFFSET + 0x80000)

#else

#define IOEB_BASE		0x03350050
#define IOC_BASE		0x03200000
#define PCIO_FLOPPYDMABASE	0x0302a000
#define PCIO_BASE		0x03010000
#define IO_BASE			0x03000000

#endif
#endif


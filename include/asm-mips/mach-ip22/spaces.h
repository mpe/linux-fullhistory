/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000, 03, 04 Ralf Baechle
 * Copyright (C) 2000, 2002  Maciej W. Rozycki
 * Copyright (C) 1990, 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_MACH_IP22_SPACES_H
#define _ASM_MACH_IP22_SPACES_H

#include <linux/config.h>

#ifdef CONFIG_MIPS32

#define CAC_BASE		0x80000000
#define IO_BASE			0xa0000000
#define UNCAC_BASE		0xa0000000
#define MAP_BASE		0xc0000000

/*
 * This handles the memory map.
 * We handle pages at KSEG0 for kernels with 32 bit address space.
 */
#define PAGE_OFFSET		0x80000000UL

/*
 * Memory above this physical address will be considered highmem.
 */
#ifndef HIGHMEM_START
#define HIGHMEM_START		0x20000000UL
#endif

#endif /* CONFIG_MIPS32 */

#ifdef CONFIG_MIPS64
#define PAGE_OFFSET		0xffffffff80000000UL

#ifndef HIGHMEM_START
#define HIGHMEM_START		(1UL << 59UL)
#endif

#define CAC_BASE		0xffffffff80000000
#define IO_BASE			0xffffffffa0000000
#define UNCAC_BASE		0xffffffffa0000000
#define MAP_BASE		0xffffffffc0000000

#define TO_PHYS(x)		(             ((x) & TO_PHYS_MASK))
#define TO_CAC(x)		(CAC_BASE   | ((x) & TO_PHYS_MASK))
#define TO_UNCAC(x)		(UNCAC_BASE | ((x) & TO_PHYS_MASK))

#endif /* CONFIG_MIPS64 */

#endif /* __ASM_MACH_IP22_SPACES_H */

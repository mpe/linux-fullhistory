/*
 *  linux/include/asm-arm/hardware.h
 *
 *  Copyright (C) 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Common hardware definitions
 */

#ifndef __ASM_HARDWARE_H
#define __ASM_HARDWARE_H

#include <asm/arch/hardware.h>

#ifdef PARAMS_OFFSET
#define PARAMS_BASE		((PAGE_OFFSET) + (PARAMS_OFFSET))
#else
#define PARAMS_BASE		0
#endif

#endif

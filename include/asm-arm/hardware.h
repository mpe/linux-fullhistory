/*
 * linux/include/asm-arm/hardware.h
 *
 * Copyright (C) 1996 Russell King
 *
 * Common hardware definitions
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

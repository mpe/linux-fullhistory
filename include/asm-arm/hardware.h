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

#ifndef FLUSH_BASE
#define FLUSH_BASE	0xdf000000
#endif

#ifdef HAS_EXPMASK
#ifndef __ASSEMBLER__
#define __EXPMASK(offset)	(((volatile unsigned char *)EXPMASK_BASE)[offset])
#else
#define __EXPMASK(offset)	offset
#endif

#define	EXPMASK_STATUS	__EXPMASK(0x00)
#define EXPMASK_ENABLE	__EXPMASK(0x04)

#endif

#endif

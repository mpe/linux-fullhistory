/*
 * linux/include/asm-arm/proc-armv/shmparam.h
 *
 * Copyright (C) 1996 Russell King
 *
 * definitions for the shared process memory on ARM v3 or v4
 * processors
 */

#ifndef __ASM_PROC_SHMPARAM_H
#define __ASM_PROC_SHMPARAM_H

#ifndef SHM_RANGE_START
#define SHM_RANGE_START	0x50000000
#define SHM_RANGE_END	0x60000000
#define SHMMAX		0x01000000
#endif

#endif

/*
 * linux/include/asm-arm/proc-armo/shmparam.h
 *
 * Copyright (C) 1996 Russell King
 *
 * definitions for the shared process memory on the ARM3
 */

#ifndef __ASM_PROC_SHMPARAM_H
#define __ASM_PROC_SHMPARAM_H

#ifndef SHM_RANGE_START
#define SHM_RANGE_START	0x00a00000
#define SHM_RANGE_END	0x00c00000
#define SHMMAX		0x003fa000
#endif

#endif

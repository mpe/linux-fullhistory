/*
 * linux/include/asm-arm/mmu_context.h
 *
 * Copyright (c) 1996 Russell King.
 *
 * Changelog:
 *  27-06-1996	RMK	Created
 */
#ifndef __ASM_ARM_MMU_CONTEXT_H
#define __ASM_ARM_MMU_CONTEXT_H

#define get_mmu_context(x) do { } while (0)

#define init_new_context(mm)	do { } while(0)
#define destroy_context(mm)	do { } while(0)
#define activate_context(tsk)	do { } while(0)

#endif

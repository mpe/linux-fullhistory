/*
 * linux/include/asm-arm/arch-nexuspci/processor.h
 *  from linux/include/asm-arm/arch-ebsa110/processor.h
 *
 * Copyright (C) 1996-1999 Russell King
 *
 * Changelog:
 *  21-Mar-1999	RMK	Added asm/arch/memory.h
 */

#ifndef __ASM_ARCH_PROCESSOR_H
#define __ASM_ARCH_PROCESSOR_H

/*
 * Bus types
 */
#define EISA_bus 0
#define EISA_bus__is_a_macro /* for versions in ksyms.c */
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE (TASK_SIZE / 3)

#endif

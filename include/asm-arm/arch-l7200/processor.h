/*
 * linux/include/asm-arm/arch-l7200/processor.h
 *
 * Copyright (c) 2000 Steven Hill (sjhill@cotw.com)
 *
 * Changelog:
 *  03-21-2000	SJH	Created
 *  05-03-2000	SJH	Comment cleaning
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

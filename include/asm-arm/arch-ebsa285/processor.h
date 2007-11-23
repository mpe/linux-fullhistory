/*
 * linux/include/asm-arm/arch-ebsa110/processor.h
 *
 * Copyright (C) 1996,1997,1998 Russell King
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

/*
 * User space: 3GB
 */
#define TASK_SIZE	(0xc0000000UL)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#if 0
#define TASK_UNMAPPED_BASE(off)	(TASK_SIZE / 3)
#else
#define TASK_UNMAPPED_BASE (TASK_SIZE / 3)
#endif
#define TASK_UNMAPPED_ALIGN(addr, off)	PAGE_ALIGN(addr)

#define INIT_MMAP \
{ &init_mm, 0, 0, PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC, NULL, &init_mm.mmap }

#endif

/*
 * include/asm-m68k/processor.h
 *
 * Copyright (C) 1995 Hamish Macdonald
 */

#ifndef __ASM_M68K_PROCESSOR_H
#define __ASM_M68K_PROCESSOR_H

#include <asm/segment.h>

/*
 * User space process size: 3.75GB. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.
 */
#define TASK_SIZE	(0xF0000000UL)

/*
 * Bus types
 */
#define EISA_bus__is_a_macro	1
#define EISA_bus 0
#define MCA_bus__is_a_macro	1
#define MCA_bus 0

/*
 * The m68k has no problems with write protection
 */
#define wp_works_ok__is_a_macro	1
#define wp_works_ok 1

/* MAX floating point unit state size (FSAVE/FRESTORE) */
#define FPSTATESIZE   (216/sizeof(unsigned short))

/* 
 * if you change this structure, you must change the code and offsets
 * in m68k/machasm.S
 */
   
struct thread_struct {
	unsigned long  ksp;		/* kernel stack pointer */
	unsigned long  usp;		/* user stack pointer */
	unsigned short sr;		/* saved status register */
	unsigned short fs;		/* saved fs (sfc, dfc) */
	unsigned long  *pagedir_v;	/* root page table virtual addr */
	unsigned long  pagedir_p;	/* root page table physaddr */
	unsigned long  crp[2];		/* cpu root pointer */
	unsigned long  esp0;		/* points to SR of stack frame */
	unsigned long  fp[8*3];
	unsigned long  fpcntl[3];	/* fp control regs */
	unsigned short fpstate[FPSTATESIZE];  /* floating point state */
};

#define INIT_MMAP { &init_mm, 0, 0x40000000, __pgprot(_PAGE_PRESENT|_PAGE_ACCESSED), VM_READ | VM_WRITE | VM_EXEC }

#define INIT_TSS  { \
	sizeof(init_kernel_stack) + (long) init_kernel_stack, 0, \
	PS_S, KERNEL_DS, \
	NULL, 0, {0, 0}, 0 \
}

#define alloc_kernel_stack()    get_free_page(GFP_KERNEL)
#define free_kernel_stack(page) free_page((page))

/*
 * Do necessary setup to start up a newly executed thread.
 */
static inline void start_thread(struct pt_regs * regs, unsigned long pc,
				unsigned long usp)
{
	unsigned long nilstate = 0;

	/* clear floating point state */
	__asm__ __volatile__ ("frestore %0@" : : "a" (&nilstate));

	/* reads from user space */
	set_fs(USER_DS);

	regs->pc = pc;
	regs->sr &= ~0x2000;
	wrusp(usp);
}

/*
 * Return saved PC of a blocked thread.
 */
extern inline unsigned long thread_saved_pc(struct thread_struct *t)
{
	return ((unsigned long *)((struct switch_stack *)t->ksp)->a6)[1];
}

#endif

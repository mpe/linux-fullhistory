/* $Id: processor.h,v 1.28 1995/11/25 02:32:30 davem Exp $
 * include/asm-sparc/processor.h
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __ASM_SPARC_PROCESSOR_H
#define __ASM_SPARC_PROCESSOR_H

#include <linux/string.h>

#include <asm/psr.h>
#include <asm/ptrace.h>
#include <asm/head.h>
#include <asm/signal.h>

/*
 * Bus types
 */
#define EISA_bus 0
#define EISA_bus__is_a_macro /* for versions in ksyms.c */
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

/*
 * Write Protection works right in supervisor mode on the Sparc...
 * And then there came the Swift module, which isn't so swift...
 */
extern char wp_works_ok;

/* Whee, this is STACK_TOP and the lowest kernel address too... */
#define TASK_SIZE	(KERNBASE)

/* The Sparc processor specific thread struct. */
struct thread_struct {
	unsigned long uwinmask __attribute__ ((aligned (8)));

	/* For signal handling */
	unsigned long sig_address __attribute__ ((aligned (8)));
	unsigned long sig_desc;

	/* Context switch saved kernel state. */
	unsigned long ksp __attribute__ ((aligned (8)));
	unsigned long kpc;
	unsigned long kpsr;
	unsigned long kwim;

	/* A place to store user windows and stack pointers
	 * when the stack needs inspection.
	 */
#define NSWINS 8
	struct reg_window reg_window[NSWINS] __attribute__ ((aligned (8)));
	unsigned long rwbuf_stkptrs[NSWINS] __attribute__ ((aligned (8)));
	unsigned long w_saved;

	/* Where our page table lives. */
	unsigned long pgd_ptr;

	/* The context currently allocated to this process
	 * or -1 if no context has been allocated to this
	 * task yet.
	 */
	int context;

	/* Floating point regs */
	unsigned long   float_regs[64] __attribute__ ((aligned (8)));
	unsigned long   fsr;
	unsigned long   fpqdepth;
	struct fpq {
		unsigned long *insn_addr;
		unsigned long insn;
	} fpqueue[16];
	struct sigstack sstk_info;
};

#define INIT_MMAP { &init_mm, (0xf0000000UL), (0xffffffffUL), \
		    __pgprot(0x0) , VM_READ | VM_WRITE | VM_EXEC }

#define INIT_TSS  { \
/* uwinmask, sig_address, sig_desc, ksp, kpc, kpsr, kwim */ \
   0,        0,           0,        0,   0,   0,    0, \
/* reg_window */  \
{ { { 0, }, { 0, } }, }, \
/* rwbuf_stkptrs */  \
{ 0, 0, 0, 0, 0, 0, 0, 0, }, \
/* w_saved, pgd_ptr,                context */  \
   0,       (long) &swapper_pg_dir, -1, \
/* FPU regs */   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, }, \
/* FPU status, FPU qdepth, FPU queue */ \
   0,          0,  { { 0, 0, }, }, \
/* sstk_info */ \
{ 0, 0, }, \
}

/* Return saved PC of a blocked thread. */
extern inline unsigned long thread_saved_pc(struct thread_struct *t)
{
	return ((struct pt_regs *)
		((t->ksp&(~0xfff))+(0x1000-TRACEREG_SZ)))->pc;
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
extern void start_thread(struct pt_regs * regs, unsigned long pc, unsigned long sp);

#endif /* __ASM_SPARC_PROCESSOR_H */


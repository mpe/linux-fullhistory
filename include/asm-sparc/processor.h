/* $Id: processor.h,v 1.43 1996/03/23 02:40:05 davem Exp $
 * include/asm-sparc/processor.h
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __ASM_SPARC_PROCESSOR_H
#define __ASM_SPARC_PROCESSOR_H

#include <linux/a.out.h>

#include <asm/psr.h>
#include <asm/ptrace.h>
#include <asm/head.h>
#include <asm/signal.h>
#include <asm/segment.h>

/*
 * Bus types
 */
#define EISA_bus 0
#define EISA_bus__is_a_macro /* for versions in ksyms.c */
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

/*
 * The sparc has no problems with write protection
 */
#define wp_works_ok 1
#define wp_works_ok__is_a_macro /* for versions in ksyms.c */

/* Whee, this is STACK_TOP and the lowest kernel address too... */
#define TASK_SIZE	(KERNBASE)

/* The Sparc processor specific thread struct. */
struct thread_struct {
	unsigned long uwinmask __attribute__ ((aligned (8)));
	struct pt_regs *kregs;

	/* For signal handling */
	unsigned long sig_address __attribute__ ((aligned (8)));
	unsigned long sig_desc;

	/* Context switch saved kernel state. */
	unsigned long ksp __attribute__ ((aligned (8)));
	unsigned long kpc;
	unsigned long kpsr;
	unsigned long kwim;

	/* Special child fork kpsr/kwim values. */
	unsigned long fork_kpsr __attribute__ ((aligned (8)));
	unsigned long fork_kwim;

	/* A place to store user windows and stack pointers
	 * when the stack needs inspection.
	 */
#define NSWINS 8
	struct reg_window reg_window[NSWINS] __attribute__ ((aligned (8)));
	unsigned long rwbuf_stkptrs[NSWINS] __attribute__ ((aligned (8)));
	unsigned long w_saved;

	/* Floating point regs */
	unsigned long   float_regs[64] __attribute__ ((aligned (8)));
	unsigned long   fsr;
	unsigned long   fpqdepth;
	struct fpq {
		unsigned long *insn_addr;
		unsigned long insn;
	} fpqueue[16];
	struct sigstack sstk_info;
	unsigned long flags;
	int current_ds;
	struct exec core_exec;     /* just what it says. */
};

#define SPARC_FLAG_KTHREAD      0x1    /* task is a kernel thread */

#define INIT_MMAP { &init_mm, (0), (0), \
		    __pgprot(0x0) , VM_READ | VM_WRITE | VM_EXEC }

#define INIT_TSS  { \
/* uwinmask, kregs, sig_address, sig_desc, ksp, kpc, kpsr, kwim */ \
   0,        0,     0,           0,        0,   0,   0,    0, \
/* fork_kpsr, fork_kwim */ \
   0,         0, \
/* reg_window */  \
{ { { 0, }, { 0, } }, }, \
/* rwbuf_stkptrs */  \
{ 0, 0, 0, 0, 0, 0, 0, 0, }, \
/* w_saved */ \
   0, \
/* FPU regs */   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, }, \
/* FPU status, FPU qdepth, FPU queue */ \
   0,          0,  { { 0, 0, }, }, \
/* sstk_info */ \
{ 0, 0, }, \
/* flags,              current_ds, */ \
   SPARC_FLAG_KTHREAD, USER_DS, \
/* core_exec */ \
{ 0, }, \
}

/* Return saved PC of a blocked thread. */
extern inline unsigned long thread_saved_pc(struct thread_struct *t)
{
	return t->kregs->pc;
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
extern inline void start_thread(struct pt_regs * regs, unsigned long pc, unsigned long sp)
{
	unsigned long saved_psr = (regs->psr & (PSR_CWP)) | PSR_S;
	int i;

	for(i = 0; i < 16; i++) regs->u_regs[i] = 0;
	regs->y = 0;
	regs->pc = ((pc & (~3)) - 4);
	regs->npc = regs->pc + 4;
	regs->psr = saved_psr;
	regs->u_regs[UREG_FP] = (sp - REGWIN_SZ);
}

#ifdef __KERNEL__
extern unsigned long (*alloc_kernel_stack)(struct task_struct *tsk);
extern void (*free_kernel_stack)(unsigned long stack);
extern struct task_struct *(*alloc_task_struct)(void);
extern void (*free_task_struct)(struct task_struct *tsk);
#endif

#endif /* __ASM_SPARC_PROCESSOR_H */

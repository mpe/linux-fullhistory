/* $Id: processor.h,v 1.32 1997/07/01 21:59:38 davem Exp $
 * include/asm-sparc64/processor.h
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __ASM_SPARC64_PROCESSOR_H
#define __ASM_SPARC64_PROCESSOR_H

#include <asm/a.out.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/segment.h>

/* Bus types */
#define EISA_bus 0
#define EISA_bus__is_a_macro /* for versions in ksyms.c */
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

/* The sparc has no problems with write protection */
#define wp_works_ok 1
#define wp_works_ok__is_a_macro /* for versions in ksyms.c */

/* User lives in his very own context, and cannot reference us. */
#define TASK_SIZE	((1UL << (PAGE_SHIFT - 3)) * PGDIR_SIZE)

#ifndef __ASSEMBLY__

#define NSWINS		8

/* The Sparc processor specific thread struct. */
struct thread_struct {
/*DC1*/	unsigned long ksp __attribute__ ((aligned(16)));
	unsigned long kpc;
/*DC2*/	unsigned long wstate;
	unsigned int cwp;
	unsigned int ctx;

/*DC3*/	unsigned int flags;
	unsigned int new_signal;
	unsigned long current_ds;
/*DC4*/	unsigned long w_saved;
	struct pt_regs *kregs;

	struct reg_window reg_window[NSWINS] __attribute__ ((aligned (16)));
	unsigned long rwbuf_stkptrs[NSWINS] __attribute__ ((aligned (8)));
	
	unsigned long sig_address __attribute__ ((aligned (8)));
	unsigned long sig_desc;
	struct sigstack sstk_info;
	struct exec core_exec;     /* just what it says. */
};

#endif /* !(__ASSEMBLY__) */

#define SPARC_FLAG_KTHREAD      0x1    /* task is a kernel thread */
#define SPARC_FLAG_UNALIGNED    0x2    /* is allowed to do unaligned accesses */
#define SPARC_FLAG_NEWSIGNALS   0x4    /* task wants new-style signals */
#define SPARC_FLAG_32BIT        0x8    /* task is older 32-bit binary */

#define INIT_MMAP { &init_mm, 0xfffff80000000000, 0xfffff80001000000, \
		    PAGE_SHARED , VM_READ | VM_WRITE | VM_EXEC, NULL, &init_mm.mmap }

#define INIT_TSS  {							\
/* ksp, kpc, wstate, cwp, secctx */ 					\
   0,   0,   0,	     0,	  0,						\
/* flags,              new_signal, current_ds, */			\
   SPARC_FLAG_KTHREAD, 0,          USER_DS,				\
/* w_saved, kregs, */							\
   0,       0,								\
/* reg_window */							\
   { { { 0, }, { 0, } }, }, 						\
/* rwbuf_stkptrs */							\
   { 0, 0, 0, 0, 0, 0, 0, 0, },						\
/* sig_address, sig_desc, sstk_info, core_exec */			\
   0,           0,        { 0, 0, }, { 0, },				\
}

#ifndef __ASSEMBLY__

/* Return saved PC of a blocked thread. */
extern __inline__ unsigned long thread_saved_pc(struct thread_struct *t)
{
	return t->kpc;
}

/* Do necessary setup to start up a newly executed thread. */
#define start_thread(regs, pc, sp) \
do { \
	regs->tstate = (regs->tstate & (TSTATE_CWP)) | (TSTATE_IE|TSTATE_PEF); \
	regs->tpc = ((pc & (~3)) - 4); \
	regs->tnpc = regs->tpc + 4; \
	regs->y = 0; \
	current->tss.flags &= ~SPARC_FLAG_32BIT; \
	current->tss.wstate = (1 << 3); \
	__asm__ __volatile__( \
	"stx		%%g0, [%0 + %2 + 0x00]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x08]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x10]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x18]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x20]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x28]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x30]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x38]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x40]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x48]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x50]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x58]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x60]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x68]\n\t" \
	"stx		%1,   [%0 + %2 + 0x70]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x78]\n\t" \
	"wrpr		%%g0, (1 << 3), %%wstate\n\t" \
	: \
	: "r" (regs), "r" (sp - REGWIN_SZ - STACK_BIAS), \
	  "i" ((const unsigned long)(&((struct pt_regs *)0)->u_regs[0]))); \
} while(0)

#define start_thread32(regs, pc, sp) \
do { \
	register unsigned int zero asm("g1"); \
\
	pc &= 0x00000000ffffffffUL; \
	sp &= 0x00000000ffffffffUL; \
\
	regs->tstate = (regs->tstate & (TSTATE_CWP))|(TSTATE_IE|TSTATE_AM|TSTATE_PEF); \
	regs->tpc = ((pc & (~3)) - 4); \
	regs->tnpc = regs->tpc + 4; \
	regs->y = 0; \
	current->tss.flags |= SPARC_FLAG_32BIT; \
	current->tss.wstate = (2 << 3); \
	zero = 0; \
	__asm__ __volatile__( \
	"stx		%%g0, [%0 + %2 + 0x00]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x08]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x10]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x18]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x20]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x28]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x30]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x38]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x40]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x48]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x50]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x58]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x60]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x68]\n\t" \
	"stx		%1,   [%0 + %2 + 0x70]\n\t" \
	"stx		%%g0, [%0 + %2 + 0x78]\n\t" \
	"wrpr		%%g0, (2 << 3), %%wstate\n\t" \
	: \
	: "r" (regs), "r" (sp - REGWIN32_SZ), \
	  "i" ((const unsigned long)(&((struct pt_regs *)0)->u_regs[0])), \
	  "r" (zero)); \
} while(0)

/* Free all resources held by a thread. */
#define release_thread(tsk)		do { } while(0)

#ifdef __KERNEL__
/* Allocation and freeing of task_struct and kernel stack. */
#define alloc_task_struct()   ((struct task_struct *)__get_free_pages(GFP_KERNEL, 1, 0))
#define free_task_struct(tsk) free_pages((unsigned long)(tsk),1)

#define init_task	(init_task_union.task)
#define init_stack	(init_task_union.stack)

#endif /* __KERNEL__ */

#endif /* !(__ASSEMBLY__) */

#endif /* !(__ASM_SPARC64_PROCESSOR_H) */

/* $Id: processor.h,v 1.8 1997/03/04 16:27:33 jj Exp $
 * include/asm-sparc64/processor.h
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __ASM_SPARC64_PROCESSOR_H
#define __ASM_SPARC64_PROCESSOR_H

#include <asm/a.out.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/head.h>
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

/* Whee, this is STACK_TOP + PAGE_SIZE and the lowest kernel address too... 
 * That one page is used to protect kernel from intruders, so that
 * we can make our access_ok test faster
 */
#define TASK_SIZE	(0xFFFFF80000000000UL)

#ifndef __ASSEMBLY__

struct fpq {
	unsigned long *insn_addr;
	unsigned long insn;
};

#define NSWINS		8

/* The Sparc processor specific thread struct. */
struct thread_struct {
	/* Floating point regs */
	/* Please check asm_offsets, so that not to much precious space
	   is wasted by this alignment and move the float_regs wherever
	   is better in this structure. Remember every byte of alignment
	   is multiplied by 512 to get the amount of wasted kernel memory. */
	unsigned int    float_regs[64] __attribute__ ((aligned (64)));
	unsigned long   fsr;
	unsigned long   fpqdepth;
	struct fpq	fpqueue[16];
	
	/* Context switch saved kernel state. */
	unsigned long user_globals[8];		/* performance hack */
	unsigned long ksp, kpc;

	/* Storage for windows when user stack is bogus. */
	struct reg_window reg_window[NSWINS] __attribute__ ((aligned (16)));
	unsigned long rwbuf_stkptrs[NSWINS] __attribute__ ((aligned (8)));
	unsigned long w_saved;

	/* Arch-specific task state flags, see below. */
	unsigned long flags;

	/* For signal handling */
	unsigned long sig_address __attribute__ ((aligned (8)));
	unsigned long sig_desc;

	struct sigstack sstk_info;
	int current_ds, new_signal;
	struct exec core_exec;     /* just what it says. */
};

#endif /* !(__ASSEMBLY__) */

#define SPARC_FLAG_KTHREAD      0x1    /* task is a kernel thread */
#define SPARC_FLAG_UNALIGNED    0x2    /* is allowed to do unaligned accesses */
#define SPARC_FLAG_NEWSIGNALS   0x4    /* task wants new-style signals */
#define SPARC_FLAG_32BIT        0x8    /* task is older 32-bit binary */

#define INIT_MMAP { &init_mm, 0xfffff80000000000, 0xfffffe00000, \
		    PAGE_SHARED , VM_READ | VM_WRITE | VM_EXEC }

#define INIT_TSS  {							\
/* FPU regs */   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	\
                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },	\
/* FPU status, FPU qdepth, FPU queue */ 				\
   0,          0,          { { 0, 0, }, }, 				\
/* user_globals */ 							\
   { 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 					\
/* ksp, kpc */ 								\
   0,   0, 								\
/* reg_window */							\
{ { { 0, }, { 0, } }, }, 						\
/* rwbuf_stkptrs */							\
{ 0, 0, 0, 0, 0, 0, 0, 0, },						\
/* w_saved */								\
   0, 									\
/* flags */								\
   SPARC_FLAG_KTHREAD,							\
/* sig_address, sig_desc */						\
   0,           0,							\
/* ex,     sstk_info, current_ds, */					\
   { 0, 0, }, USER_DS,							\
/* new_signal */							\
  0,									\
/* core_exec */								\
{ 0, },									\
}

#ifndef __ASSEMBLY__

/* Return saved PC of a blocked thread. */
extern __inline__ unsigned long thread_saved_pc(struct thread_struct *t)
{
	return t->kpc;
}

/* Do necessary setup to start up a newly executed thread. */
extern __inline__ void start_thread(struct pt_regs *regs, unsigned long pc,
				    unsigned long sp)
{
	regs->tstate = (regs->tstate & (TSTATE_CWP)) | TSTATE_IE;
	regs->tpc = ((pc & (~3)) - 4);
	regs->tnpc = regs->tpc + 4;
	regs->y = 0;
	__asm__ __volatile__("
		stx		%%g0, [%0 + %2 + 0x00]
		stx		%%g0, [%0 + %2 + 0x08]
		stx		%%g0, [%0 + %2 + 0x10]
		stx		%%g0, [%0 + %2 + 0x18]
		stx		%%g0, [%0 + %2 + 0x20]
		stx		%%g0, [%0 + %2 + 0x28]
		stx		%%g0, [%0 + %2 + 0x30]
		stx		%%g0, [%0 + %2 + 0x38]
		stx		%%g0, [%0 + %2 + 0x40]
		stx		%%g0, [%0 + %2 + 0x48]
		stx		%%g0, [%0 + %2 + 0x50]
		stx		%%g0, [%0 + %2 + 0x58]
		stx		%%g0, [%0 + %2 + 0x60]
		stx		%%g0, [%0 + %2 + 0x68]
		stx		%1,   [%0 + %2 + 0x70]
		stx		%%g0, [%0 + %2 + 0x78]
		" : : "r" (regs), "r" (sp - REGWIN_SZ),
		      "i" ((const unsigned long)(&((struct pt_regs *)0)->u_regs[0])));
}

extern __inline__ void start_thread32(struct pt_regs *regs, unsigned long pc,
				      unsigned long sp)
{
	register unsigned int zero asm("g1");

	pc &= 0x00000000ffffffffUL;
	sp &= 0x00000000ffffffffUL;

	regs->tstate = (regs->tstate & (TSTATE_CWP)) | (TSTATE_IE | TSTATE_AM);
	regs->tpc = ((pc & (~3)) - 4);
	regs->tnpc = regs->tpc + 4;
	regs->y = 0;
	zero = 0;
	__asm__ __volatile__("
		stx		%%g0, [%0 + %2 + 0x00]
		stx		%%g0, [%0 + %2 + 0x08]
		stx		%%g0, [%0 + %2 + 0x10]
		stx		%%g0, [%0 + %2 + 0x18]
		stx		%%g0, [%0 + %2 + 0x20]
		stx		%%g0, [%0 + %2 + 0x28]
		stx		%%g0, [%0 + %2 + 0x30]
		stx		%%g0, [%0 + %2 + 0x38]
		stx		%%g0, [%0 + %2 + 0x40]
		stx		%%g0, [%0 + %2 + 0x48]
		stx		%%g0, [%0 + %2 + 0x50]
		stx		%%g0, [%0 + %2 + 0x58]
		stx		%%g0, [%0 + %2 + 0x60]
		stx		%%g0, [%0 + %2 + 0x68]
		stx		%1,   [%0 + %2 + 0x70]
		stx		%%g0, [%0 + %2 + 0x78]
		" : : "r" (regs), "r" (sp - REGWIN32_SZ),
		      "i" ((const unsigned long)(&((struct pt_regs *)0)->u_regs[0])),
		      "r" (zero));
}

/* Free all resources held by a thread. */
#define release_thread(tsk)		do { } while(0)

#ifdef __KERNEL__
/* Allocation and freeing of basic task resources. */
#define alloc_kernel_stack(tsk)		__get_free_page(GFP_KERNEL)
#define free_kernel_stack(stack)	free_page(stack)
#define alloc_task_struct()		kmalloc(sizeof(struct task_struct), GFP_KERNEL)
#define free_task_struct(tsk)		kfree(tsk)
#endif

#endif /* !(__ASSEMBLY__) */

#endif /* !(__ASM_SPARC64_PROCESSOR_H) */

/* $Id: processor.h,v 1.51 1998/10/21 03:21:19 davem Exp $
 * include/asm-sparc64/processor.h
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __ASM_SPARC64_PROCESSOR_H
#define __ASM_SPARC64_PROCESSOR_H

#include <asm/asi.h>
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
#ifndef __ASSEMBLY__
#define TASK_SIZE	((unsigned long)-PGDIR_SIZE)
#else
#define TASK_SIZE	0xfffffffc00000000
#endif

#ifndef __ASSEMBLY__

#define NSWINS		7

typedef struct {
	unsigned long seg;
} mm_segment_t;

/* The Sparc processor specific thread struct. */
struct thread_struct {
/*DC1*/	unsigned long ksp __attribute__ ((aligned(16)));
	unsigned short wstate;
	unsigned short cwp;
	unsigned short flags;
	unsigned short ctx;

/*DC2*/	unsigned short w_saved;
	unsigned short new_signal;
	unsigned int ___pad;
	mm_segment_t current_ds;

/*DC3*/	struct pt_regs *kregs;
	unsigned long *utraps;

	struct reg_window reg_window[NSWINS] __attribute__ ((aligned (16)));
	unsigned long rwbuf_stkptrs[NSWINS] __attribute__ ((aligned (8)));
	
	unsigned long sig_address __attribute__ ((aligned (8)));
	unsigned long sig_desc;
	
	/* Performance counter state */
	u64 *user_cntd0, *user_cntd1;
	u64 kernel_cntd0, kernel_cntd1;
	u64 pcr_reg;

	unsigned char fpdepth;
	unsigned char fpsaved[7];
	unsigned char gsr[7];
	unsigned long xfsr[7];
};

#endif /* !(__ASSEMBLY__) */

#define SPARC_FLAG_KTHREAD      0x010    /* task is a kernel thread */
#define SPARC_FLAG_UNALIGNED    0x020    /* is allowed to do unaligned accesses */
#define SPARC_FLAG_NEWSIGNALS   0x040    /* task wants new-style signals */
#define SPARC_FLAG_32BIT        0x080    /* task is older 32-bit binary */
#define SPARC_FLAG_NEWCHILD     0x100    /* task is just-spawned child process */
#define SPARC_FLAG_PERFCTR	0x200    /* task has performance counters active */

#define INIT_MMAP { &init_mm, 0xfffff80000000000, 0xfffff80001000000, \
		    PAGE_SHARED , VM_READ | VM_WRITE | VM_EXEC, NULL, &init_mm.mmap }

#define INIT_TSS  {						\
/* ksp, wstate, cwp, flags,              ctx, */ 		\
   0,   0,      0,   SPARC_FLAG_KTHREAD, 0,			\
/* w_saved, new_signal, padding, current_ds, */			\
   0,       0,          0,       KERNEL_DS,			\
/* kregs,   utraps, */						\
   0,       0,							\
/* reg_window */						\
   { { { 0, }, { 0, } }, }, 					\
/* rwbuf_stkptrs */						\
   { 0, 0, 0, 0, 0, 0, 0, },					\
/* sig_address, sig_desc */					\
   0,           0,						\
/* user_cntd0, user_cndd1, kernel_cntd0, kernel_cntd0, pcr_reg */ \
   0,          0,          0,		 0,            0,	\
/* fpdepth, fpsaved, gsr,   xfsr */				\
   0,       { 0 },   { 0 }, { 0 },				\
}

#ifndef __ASSEMBLY__

/* Return saved PC of a blocked thread. */
extern __inline__ unsigned long thread_saved_pc(struct thread_struct *t)
{
	unsigned long *sp = (unsigned long *)(t->ksp + STACK_BIAS);
	unsigned long *fp = (unsigned long *)(sp[14] + STACK_BIAS);

	return fp[15];
}

/* On Uniprocessor, even in RMO processes see TSO semantics */
#ifdef __SMP__
#define TSTATE_INITIAL_MM	TSTATE_TSO
#else
#define TSTATE_INITIAL_MM	TSTATE_RMO
#endif

/* Do necessary setup to start up a newly executed thread. */
#define start_thread(regs, pc, sp) \
do { \
	regs->tstate = (regs->tstate & (TSTATE_CWP)) | (TSTATE_INITIAL_MM|TSTATE_IE) | (ASI_PNF << 24); \
	regs->tpc = ((pc & (~3)) - 4); \
	regs->tnpc = regs->tpc + 4; \
	regs->y = 0; \
	current->tss.flags &= ~SPARC_FLAG_32BIT; \
	current->tss.wstate = (1 << 3); \
	if (current->tss.utraps) { \
		if (*(current->tss.utraps) < 2) \
			kfree (current->tss.utraps); \
		else \
			(*(current->tss.utraps))--; \
		current->tss.utraps = NULL; \
	} \
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
	pc &= 0x00000000ffffffffUL; \
	sp &= 0x00000000ffffffffUL; \
\
	regs->tstate = (regs->tstate & (TSTATE_CWP))|(TSTATE_INITIAL_MM|TSTATE_IE|TSTATE_AM); \
	regs->tpc = ((pc & (~3)) - 4); \
	regs->tnpc = regs->tpc + 4; \
	regs->y = 0; \
	current->tss.flags |= SPARC_FLAG_32BIT; \
	current->tss.wstate = (2 << 3); \
	if (current->tss.utraps) { \
		if (*(current->tss.utraps) < 2) \
			kfree (current->tss.utraps); \
		else \
			(*(current->tss.utraps))--; \
		current->tss.utraps = NULL; \
	} \
	__asm__ __volatile__( \
	"stxa		%3, [%4] %5\n\t" \
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
	  "r" (current->mm->pgd[0]), "r" (TSB_REG), "i" (ASI_DMMU)); \
} while(0)

/* Free all resources held by a thread. */
#define release_thread(tsk)		do { } while(0)

#define copy_segments(nr, tsk, mm)	do { } while (0)
#define release_segments(mm)		do { } while (0)
#define forget_segments()		do { } while (0)

#ifdef __KERNEL__
/* Allocation and freeing of task_struct and kernel stack. */
#define alloc_task_struct()   ((struct task_struct *)__get_free_pages(GFP_KERNEL, 1))
#define free_task_struct(tsk) free_pages((unsigned long)(tsk),1)

#define init_task	(init_task_union.task)
#define init_stack	(init_task_union.stack)

#endif /* __KERNEL__ */

#endif /* !(__ASSEMBLY__) */

#endif /* !(__ASM_SPARC64_PROCESSOR_H) */

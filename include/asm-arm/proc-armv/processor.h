/*
 * linux/include/asm-arm/proc-armv/processor.h
 *
 * Copyright (c) 1996 Russell King.
 *
 * Changelog:
 *  20-09-1996	RMK	Created
 *  26-09-1996	RMK	Added 'EXTRA_THREAD_STRUCT*'
 *  28-09-1996	RMK	Moved start_thread into the processor dependencies
 *  09-09-1998	PJB	Delete redundant `wp_works_ok'
 */
#ifndef __ASM_PROC_PROCESSOR_H
#define __ASM_PROC_PROCESSOR_H

#ifdef __KERNEL__
 
#define KERNEL_STACK_SIZE	PAGE_SIZE

struct context_save_struct {
	unsigned long cpsr;
	unsigned long r4;
	unsigned long r5;
	unsigned long r6;
	unsigned long r7;
	unsigned long r8;
	unsigned long r9;
	unsigned long fp;
	unsigned long pc;
};

#define EXTRA_THREAD_STRUCT				\
	struct context_save_struct *save;		\
	unsigned long memmap;

#define EXTRA_THREAD_STRUCT_INIT			\
	0,						\
	((unsigned long) swapper_pg_dir) - PAGE_OFFSET

DECLARE_THREAD_STRUCT;

/*
 * Return saved PC of a blocked thread.
 */
extern __inline__ unsigned long thread_saved_pc (struct thread_struct *t)
{
	if (t->save)
		return t->save->pc;
	else
		return 0;
}

extern __inline__ unsigned long get_css_fp (struct thread_struct *t)
{
	if (t->save)
		return t->save->fp;
	else
		return 0;
}

asmlinkage void ret_from_sys_call(void) __asm__ ("ret_from_sys_call");

extern __inline__ void copy_thread_css (struct context_save_struct *save)
{
	save->cpsr = SVC_MODE;
	save->r4 =
	save->r5 =
	save->r6 =
	save->r7 =
	save->r8 =
	save->r9 =
	save->fp = 0;
	save->pc = (unsigned long) ret_from_sys_call;
}

#define start_thread(regs,pc,sp)					\
({									\
	unsigned long *stack = (unsigned long *)sp;			\
	set_fs(USER_DS);						\
	memzero(regs->uregs, sizeof(regs->uregs));			\
	if (current->personality == PER_LINUX_32BIT)			\
		regs->ARM_cpsr = USR_MODE;				\
	else								\
		regs->ARM_cpsr = USR26_MODE;				\
	regs->ARM_pc = pc;		/* pc */			\
	regs->ARM_sp = sp;		/* sp */			\
	regs->ARM_r2 = stack[2];	/* r2 (envp) */			\
	regs->ARM_r1 = stack[1];	/* r1 (argv) */			\
	regs->ARM_r0 = stack[0];	/* r0 (argc) */			\
})

/* Allocation and freeing of basic task resources. */
/*
 * NOTE! The task struct and the stack go together
 */
#define alloc_task_struct() \
	((struct task_struct *) __get_free_pages(GFP_KERNEL,1))
#define free_task_struct(p)	free_pages((unsigned long)(p),1)

#endif

#endif

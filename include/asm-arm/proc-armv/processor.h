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

#define INIT_CSS (struct context_save_struct){ SVC_MODE, 0, 0, 0, 0, 0, 0, 0, 0 }

#define EXTRA_THREAD_STRUCT
#define EXTRA_THREAD_STRUCT_INIT
#define SWAPPER_PG_DIR	(((unsigned long)swapper_pg_dir) - PAGE_OFFSET)

#define start_thread(regs,pc,sp)					\
({									\
	unsigned long *stack = (unsigned long *)sp;			\
	set_fs(USER_DS);						\
	memzero(regs->uregs, sizeof(regs->uregs));			\
	if (current->personality & ADDR_LIMIT_32BIT)			\
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
#define ll_alloc_task_struct() ((struct task_struct *) __get_free_pages(GFP_KERNEL,1))
#define ll_free_task_struct(p) free_pages((unsigned long)(p),1)

#endif

/*
 * linux/include/asm-arm/proc-armo/processor.h
 *
 * Copyright (c) 1996 Russell King.
 *
 * Changelog:
 *  27-06-1996	RMK	Created
 *  10-10-1996	RMK	Brought up to date with SA110
 *  26-09-1996	RMK	Added 'EXTRA_THREAD_STRUCT*'
 *  28-09-1996	RMK	Moved start_thread into the processor dependencies
 *  11-01-1998	RMK	Added new uaccess_t
 *  09-09-1998	PJB	Delete redundant `wp_works_ok'
 */
#ifndef __ASM_PROC_PROCESSOR_H
#define __ASM_PROC_PROCESSOR_H

#ifdef __KERNEL__

#include <asm/assembler.h>
#include <linux/string.h>

#define KERNEL_STACK_SIZE 4096

struct context_save_struct {
	unsigned long r4;
	unsigned long r5;
	unsigned long r6;
	unsigned long r7;
	unsigned long r8;
	unsigned long r9;
	unsigned long fp;
	unsigned long pc;
};

typedef struct {
	void (*put_byte)(void);			/* Special calling convention */
	void (*get_byte)(void);			/* Special calling convention */
	void (*put_half)(void);			/* Special calling convention */
	void (*get_half)(void);			/* Special calling convention */
	void (*put_word)(void);			/* Special calling convention */
	void (*get_word)(void);			/* Special calling convention */
	unsigned long (*copy_from_user)(void *to, const void *from, unsigned long sz);
	unsigned long (*copy_to_user)(void *to, const void *from, unsigned long sz);
	unsigned long (*clear_user)(void *addr, unsigned long sz);
	unsigned long (*strncpy_from_user)(char *to, const char *from, unsigned long sz);
	unsigned long (*strlen_user)(const char *s);
} uaccess_t;

extern uaccess_t uaccess_user, uaccess_kernel;

#define EXTRA_THREAD_STRUCT							\
	uaccess_t	*uaccess;		/* User access functions*/	\
	struct context_save_struct *save;					\
	unsigned long	memmap;							\
	unsigned long	memcmap[256];

#define EXTRA_THREAD_STRUCT_INIT		\
	&uaccess_kernel,			\
	0,					\
	(unsigned long) swapper_pg_dir,		\
	{ 0, }

DECLARE_THREAD_STRUCT;

/*
 * Return saved PC of a blocked thread.
 */
extern __inline__ unsigned long thread_saved_pc (struct thread_struct *t)
{
	if (t->save)
		return t->save->pc & ~PCMASK;
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

asmlinkage void ret_from_sys_call(void) __asm__("ret_from_sys_call");

extern __inline__ void copy_thread_css (struct context_save_struct *save)
{
	save->r4 =
	save->r5 =
	save->r6 =
	save->r7 =
	save->r8 =
	save->r9 =
	save->fp = 0;
	save->pc = ((unsigned long)ret_from_sys_call) | SVC26_MODE;
}

#define start_thread(regs,pc,sp)					\
({									\
	unsigned long *stack = (unsigned long *)sp;			\
	set_fs(USER_DS);						\
	memzero(regs->uregs, sizeof (regs->uregs));			\
	regs->ARM_pc = pc;		/* pc */			\
	regs->ARM_sp = sp;		/* sp */			\
	regs->ARM_r2 = stack[2];	/* r2 (envp) */			\
	regs->ARM_r1 = stack[1];	/* r1 (argv) */			\
	regs->ARM_r0 = stack[0];	/* r0 (argc) */			\
	flush_tlb_mm(current->mm);					\
})

/* Allocation and freeing of basic task resources. */
/*
 * NOTE! The task struct and the stack go together
 */
#define alloc_task_struct() \
	((struct task_struct *) __get_free_pages(GFP_KERNEL,1))
#define free_task_struct(p)    free_pages((unsigned long)(p),1)


#endif

#endif

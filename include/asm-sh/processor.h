/*
 * include/asm-sh/processor.h
 *
 * Copyright (C) 1999 Niibe Yutaka
 */

#ifndef __ASM_SH_PROCESSOR_H
#define __ASM_SH_PROCESSOR_H

#include <asm/page.h>
#include <asm/types.h>
#include <linux/threads.h>

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ void *pc; __asm__("mova	1f,%0\n1:":"=z" (pc)); pc; })

/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 */

struct sh_cpuinfo {
	unsigned long loops_per_sec;
	unsigned long *pgd_quick;
	unsigned long *pte_quick;
	unsigned long pgtable_cache_sz;
};

extern struct sh_cpuinfo boot_cpu_data;

#define cpu_data &boot_cpu_data
#define current_cpu_data boot_cpu_data

/*
 * User space process size: 2GB.
 */
#define TASK_SIZE	0x80000000

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(TASK_SIZE / 3)

struct thread_struct {
	unsigned long sp;
	unsigned long pc;

	unsigned long trap_no, error_code;
	unsigned long address;
	/* Hardware debugging registers may come here */
};

#define INIT_MMAP \
{ &init_mm, 0x80000000, 0xa0000000, NULL, PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC, 1, NULL, NULL }

#define INIT_THREAD  {						\
	sizeof(init_stack) + (long) &init_stack, /* sp */	\
	0,					 /* pc */	\
	0, 0, \
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
#define start_thread(regs, new_pc, new_sp)	 \
	set_fs(USER_DS);			 \
	regs->pr = 0;   		 	 \
	regs->sr = 0;		/* User mode. */ \
	regs->pc = new_pc;			 \
	regs->u_regs[UREG_SP] = new_sp

/* Forward declaration, a strange C thing */
struct task_struct;
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);
/*
 * create a kernel thread without removing it from tasklists
 */
extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

/*
 * Bus types
 */
#define EISA_bus 0
#define EISA_bus__is_a_macro /* for versions in ksyms.c */
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */


/* Copy and release all segment info associated with a VM */
#define copy_segments(p, mm)	do { } while(0)
#define release_segments(mm)	do { } while(0)
#define forget_segments()	do { } while (0)

/*
 * Return saved PC of a blocked thread.
 */
extern __inline__ unsigned long thread_saved_pc(struct thread_struct *t)
{
	return t->pc;
}

#define THREAD_SIZE (2*PAGE_SIZE)
extern struct task_struct * alloc_task_struct(void);
extern void free_task_struct(struct task_struct *);

#define init_task	(init_task_union.task)
#define init_stack	(init_task_union.stack)

#endif /* __ASM_SH_PROCESSOR_H */

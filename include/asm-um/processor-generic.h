/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_PROCESSOR_GENERIC_H
#define __UM_PROCESSOR_GENERIC_H

struct pt_regs;

struct task_struct;

#include "linux/config.h"
#include "asm/ptrace.h"
#include "choose-mode.h"

struct mm_struct;

#define cpu_relax()   barrier()

struct thread_struct {
	int forking;
	int nsyscalls;
	struct pt_regs regs;
	unsigned long cr2;
	int err;
	unsigned long trap_no;
	int singlestep_syscall;
	void *fault_addr;
	void *fault_catcher;
	struct task_struct *prev_sched;
	unsigned long temp_stack;
	void *exec_buf;
	struct arch_thread arch;
	union {
#ifdef CONFIG_MODE_TT
		struct {
			int extern_pid;
			int tracing;
			int switch_pipe[2];
			int vm_seq;
		} tt;
#endif
#ifdef CONFIG_MODE_SKAS
		struct {
			void *switch_buf;
			void *fork_buf;
			int mm_count;
		} skas;
#endif
	} mode;
	struct {
		int op;
		union {
			struct {
				int pid;
			} fork, exec;
			struct {
				int (*proc)(void *);
				void *arg;
			} thread;
			struct {
				void (*proc)(void *);
				void *arg;
			} cb;
		} u;
	} request;
};

#define INIT_THREAD \
{ \
	.forking		= 0, \
	.nsyscalls		= 0, \
        .regs		   	= EMPTY_REGS, \
	.cr2			= 0, \
	.err			= 0, \
	.fault_addr		= NULL, \
	.prev_sched		= NULL, \
	.temp_stack		= 0, \
	.exec_buf		= NULL, \
	.arch			= INIT_ARCH_THREAD, \
	.request		= { 0 } \
}

typedef struct {
	unsigned long seg;
} mm_segment_t;

extern struct task_struct *alloc_task_struct(void);

extern void release_thread(struct task_struct *);
extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);
extern void dump_thread(struct pt_regs *regs, struct user *u);
extern void prepare_to_copy(struct task_struct *tsk);

extern unsigned long thread_saved_pc(struct task_struct *t);

static inline void mm_copy_segments(struct mm_struct *from_mm,
				    struct mm_struct *new_mm)
{
}

#define init_stack	(init_thread_union.stack)

/*
 * User space process size: 3GB (default).
 */
extern unsigned long task_size;

#define TASK_SIZE	(task_size)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(0x40000000)

extern void start_thread(struct pt_regs *regs, unsigned long entry, 
			 unsigned long stack);

struct cpuinfo_um {
	unsigned long loops_per_jiffy;
	int ipi_pipe[2];
};

extern struct cpuinfo_um boot_cpu_data;

#define my_cpu_data		cpu_data[smp_processor_id()]

#ifdef CONFIG_SMP
extern struct cpuinfo_um cpu_data[];
#define current_cpu_data cpu_data[smp_processor_id()]
#else
#define cpu_data (&boot_cpu_data)
#define current_cpu_data boot_cpu_data
#endif

#define KSTK_EIP(tsk) (PT_REGS_IP(&tsk->thread.regs))
#define KSTK_ESP(tsk) (PT_REGS_SP(&tsk->thread.regs))
#define get_wchan(p) (0)

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */

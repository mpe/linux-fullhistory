/* thread_info.h: x86_64 low-level thread information
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <linux/config.h>
#include <asm/pda.h>
#endif

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 */
#ifndef __ASSEMBLY__
struct thread_info {
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	__u32			flags;		/* low level flags */
	__u32			cpu;		/* current CPU */
	int 			preempt_count;

	mm_segment_t		addr_limit;	
};

#endif

/*
 * macros/functions for gaining access to the thread information structure
 */
#ifndef __ASSEMBLY__
#define INIT_THREAD_INFO(tsk)			\
{						\
	task:		&tsk,			\
	exec_domain:	&default_exec_domain,	\
	flags:		0,			\
	cpu:		0,			\
	addr_limit:	KERNEL_DS,		\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* how to get the thread information struct from C */

#define THREAD_SIZE (2*PAGE_SIZE)

static inline struct thread_info *current_thread_info(void)
{ 
	struct thread_info *ti;
	ti = (void *)read_pda(kernelstack) + PDA_STACKOFFSET - THREAD_SIZE;
	return ti; 
} 

static inline struct thread_info *stack_thread_info(void)
{
	struct thread_info *ti;
	__asm__("andq %%rsp,%0; ":"=r" (ti) : "0" (~8191UL));
	return ti;
}

/* thread information allocation */
#define alloc_thread_info() ((struct thread_info *) __get_free_pages(GFP_KERNEL,1))
#define free_thread_info(ti) free_pages((unsigned long) (ti), 1)
#define get_thread_info(ti) get_task_struct((ti)->task)
#define put_thread_info(ti) put_task_struct((ti)->task)

#else /* !__ASSEMBLY__ */

/* how to get the thread information struct from ASM */
/* only works on the process stack. otherwise get it via the PDA. */
#define GET_THREAD_INFO(reg) \
	movq $-8192, reg; \
	andq %rsp, reg

#endif

/*
 * thread information flags
 * - these are process state flags that various assembly files may need to access
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 * Warning: layout of LSW is hardcoded in entry.S
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_NOTIFY_RESUME	1	/* resumption notification requested */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_USEDFPU		16	/* FPU was used by this task this quantum */
#define TIF_POLLING_NRFLAG	17	/* true if poll_idle() is polling TIF_NEED_RESCHED */
#define TIF_IA32		18	/* 32bit process */ 

#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_USEDFPU		(1<<TIF_USEDFPU)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)
#define _TIF_IA32		(1<<TIF_IA32)

#define _TIF_WORK_MASK		0x0000FFFE	/* work to do on interrupt/exception return */
#define _TIF_ALLWORK_MASK	0x0000FFFF	/* work to do on any return to u-space */

#define PREEMPT_ACTIVE     0x4000000

#endif /* __KERNEL__ */

#endif /* _ASM_THREAD_INFO_H */

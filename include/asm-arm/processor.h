/*
 * include/asm-arm/processor.h
 *
 * Copyright (C) 1995 Russell King
 */

#ifndef __ASM_ARM_PROCESSOR_H
#define __ASM_ARM_PROCESSOR_H

struct fp_hard_struct {
	unsigned int save[140/4];		/* as yet undefined */
};

struct fp_soft_struct {
	unsigned int save[140/4];		/* undefined information */
};

union fp_state {
	struct fp_hard_struct	hard;
	struct fp_soft_struct	soft;
};

typedef unsigned long mm_segment_t;		/* domain register	*/

#define NR_DEBUGS	5

#define DECLARE_THREAD_STRUCT							\
struct thread_struct {								\
	unsigned long	address;		/* Address of fault	*/	\
	unsigned long	trap_no;		/* Trap number		*/	\
	unsigned long	error_code;		/* Error code of trap	*/	\
	union fp_state	fpstate;		/* FPE save state	*/	\
	unsigned long	debug[NR_DEBUGS];	/* Debug/ptrace		*/	\
	EXTRA_THREAD_STRUCT							\
}

#include <asm/arch/processor.h>
#include <asm/proc/processor.h>

#define INIT_TSS  {			\
	0,				\
	0,				\
	0,				\
	{ { { 0, }, }, },		\
	{ 0, },				\
	EXTRA_THREAD_STRUCT_INIT	\
}

/* Forward declaration, a strange C thing */
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/* Copy and release all segment info associated with a VM */
#define copy_segments(nr, tsk, mm)	do { } while (0)
#define release_segments(mm)		do { } while (0)
#define forget_segments()		do { } while (0)

#define init_task	(init_task_union.task)
#define init_stack	(init_task_union.stack)

#endif /* __ASM_ARM_PROCESSOR_H */

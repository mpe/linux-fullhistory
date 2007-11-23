/*
 * include/asm-arm/processor.h
 *
 * Copyright (C) 1995 Russell King
 */

#ifndef __ASM_ARM_PROCESSOR_H
#define __ASM_ARM_PROCESSOR_H

#define FP_SIZE 35

struct fp_hard_struct {
	unsigned int save[FP_SIZE];		/* as yet undefined */
};

struct fp_soft_struct {
	unsigned int save[FP_SIZE];		/* undefined information */
};

union fp_state {
	struct fp_hard_struct	hard;
	struct fp_soft_struct	soft;
};

typedef unsigned long mm_segment_t;		/* domain register	*/

#ifdef __KERNEL__

#include <asm/assembler.h> 

#define NR_DEBUGS	5

#include <asm/arch/processor.h>
#include <asm/proc/processor.h>

struct thread_struct {
	unsigned long			address;	  /* Address of fault	*/
	unsigned long			trap_no;	  /* Trap number	*/
	unsigned long			error_code;	  /* Error code of trap	*/
	union fp_state			fpstate;	  /* FPE save state	*/
	unsigned long			debug[NR_DEBUGS]; /* Debug/ptrace	*/
	struct context_save_struct	*save;		  /* context save	*/
	unsigned long			memmap;		  /* page tables	*/
	EXTRA_THREAD_STRUCT
};

#define INIT_MMAP \
{ &init_mm, 0, 0, NULL, PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC, 1, NULL, NULL }

#define INIT_TSS  {				\
	0,					\
	0,					\
	0,					\
	{ { { 0, }, }, },			\
	{ 0, },					\
	(struct context_save_struct *)0,	\
	SWAPPER_PG_DIR				\
	EXTRA_THREAD_STRUCT_INIT		\
}

/*
 * Return saved PC of a blocked thread.
 */
extern __inline__ unsigned long thread_saved_pc(struct thread_struct *t)
{
	return t->save ? t->save->pc & ~PCMASK : 0;
}

extern __inline__ unsigned long get_css_fp(struct thread_struct *t)
{
	return t->save ? t->save->fp : 0;
}

asmlinkage void ret_from_sys_call(void) __asm__("ret_from_sys_call");

extern __inline__ void init_thread_css(struct context_save_struct *save)
{
	*save = INIT_CSS;
	save->pc |= (unsigned long)ret_from_sys_call;
}

/* Forward declaration, a strange C thing */
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/* Copy and release all segment info associated with a VM */
#define copy_segments(nr, tsk, mm)	do { } while (0)
#define release_segments(mm)		do { } while (0)
#define forget_segments()		do { } while (0)

extern struct task_struct *alloc_task_struct(void);
extern void free_task_struct(struct task_struct *);

#define init_task	(init_task_union.task)
#define init_stack	(init_task_union.stack)

#endif

#endif /* __ASM_ARM_PROCESSOR_H */

/*
 * include/asm-x86_64/i387.h
 *
 * Copyright (C) 1994 Linus Torvalds
 *
 * Pentium III FXSR, SSE support
 * General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * x86-64 work by Andi Kleen 2002
 */

#ifndef __ASM_X86_64_I387_H
#define __ASM_X86_64_I387_H

#include <linux/sched.h>
#include <asm/processor.h>
#include <asm/sigcontext.h>
#include <asm/user.h>
#include <asm/thread_info.h>

extern void fpu_init(void);
extern void init_fpu(void);
int save_i387(struct _fpstate *buf);

static inline int need_signal_i387(struct task_struct *me) 
{ 
	if (!me->used_math)
		return 0;
	me->used_math = 0; 
	if (!test_thread_flag(TIF_USEDFPU))
		return 0;
	return 1;
} 

/*
 * FPU lazy state save handling...
 */

#define kernel_fpu_end() stts()

#define unlazy_fpu(tsk) do { \
	if (test_tsk_thread_flag(tsk, TIF_USEDFPU)) \
		save_init_fpu(tsk); \
} while (0)

#define clear_fpu(tsk) do { \
	if (test_tsk_thread_flag(tsk, TIF_USEDFPU)) {		\
		asm volatile("fwait");				\
		clear_tsk_thread_flag(tsk,TIF_USEDFPU); 	\
		stts();						\
	}							\
} while (0)

#define load_mxcsr(val) do { \
		unsigned long __mxcsr = ((unsigned long)(val) & 0xffbf); \
		asm volatile("ldmxcsr %0" : : "m" (__mxcsr)); \
} while (0)

/*
 * ptrace request handers...
 */
extern int get_fpregs(struct user_i387_struct *buf,
		      struct task_struct *tsk);
extern int set_fpregs(struct task_struct *tsk,
		      struct user_i387_struct *buf);

/*
 * FPU state for core dumps...
 */
extern int dump_fpu(struct pt_regs *regs,
		    struct user_i387_struct *fpu);

/*
 * i387 state interaction
 */
#define get_fpu_mxcsr(t) ((t)->thread.i387.fxsave.mxcsr)
#define get_fpu_cwd(t) ((t)->thread.i387.fxsave.cwd)
#define get_fpu_fxsr_twd(t) ((t)->thread.i387.fxsave.twd)
#define get_fpu_swd(t) ((t)->thread.i387.fxsave.swd)
#define set_fpu_cwd(t,val) ((t)->thread.i387.fxsave.cwd = (val))
#define set_fpu_swd(t,val) ((t)->thread.i387.fxsave.swd = (val))
#define set_fpu_fxsr_twd(t,val) ((t)->thread.i387.fxsave.twd = (val))
#define set_fpu_mxcsr(t,val) ((t)->thread.i387.fxsave.mxcsr = (val)&0xffbf)

static inline int restore_fpu_checking(struct i387_fxsave_struct *fx) 
{ 
	int err;
	asm volatile("1:  rex64 ; fxrstor (%[fx])\n\t"
		     "2:\n"
		     ".section .fixup,\"ax\"\n"
		     "3:  movl $-1,%[err]\n"
		     "    jmp  2b\n"
		     ".previous\n"
		     ".section __ex_table,\"a\"\n"
		     "   .align 8\n"
		     "   .quad  1b,3b\n"
		     ".previous"
		     : [err] "=r" (err)
		     : [fx] "r" (fx), "0" (0)); 
	return err;
} 

static inline int save_i387_checking(struct i387_fxsave_struct *fx) 
{ 
	int err;
	asm volatile("1:  rex64 ; fxsave (%[fx])\n\t"
		     "2:\n"
		     ".section .fixup,\"ax\"\n"
		     "3:  movl $-1,%[err]\n"
		     "    jmp  2b\n"
		     ".previous\n"
		     ".section __ex_table,\"a\"\n"
		     "   .align 8\n"
		     "   .quad  1b,3b\n"
		     ".previous"
		     : [err] "=r" (err)
		     : [fx] "r" (fx), "0" (0)); 
	return err;
} 

static inline void kernel_fpu_begin(void)
{
	struct task_struct *me = current;
	if (test_tsk_thread_flag(me,TIF_USEDFPU)) {
		asm volatile("fxsave %0 ; fnclex"
			      : "=m" (me->thread.i387.fxsave));
		clear_tsk_thread_flag(me, TIF_USEDFPU);
		return;
	}
	clts();
}

static inline void save_init_fpu( struct task_struct *tsk )
{
	asm volatile( "fxsave %0 ; fnclex"
		      : "=m" (tsk->thread.i387.fxsave));
	clear_tsk_thread_flag(tsk, TIF_USEDFPU);
	stts();
}

/* 
 * This restores directly out of user space. Exceptions are handled.
 */
static inline int restore_i387(struct _fpstate *buf)
{
	return restore_fpu_checking((struct i387_fxsave_struct *)buf);
}


static inline void empty_fpu(struct task_struct *child)
{
	if (!child->used_math) {
		/* Simulate an empty FPU. */
		memset(&child->thread.i387.fxsave,0,sizeof(struct i387_fxsave_struct));
		child->thread.i387.fxsave.cwd = 0x037f; 
		child->thread.i387.fxsave.swd = 0;
		child->thread.i387.fxsave.twd = 0; 
		child->thread.i387.fxsave.mxcsr = 0x1f80;
	}
	child->used_math = 1; 
}		

#endif /* __ASM_X86_64_I387_H */

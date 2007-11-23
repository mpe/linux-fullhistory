/*
 *  linux/arch/m68k/kernel/process.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/a.out.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/machdep.h>

asmlinkage void ret_from_exception(void);

/*
 * The idle loop on an m68k..
 */
asmlinkage int sys_idle(void)
{
	if (current->pid != 0)
		return -EPERM;

	/* endless idle loop with no priority at all */
	current->counter = -100;
	for (;;)
		schedule();
}

void hard_reset_now(void)
{
	if (mach_reset)
		mach_reset();
}

void show_regs(struct pt_regs * regs)
{
	printk("\n");
	printk("Format %02x  Vector: %04x  PC: %08lx  Status: %04x\n",
	       regs->format, regs->vector, regs->pc, regs->sr);
	printk("ORIG_D0: %08lx  D0: %08lx  A1: %08lx\n",
	       regs->orig_d0, regs->d0, regs->a1);
	printk("A0: %08lx  D5: %08lx  D4: %08lx\n",
	       regs->a0, regs->d5, regs->d4);
	printk("D3: %08lx  D2: %08lx  D1: %08lx\n",
	       regs->d3, regs->d2, regs->d1);
	if (!(regs->sr & PS_S))
		printk("USP: %08lx\n", rdusp());
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
}

void flush_thread(void)
{
	set_fs(USER_DS);
	current->tss.fs = USER_DS;
}

/*
 * "m68k_fork()".. By the time we get here, the
 * non-volatile registers have also been saved on the
 * stack. We do some ugly pointer stuff here.. (see
 * also copy_thread)
 */

asmlinkage int m68k_fork(struct pt_regs *regs)
{
	return do_fork(SIGCHLD, rdusp(), regs);
}

asmlinkage int m68k_clone(struct pt_regs *regs)
{
	unsigned long clone_flags;
	unsigned long newsp;

	/* syscall2 puts clone_flags in d1 and usp in d2 */
	clone_flags = regs->d1;
	newsp = regs->d2;
	if (!newsp)
	  newsp  = rdusp();
	return do_fork(clone_flags, newsp, regs);
}

void release_thread(struct task_struct *dead_task)
{
}

void copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
		 struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	struct switch_stack * childstack, *stack;
	unsigned long stack_offset, *retp;

	stack_offset = PAGE_SIZE - sizeof(struct pt_regs);
	childregs = (struct pt_regs *) (p->kernel_stack_page + stack_offset);

	*childregs = *regs;
	childregs->d0 = 0;

	retp = ((unsigned long *) regs);
	stack = ((struct switch_stack *) retp) - 1;

	childstack = ((struct switch_stack *) childregs) - 1;
	*childstack = *stack;
	childstack->retpc = (unsigned long) ret_from_exception;

	p->tss.usp = usp;
	p->tss.ksp = (unsigned long)childstack;

	/* Copy the current fpu state */
	asm volatile ("fsave %0" : : "m" (p->tss.fpstate[0]) : "memory");
	if (p->tss.fpstate[0])
	  asm volatile ("fmovemx %/fp0-%/fp7,%0\n\t"
			"fmoveml %/fpiar/%/fpcr/%/fpsr,%1"
			: : "m" (p->tss.fp[0]), "m" (p->tss.fpcntl[0])
			: "memory");
	/* Restore the state in case the fpu was busy */
	asm volatile ("frestore %0" : : "m" (p->tss.fpstate[0]));
}

/* Fill in the fpu structure for a core dump.  */

int dump_fpu (struct user_m68kfp_struct *fpu)
{
  char fpustate[216];

  /* First dump the fpu context to avoid protocol violation.  */
  asm volatile ("fsave %0" :: "m" (fpustate[0]) : "memory");
  if (!fpustate[0])
    return 0;

  asm volatile ("fmovem %/fpiar/%/fpcr/%/fpsr,%0"
		:: "m" (fpu->fpcntl[0])
		: "memory");
  asm volatile ("fmovemx %/fp0-%/fp7,%0"
		:: "m" (fpu->fpregs[0])
		: "memory");
  return 1;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
/* changed the size calculations - should hopefully work better. lbt */
	dump->magic = CMAGIC;
	dump->start_code = 0;
	dump->start_stack = rdusp() & ~(PAGE_SIZE - 1);
	dump->u_tsize = ((unsigned long) current->mm->end_code) >> PAGE_SHIFT;
	dump->u_dsize = ((unsigned long) (current->mm->brk +
					  (PAGE_SIZE-1))) >> PAGE_SHIFT;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;

	if (dump->start_stack < TASK_SIZE)
		dump->u_ssize = ((unsigned long) (TASK_SIZE - dump->start_stack)) >> PAGE_SHIFT;

	dump->u_ar0 = (struct pt_regs *)(((int)(&dump->regs)) -((int)(dump)));
	dump->regs = *regs;
	dump->regs2 = ((struct switch_stack *)regs)[-1];
	/* dump floating point stuff */
	dump->u_fpvalid = dump_fpu (&dump->m68kfp);
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(char *name, char **argv, char **envp)
{
	int error;
	char * filename;
	struct pt_regs *regs = (struct pt_regs *) &name;

	error = getname(name, &filename);
	if (error)
		return error;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
	return error;
}

/*
 *  linux/arch/i386/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
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
#include <linux/ldt.h>
#include <linux/user.h>
#include <linux/a.out.h>

#include <asm/segment.h>
#include <asm/system.h>

void ret_from_sys_call(void) { __asm__("nop"); }

/*
 * The idle loop on a i386..
 */
asmlinkage int sys_idle(void)
{
	int i;

	if (current->pid != 0)
		return -EPERM;

	/* Map out the low memory: it's no longer needed */
	/* Sparc version RSN */

	/* endless idle loop with no priority at all */
	current->counter = -100;
	for (;;) {
		if (!need_resched)
			__asm__("nop");
		schedule();
	}
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
void start_thread(struct pt_regs * regs, unsigned long sp, unsigned long fp)
{
	regs->sp = sp;
	regs->fp = fp;
	regs->psr = psr;
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
  return; /* i'm getting to it */
}

void flush_thread(void)
{
  return;
}

unsigned long copy_thread(int nr, unsigned long clone_flags, struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;

	childregs = ((struct pt_regs *) (p->kernel_stack_page + PAGE_SIZE)) - 1;
	p->tss.sp = (unsigned long) childregs;
	*childregs = *regs;
	p->tss.back_link = 0;
	p->tss.psr = regs->psr; /* for condition codes */
	return clone_flags;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
  return; /* solaris does this enough */
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(struct pt_regs regs)
{
	int error;
	char * filename;

	error = do_execve(filename, (char **) regs.reg_window[0], 
			  (char **) regs.reg_window[1], &regs);
	putname(filename);
	return error;
}

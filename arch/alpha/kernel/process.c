/*
 *  linux/arch/alpha/kernel/process.c
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
#include <asm/io.h>

asmlinkage int sys_idle(void)
{
	if (current->pid != 0)
		return -EPERM;

	/* endless idle loop with no priority at all */
	current->counter = -100;
	for (;;) {
		schedule();
	}
}

void hard_reset_now(void)
{
	halt();
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
void start_thread(struct pt_regs * regs, unsigned long pc, unsigned long sp)
{
	regs->pc = pc;
	wrusp(sp);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	halt();
}

void flush_thread(void)
{
	halt();
}

/*
 * This needs lots of work still..
 */
unsigned long copy_thread(int nr, unsigned long clone_flags, struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;

	p->tss.usp = rdusp();
	childregs = ((struct pt_regs *) (p->kernel_stack_page + PAGE_SIZE)) - 1;
	*childregs = *regs;
	p->tss.ksp = (unsigned long) childregs;
/*	p->tss.pc = XXXX; */
	halt();
	return clone_flags;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(struct pt_regs regs)
{
	halt();
	return 0;
}

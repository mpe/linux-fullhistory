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

void show_regs(struct pt_regs * regs)
{
	printk("\nPS: %04lx PC: %016lx\n", regs->ps, regs->pc);
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
 * This needs some work still..
 */
void copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;

	p->tss.usp = usp;
	childregs = ((struct pt_regs *) (p->kernel_stack_page + PAGE_SIZE)) - 1;
	*childregs = *regs;
	p->tss.ksp = (unsigned long) childregs;
/*	p->tss.pc = XXXX; */
	panic("copy_thread not implemented");
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
}

/*
 * sys_execve() executes a new program.
 *
 * This works due to the alpha calling sequence: the first 6 args
 * are gotten from registers, while the rest is on the stack, so
 * we get a0-a5 for free, and then magically find "struct pt_regs"
 * on the stack for us..
 *
 * Don't do this at home.
 */
asmlinkage int sys_execve(unsigned long a0, unsigned long a1, unsigned long a2,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct pt_regs regs)
{
	int error;
	char * filename;

	error = getname((char *) a0, &filename);
	if (error)
		return error;
	error = do_execve(filename, (char **) a1, (char **) a2, &regs);
	putname(filename);
	return error;
}

/*
 * sys_fork() does the obvious thing, but not the obvious way.
 * See sys_execve() above.
 */
asmlinkage int sys_fork(unsigned long a0, unsigned long a1, unsigned long a2,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct pt_regs regs)
{
	return do_fork(COPYVM | SIGCHLD, rdusp(), &regs);
}

asmlinkage int sys_clone(unsigned long a0, unsigned long a1, unsigned long a2,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct pt_regs regs)
{
	unsigned long clone_flags = a0;
	unsigned long newsp;

	newsp = rdusp();
	if (newsp == a1 || !a1)
		clone_flags |= COPYVM;
	else
		newsp = a1;	
	return do_fork(clone_flags, newsp, &regs);
}

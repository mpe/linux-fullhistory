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
	if (current->pid != 0)
		return -EPERM;

	/* Map out the low memory: it's no longer needed */
	/* Sparc version RSN */

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
        printk("\nSP: %08lx PC: %08lx NPC: %08lx\n", regs->sp, regs->pc,
	       regs->npc);
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
void start_thread(struct pt_regs * regs, unsigned long sp, unsigned long fp)
{
	regs->sp = sp;
	regs->fp = fp;
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

void copy_thread(int nr, unsigned long clone_flags, unsigned long sp, struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;

	childregs = ((struct pt_regs *) (p->kernel_stack_page + PAGE_SIZE)) - 1;
	p->tss.usp = (unsigned long) childregs;
	*childregs = *regs;
	childregs->sp = sp;
	p->tss.psr = regs->psr; /* for condition codes */
	return;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
  return; /* solaris does this enough */
}

asmlinkage int sys_fork(struct pt_regs regs)
{
	return do_fork(COPYVM | SIGCHLD, regs.sp, &regs);
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(struct pt_regs regs)
{
  halt();
  return 0;
}


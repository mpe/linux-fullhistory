/*
 *  linux/arch/sparc/kernel/process.c
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
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

#include <asm/oplib.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/processor.h>

/*
 * The idle loop on a sparc... ;)
 */
asmlinkage int sys_idle(void)
{

	if (current->pid != 0)
		return -EPERM;

	printk("in sys_idle...\n");
	/* Map out the low memory: it's no longer needed */
	/* Sparc version RSN */

	/* endless idle loop with no priority at all */
	current->counter = -100;
	for (;;) {
	  printk("calling schedule() aieee!\n");
	  schedule();
	  printk("schedule() returned, halting...\n");
	  halt();
	}
}

void hard_reset_now(void)
{
	prom_reboot("boot vmlinux");
}

void show_regs(struct pt_regs * regs)
{
        printk("\nFP: %08lx PC: %08lx NPC: %08lx\n", regs->u_regs[14],
	       regs->pc, regs->npc);
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

void release_thread(struct task_struct *dead_task)
{
  halt();
}

extern void ret_sys_call(void);

/*
 * Copy a Sparc thread.  The context of a process on the Sparc is
 * composed of the following:
 *  1) status registers  %psr (for condition codes + CWP) and %wim
 *  2) current register window (in's and global registers)
 *  3) the current live stack frame, it contains the register
 *     windows the child may 'restore' into, this is important
 *  4) kernel stack pointer, user stack pointer (which is %i6)
 *  5) The pc and npc the child returns to during a switch
 */

void copy_thread(int nr, unsigned long clone_flags, unsigned long sp,
		 struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs *childregs;
	unsigned char *old_stack;
	unsigned char *new_stack;
	int i;

	/* This process has no context yet. */
	p->tss.context = -1;

	/* Grrr, Sparc stack alignment restrictions make things difficult. */
	childregs = ((struct pt_regs *) 
		     ((p->kernel_stack_page + PAGE_SIZE - 80)&(~7)));

	*childregs = *regs;

	p->tss.usp = sp;    /* both processes have the same user stack */
	/* See entry.S */

	/* Allocate new processes kernel stack right under pt_regs.
	 * Hopefully this should align things the right way.
	 */
	p->tss.ksp = (unsigned long) ((p->kernel_stack_page + PAGE_SIZE - 80 - 96)&(~7));
	new_stack = (unsigned char *) (p->tss.ksp);
	old_stack = (unsigned char *) (((unsigned long) regs) - 96);

	/* Copy c-stack. */
	for(i=0; i<96; i++) *new_stack++ = *old_stack++;

	/* These pc values are only used when we switch to the child for
	 * the first time, it jumps the child to ret_sys_call in entry.S
	 * so that the child returns from the sys_call just like parent.
	 */
	p->tss.pc = (((unsigned long) ret_sys_call) - 8);
	p->tss.npc = p->tss.pc+4;

	/* Set the return values for both the parent and the child */
	regs->u_regs[8] = p->pid;
	childregs->u_regs[8] = 0;

	return;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
  return; /* solaris does this enough */
}

asmlinkage int sys_fork(struct pt_regs *regs)
{
  return do_fork(SIGCHLD, regs->u_regs[14], regs);
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(struct pt_regs regs)
{
  printk("sys_execve()... halting\n");
  halt();
  return 0;
}


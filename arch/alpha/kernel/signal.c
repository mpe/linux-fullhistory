/*
 *  linux/arch/alpha/kernel/signal.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>

#include <asm/segment.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

asmlinkage int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options);

/*
 * atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int sys_sigsuspend(int restart, unsigned long oldmask, unsigned long set)
{
	unsigned long mask;
	struct pt_regs * regs = (struct pt_regs *) &restart;

	halt();
	mask = current->blocked;
	current->blocked = set & _BLOCKABLE;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(mask,regs))
			return -EINTR;
	}
}

/*
 * this should do a signal return with the info on the stack..
 */
asmlinkage int sys_sigreturn(unsigned long __unused)
{
	halt();
	return 0;
}

/*
 * Set up a signal frame... I don't know what it should look like yet.
 */
void setup_frame(struct sigaction * sa, unsigned long ** fp, unsigned long pc,
	struct pt_regs * regs, int signr, unsigned long oldmask)
{
	halt();
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals that
 * the kernel can handle, and then we build all the user-level signal handling
 * stack-frames in one go after that.
 *
 * Not that any of this is actually implemented yet ;-)
 */
asmlinkage int do_signal(unsigned long oldmask, struct pt_regs * regs)
{
	halt();
	return 1;
}

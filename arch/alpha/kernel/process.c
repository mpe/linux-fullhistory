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
#include <linux/utsname.h>
#include <linux/time.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/mman.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>

asmlinkage int sys_sethae(unsigned long hae, unsigned long a1, unsigned long a2,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct pt_regs regs)
{
	(&regs)->hae = hae;
	return 0;
}

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
	printk("\nps: %04lx pc: %016lx\n", regs->ps, regs->pc);
	printk("rp: %016lx sp: %p\n", regs->r26, regs+1);
	printk(" r0: %016lx  r1: %016lx  r2: %016lx  r3: %016lx\n",
	       regs->r0, regs->r1, regs->r2, regs->r3);
	printk(" r4: %016lx  r5: %016lx  r6: %016lx  r7: %016lx\n",
	       regs->r4, regs->r5, regs->r6, regs->r7);
	printk(" r8: %016lx r16: %016lx r17: %016lx r18: %016lx\n",
	       regs->r8, regs->r16, regs->r17, regs->r18);
	printk("r19: %016lx r20: %016lx r21: %016lx r22: %016lx\n",
	       regs->r19, regs->r20, regs->r21, regs->r22);
	printk("r23: %016lx r24: %016lx r25: %016lx r26: %016lx\n",
	       regs->r23, regs->r24, regs->r25, regs->r26);
	printk("r27: %016lx r28: %016lx r29: %016lx hae: %016lx\n",
	       regs->r27, regs->r28, regs->gp, regs->hae);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
}

void flush_thread(void)
{
}

/*
 * "alpha_clone()".. By the time we get here, the
 * non-volatile registers have also been saved on the
 * stack. We do some ugly pointer stuff here.. (see
 * also copy_thread)
 *
 * Notice that "fork()" is implemented in terms of clone,
 * with parameters (SIGCHLD, 0).
 */
int alpha_clone(unsigned long clone_flags, unsigned long usp,
	struct switch_stack * swstack)
{
	if (!usp)
		usp = rdusp();
	return do_fork(clone_flags, usp, (struct pt_regs *) (swstack+1));
}

extern void ret_from_sys_call(void);
/*
 * Copy an alpha thread..
 *
 * Note the "stack_offset" stuff: when returning to kernel mode, we need
 * to have some extra stack-space for the kernel stack that still exists
 * after the "ret_from_sys_call". When returning to user mode, we only
 * want the space needed by the syscall stack frame (ie "struct pt_regs").
 * Use the passed "regs" pointer to determine how much space we need
 * for a kernel fork().
 */
void copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	struct switch_stack * childstack, *stack;
	unsigned long stack_offset;

	stack_offset = PAGE_SIZE - sizeof(struct pt_regs);
	if (!(regs->ps & 8))
		stack_offset = (PAGE_SIZE-1) & (unsigned long) regs;
	childregs = (struct pt_regs *) (p->kernel_stack_page + stack_offset);
		
	*childregs = *regs;
	childregs->r0 = 0;
	childregs->r19 = 0;
	childregs->r20 = 1;	/* OSF/1 has some strange fork() semantics.. */
	regs->r20 = 0;
	stack = ((struct switch_stack *) regs) - 1;
	childstack = ((struct switch_stack *) childregs) - 1;
	*childstack = *stack;
	childstack->r26 = (unsigned long) ret_from_sys_call;
	p->tss.usp = usp;
	p->tss.ksp = (unsigned long) childstack;
	p->tss.flags = 1;
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

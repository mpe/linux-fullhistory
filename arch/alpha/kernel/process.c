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
	printk("\nps: %04lx pc: %016lx\n", regs->ps, regs->pc);
	printk("rp: %04lx sp: %p\n", regs->r26, regs+1);
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

struct alpha_switch_stack {
	unsigned long r9;
	unsigned long r10;
	unsigned long r11;
	unsigned long r12;
	unsigned long r13;
	unsigned long r14;
	unsigned long r15;
	unsigned long r26;
};

/*
 * "alpha_switch_to()".. Done completely in assembly, due to the
 * fact that we obviously don't returns to the caller directly.
 * Also, we have to save the regs that the C compiler expects to be
 * saved across a function call.. (9-15)
 *
 * NOTE! The stack switches from under us when we do the swpctx call:
 * this *looks* like it restores the same registers that it just saved,
 * but it actually restores the new context regs and return address.
 */
__asm__(".align 3\n\t"
	".globl alpha_switch_to\n\t"
	".ent alpha_switch_to\n"
	"alpha_switch_to:\n\t"
	"subq $30,64,$30\n\t"
	"stq  $9,0($30)\n\t"
	"stq $10,8($30)\n\t"
	"stq $11,16($30)\n\t"
	"stq $12,24($30)\n\t"
	"stq $13,32($30)\n\t"
	"stq $14,40($30)\n\t"
	"stq $15,48($30)\n\t"
	"stq $26,56($30)\n\t"
	"call_pal 48\n\t"
	"ldq  $9,0($30)\n\t"
	"ldq $10,8($30)\n\t"
	"ldq $11,16($30)\n\t"
	"ldq $12,24($30)\n\t"
	"ldq $13,32($30)\n\t"
	"ldq $14,40($30)\n\t"
	"ldq $15,48($30)\n\t"
	"ldq $26,56($30)\n\t"
	"addq $30,64,$30\n\t"
	"ret $31,($26),1\n\t"
	".end alpha_switch_to");

/*
 * "alpha_fork()".. By the time we get here, the
 * non-volatile registers have also been saved on the
 * stack. We do some ugly pointer stuff here.. (see
 * also copy_thread)
 */
int alpha_fork(struct alpha_switch_stack * swstack)
{
	return do_fork(COPYVM | SIGCHLD, 0, (struct pt_regs *) (swstack+1));
}

/*
 * Copy an alpha thread..
 */
void copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	struct alpha_switch_stack * childstack, *stack;

	childregs = ((struct pt_regs *) (p->kernel_stack_page + PAGE_SIZE)) - 1;
	*childregs = *regs;
	childregs->r0 = 0;
	regs->r0 = p->pid;
	stack = ((struct alpha_switch_stack *) regs) - 1;
	childstack = ((struct alpha_switch_stack *) childregs) - 1;
	*childstack = *stack;
	p->tss.usp = usp;
	p->tss.ksp = (unsigned long) childstack;
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
 * This doesn't actually work correctly like this: we need to do the
 * same stack setups that fork() does first.
 */
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

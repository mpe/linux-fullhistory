/*
 *  linux/arch/ppc/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted for PowerPC by Gary Thomas
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

#include <asm/pgtable.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>

#include "ppc_machine.h"

int
dump_fpu()
{
	return (1);
}

void
switch_to(struct task_struct *prev, struct task_struct *new)
{
	struct pt_regs *regs;
	struct thread_struct *new_tss, *old_tss;
	int s = _disable_interrupts();
	regs = new->tss.ksp;
#if 0
	printk("Task %x(%d) -> %x(%d)", current, current->pid, new, new->pid);
	printk(" - IP: %x, SR: %x, SP: %x\n", regs->nip, regs->msr, regs);
	cnpause();
#endif
	new_tss = &new->tss;
	old_tss = &current->tss;
	current_set[0] = new;	/* FIX ME! */
	_switch(old_tss, new_tss);
#if 0
	printk("Back in task %x(%d)\n", current, current->pid);
#endif
	_enable_interrupts(s);
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
	_panic("show_regs");
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

void
release_thread(struct task_struct *t)
{
}

/*
 * Copy a thread..
 */
void copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	struct task_struct * p, struct pt_regs * regs)
{
	int i;
	SEGREG *segs;
	struct pt_regs * childregs;
#if 0
printk("copy thread - NR: %d, Flags: %x, USP: %x, Task: %x, Regs: %x\n", nr, clone_flags, usp, p, regs);
cnpause();
#endif
	/* Construct segment registers */
	segs = p->tss.segs;
	for (i = 0;  i < 8;  i++)
	{
		segs[i].ks = 0;
		segs[i].kp = 1;
		segs[i].vsid = i | (nr << 4);
	}
	if ((p->mm->context == 0) || (p->mm->count == 1))
	{
		p->mm->context = (nr<<4);
#if 0		
printk("Setting MM[%x] Context = %x Task = %x Current = %x/%x\n", p->mm, p->mm->context, p, current, current->mm);
cnpause();
#endif
	}
	/* Last 8 are shared with kernel & everybody else... */
	for (i = 8;  i < 16;  i++)
	{
		segs[i].ks = 0;
		segs[i].kp = 1;
		segs[i].vsid = i;
	}
	/* Copy registers */
#ifdef STACK_HAS_TWO_PAGES
	childregs = ((struct pt_regs *) (p->kernel_stack_page + 2*PAGE_SIZE)) - 2;
#else	
	childregs = ((struct pt_regs *) (p->kernel_stack_page + 1*PAGE_SIZE)) - 2;
#endif	
	*childregs = *regs;	/* STRUCT COPY */
	childregs->gpr[3] = 0;  /* Result from fork() */
	p->tss.ksp = childregs;
	if (usp >= (unsigned long)regs)
	{ /* Stack is in kernel space - must adjust */
		childregs->gpr[1] = childregs+1;
	} else
	{ /* Provided stack is in user space */
		childregs->gpr[1] = usp;
	}
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
}

#if 0
/*
 * Do necessary setup to start up a newly executed thread.
 */
void start_thread(struct pt_regs * regs, unsigned long eip, unsigned long esp)
{
	regs->nip = eip;
	regs->gpr[1] = esp;
	regs->msr = MSR_USER;
#if 0
{
	int len;
	len = (unsigned long)0x80000000 - esp;
	if (len > 128) len = 128;
	printk("Start thread [%x] at PC: %x, SR: %x, SP: %x\n", regs, eip, regs->msr, esp);
	dump_buf(esp, len);
	dump_buf(eip, 0x80);
	cnpause();
}
#endif	
}
#endif

asmlinkage int sys_fork(int p1, int p2, int p3, int p4, int p5, int p6, struct pt_regs *regs)
{
	return do_fork(SIGCHLD, regs->gpr[1], regs);
}

/*
 * sys_execve() executes a new program.
 *
 * This works due to the PowerPC calling sequence: the first 6 args
 * are gotten from registers, while the rest is on the stack, so
 * we get a0-a5 for free, and then magically find "struct pt_regs"
 * on the stack for us..
 *
 * Don't do this at home.
 */
asmlinkage int sys_execve(unsigned long a0, unsigned long a1, unsigned long a2,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct pt_regs *regs)
{
	int error;
	char * filename;

	error = getname((char *) a0, &filename);
	if (error)
	{
printk("Error getting EXEC name: %d\n", error);		
		return error;
	}
	flush_instruction_cache();
	error = do_execve(filename, (char **) a1, (char **) a2, regs);
#if 0
if (error)
{	
printk("EXECVE - file = '%s', error = %d\n", filename, error);
}
#endif
	putname(filename);
	return error;
}

/*
 * This doesn't actually work correctly like this: we need to do the
 * same stack setups that fork() does first.
 */
asmlinkage int sys_clone(int p1, int p2, int p3, int p4, int p5, int p6, struct pt_regs *regs)
{
	unsigned long clone_flags = p1;
	int res;
	res = do_fork(clone_flags, regs->gpr[1], regs);
	return res;
}

void
print_backtrace(unsigned long *sp)
{
	int cnt = 0;
	printk("... Call backtrace:\n");
	while (*sp)
	{
		printk("%08X ", sp[1]);
		sp = *sp;
		if (++cnt == 8)
		{
			printk("\n");
		}
		if (cnt > 16) break;
	}
	printk("\n");
}

void
print_user_backtrace(unsigned long *sp)
{
	int cnt = 0;
	printk("... [User] Call backtrace:\n");
	while (valid_addr(sp) && *sp)
	{
		printk("%08X ", sp[1]);
		sp = *sp;
		if (++cnt == 8)
		{
			printk("\n");
		}
		if (cnt > 16) break;
	}
	printk("\n");
}

void
print_kernel_backtrace(void)
{
	unsigned long *_get_SP(void);
	print_backtrace(_get_SP());
}

/* * Last edited: Dec 14 17:32 1995 (cort) */
/*
 *  linux/arch/ppc/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted for PowerPC by Gary Thomas
 *  Modified by Cort Dougan
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

#include <asm/ppc_machine.h>

int dump_fpu (struct user_i387_struct* fpu)
{
  return 1;		/* all ppc's have a fpu */
}

void
switch_to(struct task_struct *new)
{
	struct pt_regs *regs;
	struct thread_struct *new_tss, *old_tss;
	int s;
	regs = (struct pt_regs *)new->tss.ksp;
/*
printk("Task %x(%d) -> %x(%d)", current, current->pid, new, new->pid);
printk(" - IP: %x, SR: %x, SP: %x\n", regs->nip, regs->msr, regs);
*/
	s = _disable_interrupts();
	new_tss = &new->tss;
	old_tss = &current->tss;
	current = new;
	_switch(old_tss, new_tss);

/*	printk("Back in task %x(%d)\n", current, current->pid);*/

	_enable_interrupts(s);
}

asmlinkage int sys_idle(void)
{

	if (current->pid != 0)
		return -EPERM;
/*panic("process.c: sys_idle()\n");*/
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

/*
 * Copy a thread..
 */
void copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	struct task_struct * p, struct pt_regs * regs)
{
	int i;
	SEGREG *segs;
	struct pt_regs * childregs;

/*printk("copy thread - NR: %d, Flags: %x, USP: %x, Task: %x, Regs: %x\n", nr, clone_flags, usp, p, regs);*/

	/* Construct segment registers */
	segs = (SEGREG *)p->tss.segs;
	for (i = 0;  i < 8;  i++)
	{
		segs[i].ks = 0;
		segs[i].kp = 1;
		segs[i].vsid = i | (nr << 4);
	}
	/* Last 8 are shared with kernel & everybody else... */
	for (i = 8;  i < 16;  i++)
	{
		segs[i].ks = 0;
		segs[i].kp = 1;
		segs[i].vsid = i;
	}
	/* Copy registers */
	childregs = ((struct pt_regs *) (p->kernel_stack_page + 2*PAGE_SIZE)) - 2;
	*childregs = *regs;	/* STRUCT COPY */
	childregs->gpr[3] = 0;  /* Result from fork() */
	p->tss.ksp = (unsigned long)childregs;
	if (usp >= (unsigned long)regs)
	{ /* Stack is in kernel space - must adjust */
		childregs->gpr[1] = (long)(childregs+1);
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

#if 0 /* mfisk */
/*
 * Do necessary setup to start up a newly executed thread.
 */
void start_thread(struct pt_regs * regs, unsigned long eip, unsigned long esp)
{
  regs->nip = eip;
  regs->gpr[1] = esp;
  regs->msr = MSR_USER;
#if 0
/*  printk("current = %x current->mm = %x\n", current, current->mm);
  printk("task[0] = %x task[0]->mm = %x\n", task[0],task[0]->mm);*/
  printk("Start thread [%x] at PC: %x, SR: %x, SP: %x\n",
    regs, eip, regs->msr, esp);
/*  dump_buf(esp, 64);*/
/*  dump_buf(eip, 64);*/
#endif
}
#endif

asmlinkage int sys_newselect(int p1, int p2, int p3, int p4, int p5, int p6, struct pt_regs *regs)
{
  panic("sys_newselect unimplemented");
}

asmlinkage int sys_fork(int p1, int p2, int p3, int p4, int p5, int p6, struct pt_regs *regs)
{
  int i;
  char *a;
#if 0
  for ( i = 0 ; i <= 0x400 ; i++)
  {
    printk("going to do kmalloc(%d)\n",i);
    a = kmalloc(i,GFP_KERNEL);
    a = kmalloc(i,GFP_KERNEL);
    printk("a = %x\n",a);
  }
#endif
  return do_fork( SIGCHLD, regs->gpr[1], regs);
}

asmlinkage int sys_execve(unsigned long a0, unsigned long a1, unsigned long a2,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct pt_regs *regs)
{
	int error;
	char * filename;

#if 1
	/* paranoia check.  I really don't trust head.S  -- Cort */
	if ( regs->marker != 0xDEADDEAD )
	{
	  panic("process.c: sys_execve(): regs->marker != DEADDEAD\n");
	}
#endif
	error = getname((char *) a0, &filename);
	if (error)
	  return error;
	error = do_execve(filename, (char **) a1, (char **) a2, regs);

	putname(filename);
	return error;
}


asmlinkage int sys_clone(unsigned long clone_flags, unsigned long usp, unsigned long a2,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct pt_regs *regs)
{
  int i;
  
  if (!usp)
    usp = regs->gpr[1];
  

  /* I hard coded in all the arguments to clone since clone() is inlined
     and has trouble with its args  with our gcc -- Cort*/
  return do_fork(/*clone_flags*/CLONE_VM, /*usp*/ regs->gpr[1], regs);
}



void
print_backtrace(void)
{
	unsigned long *sp = (unsigned long *)_get_SP();
	int cnt = 0;
	printk("... Call backtrace:\n");
	while (*sp)
	{
		printk("%08X ", sp[2]);
		sp = (unsigned long *)*sp;
		if (++cnt == 8)
		{
			printk("\n");
		}
		if (cnt > 16) break;
	}
	printk("\n");
}


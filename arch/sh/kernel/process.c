/*
 *  linux/arch/sh/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  SuperH version:  Copyright (C) 1999  Niibe Yutaka
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#define __KERNEL_SYSCALLS__
#include <stdarg.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/unistd.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>
#include <asm/elf.h>

#include <linux/irq.h>

static int hlt_counter=0;

#define HARD_IDLE_TIMEOUT (HZ / 3)

void disable_hlt(void)
{
	hlt_counter++;
}

void enable_hlt(void)
{
	hlt_counter--;
}

/*
 * The idle loop on a uniprocessor i386..
 */ 
void cpu_idle(void *unused)
{
	/* endless idle loop with no priority at all */
	init_idle();
	current->priority = 0;
	current->counter = -100;

	while (1) {
		while (!current->need_resched) {
			if (hlt_counter)
				continue;
			__sti();
			asm volatile("sleep" : : : "memory");
		}
		schedule();
		check_pgt_cache();
	}
}

void machine_restart(char * __unused)
{ /* Need to set MMU_TTB?? */
}

void machine_halt(void)
{
}

void machine_power_off(void)
{
}

void show_regs(struct pt_regs * regs)
{
	printk("\n");
	printk("PC: [<%08lx>]", regs->pc);
	printk(" SP: %08lx", regs->u_regs[UREG_SP]);
	printk(" SR: %08lx\n", regs->sr);
	printk("R0 : %08lx R1 : %08lx R2 : %08lx R3 : %08lx\n",
	       regs->u_regs[0],regs->u_regs[1],
	       regs->u_regs[2],regs->u_regs[3]);
	printk("R4 : %08lx R5 : %08lx R6 : %08lx R7 : %08lx\n",
	       regs->u_regs[4],regs->u_regs[5],
	       regs->u_regs[6],regs->u_regs[7]);
	printk("R8 : %08lx R9 : %08lx R10: %08lx R11: %08lx\n",
	       regs->u_regs[8],regs->u_regs[9],
	       regs->u_regs[10],regs->u_regs[11]);
	printk("R12: %08lx R13: %08lx R14: %08lx\n",
	       regs->u_regs[12],regs->u_regs[13],
	       regs->u_regs[14]);
	printk("MACH: %08lx MACL: %08lx GBR: %08lx PR: %08lx",
	       regs->mach, regs->macl, regs->gbr, regs->pr);
}

struct task_struct * alloc_task_struct(void)
{
	/* Get two pages */
	return (struct task_struct *) __get_free_pages(GFP_KERNEL,1);
}

void free_task_struct(struct task_struct *p)
{
	free_pages((unsigned long) p, 1);
}

/*
 * Create a kernel thread
 */

/*
 * This is the mechanism for creating a new kernel thread.
 *
 * NOTE! Only a kernel-only process(ie the swapper or direct descendants
 * who haven't done an "execve()") should use this: it will work within
 * a system call from a "real" process, but the process memory space will
 * not be free'd until both the parent and the child have exited.
 */
int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{	/* Don't use this in BL=1(cli).  Or else, CPU resets! */
	register unsigned long __sc0 __asm__ ("r0") = __NR_clone;
	register unsigned long __sc4 __asm__ ("r4") = (long) flags | CLONE_VM;
	register unsigned long __sc5 __asm__ ("r5") = 0;
	register unsigned long __sc8 __asm__ ("r8") = (long) arg;
	register unsigned long __sc9 __asm__ ("r9") = (long) fn;
	__asm__ __volatile__(
		"trapa	#0\n\t" 	/* Linux/SH system call */
		"tst	#0xff,r0\n\t"	/* child or parent? */
		"bf	1f\n\t"		/* parent - jump */
		"jsr	@r9\n\t"	/* call fn */
		" mov	r8,r4\n\t"	/* push argument */
		"mov	r0,r4\n\t"	/* return value to arg of exit */
		"mov	%2,r0\n\t"	/* exit */
		"trapa	#0\n"
		"1:"
		:"=z" (__sc0)
		:"0" (__sc0), "i" (__NR_exit),
		 "r" (__sc4), "r" (__sc5), "r" (__sc8), "r" (__sc9)
		:"memory");
	return __sc0;
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	/* nothing to do ... */
}

void flush_thread(void)
{
	/* do nothing */
	/* Possibly, set clear debug registers */
}

void release_thread(struct task_struct *dead_task)
{
	/* do nothing */
}

/* Fill in the fpu structure for a core dump.. */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *r)
{
	return 0; /* Task didn't use the fpu at all. */
}

asmlinkage void ret_from_fork(void);

int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
		struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs;

	childregs = ((struct pt_regs *)(THREAD_SIZE + (unsigned long) p)) - 1;

	*childregs = *regs;
	if (user_mode(regs)) {
		childregs->u_regs[UREG_SP] = usp;
	} else {
		childregs->u_regs[UREG_SP] = (unsigned long)p+2*PAGE_SIZE;
	}
	childregs->u_regs[0] = 0; /* Set return value for child */

	p->thread.sp = (unsigned long) childregs;
	p->thread.pc = (unsigned long) ret_from_fork;
	if (p->mm)
		p->mm->context = NO_CONTEXT;

	return 0;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
/* changed the size calculations - should hopefully work better. lbt */
	dump->magic = CMAGIC;
	dump->start_code = 0;
	dump->start_stack = regs->u_regs[UREG_SP] & ~(PAGE_SIZE - 1);
	dump->u_tsize = ((unsigned long) current->mm->end_code) >> PAGE_SHIFT;
	dump->u_dsize = ((unsigned long) (current->mm->brk + (PAGE_SIZE-1))) >> PAGE_SHIFT;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;
	/* Debug registers will come here. */

	if (dump->start_stack < TASK_SIZE)
		dump->u_ssize = ((unsigned long) (TASK_SIZE - dump->start_stack)) >> PAGE_SHIFT;

	dump->regs = *regs;
}

/*
 *	switch_to(x,y) should switch tasks from x to y.
 *
 */
void __switch_to(struct task_struct *prev, struct task_struct *next)
{
	/*
	 * Restore the kernel stack onto kernel mode register
	 *   	k4 (r4_bank1)
	 */
	asm volatile("ldc	%0,r4_bank"
		     : /* no output */
		     :"r" ((unsigned long)next+8192));
}

asmlinkage int sys_fork(unsigned long r4, unsigned long r5,
			unsigned long r6, unsigned long r7,
			struct pt_regs regs)
{
	return do_fork(SIGCHLD, regs.u_regs[UREG_SP], &regs);
}

asmlinkage int sys_clone(unsigned long clone_flags, unsigned long newsp,
			 unsigned long r6, unsigned long r7,
			 struct pt_regs regs)
{
	if (!newsp)
		newsp = regs.u_regs[UREG_SP];
	return do_fork(clone_flags, newsp, &regs);
}

/*
 * This is trivial, and on the face of it looks like it
 * could equally well be done in user mode.
 *
 * Not so, for quite unobvious reasons - register pressure.
 * In user mode vfork() cannot have a stack frame, and if
 * done by calling the "clone()" system call directly, you
 * do not have enough call-clobbered registers to hold all
 * the information you need.
 */
asmlinkage int sys_vfork(unsigned long r4, unsigned long r5,
			 unsigned long r6, unsigned long r7,
			 struct pt_regs regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD,
		       regs.u_regs[UREG_SP], &regs);
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(char *ufilename, char **uargv,
			  char **uenvp, unsigned long r7,
			  struct pt_regs regs)
{
	int error;
	char *filename;

	lock_kernel();
	filename = getname(ufilename);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, uargv, uenvp, &regs);
	if (error == 0)
		current->flags &= ~PF_DTRACE;
	putname(filename);
out:
	unlock_kernel();
	return error;
}

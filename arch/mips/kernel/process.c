/*
 *  linux/arch/mips/kernel/process.c
 *
 *  Copyright (C) 1995  Waldorf Electronics,
 *  written by Ralf Baechle
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
#include <asm/mipsregs.h>
#include <asm/mipsconfig.h>
#include <asm/stackframe.h>

asmlinkage void ret_from_sys_call(void) __asm__("ret_from_sys_call");

/*
 * The idle loop on a MIPS..
 */
asmlinkage int sys_idle(void)
{
#if 0
	int i;
#endif

	if (current->pid != 0)
		return -EPERM;

#if 0
	/* Map out the low memory: it's no longer needed */
	for (i = 0 ; i < 512 ; i++)
		pgd_clear(swapper_pg_dir + i);
#endif

	/* endless idle loop with no priority at all */
	current->counter = -100;
	for (;;) {
		/*
		 * R4[26]00 have wait, R4[04]00 don't.
		 */
		if (wait_available && !need_resched)
			__asm__("wait");
		schedule();
	}
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
void start_thread(struct pt_regs * regs, unsigned long eip, unsigned long esp)
{
	regs->cp0_epc = eip;
	regs->reg29 = esp;
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	/*
	 * Nothing to do
	 */
}

void flush_thread(void)
{
	/*
	 * Nothing to do
	 */
}

#define IS_CLONE (regs->orig_reg2 == __NR_clone)

unsigned long copy_thread(int nr, unsigned long clone_flags, struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;

	/*
	 * set up new TSS
	 */
	p->tss.fs = KERNEL_DS;
	p->tss.ksp = (p->kernel_stack_page + PAGE_SIZE - 4) | KSEG0;
	childregs = ((struct pt_regs *) (p->kernel_stack_page + PAGE_SIZE)) - 1;
	p->tss.reg29 = ((unsigned long) childregs) | KSEG0; /* new sp */
	p->tss.reg31 = (unsigned long) ret_from_sys_call;
	*childregs = *regs;
	childregs->reg2 = 0;

	/*
	 * New tasks loose permission to use the fpu. This accelerates context
	 * switching for non fp programs, which true for the most programs.
	 */
	p->tss.cp0_status = regs->cp0_status &
	                    ~(ST0_CU1|ST0_CU0|ST0_KSU|ST0_ERL|ST0_EXL);
	childregs->cp0_status &= ~(ST0_CU1|ST0_CU0);

	if (IS_CLONE) {
		if (regs->reg4)
			childregs->reg29 = regs->reg4;
		clone_flags = regs->reg5;
		if (childregs->reg29 == regs->reg29)
			clone_flags |= COPYVM;
	}

	return clone_flags;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
	/*
	 * Not ready yet
	 */
#if 0
	int i;

/* changed the size calculations - should hopefully work better. lbt */
	dump->magic = CMAGIC;
	dump->start_code = 0;
	dump->start_stack = regs->esp & ~(PAGE_SIZE - 1);
	dump->u_tsize = ((unsigned long) current->mm->end_code) >> 12;
	dump->u_dsize = ((unsigned long) (current->mm->brk + (PAGE_SIZE-1))) >> 12;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;
	for (i = 0; i < 8; i++)
		dump->u_debugreg[i] = current->debugreg[i];  

	if (dump->start_stack < TASK_SIZE)
		dump->u_ssize = ((unsigned long) (TASK_SIZE - dump->start_stack)) >> 12;

	dump->regs = *regs;

/* Flag indicating the math stuff is valid. We don't support this for the
   soft-float routines yet */
	if (hard_math) {
		if ((dump->u_fpvalid = current->used_math) != 0) {
			if (last_task_used_math == current)
				__asm__("clts ; fnsave %0": :"m" (dump->i387));
			else
				memcpy(&dump->i387,&current->tss.i387.hard,sizeof(dump->i387));
		}
	} else {
		/* we should dump the emulator state here, but we need to
		   convert it into standard 387 format first.. */
		dump->u_fpvalid = 0;
	}
#endif
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(struct pt_regs regs)
{
	int error;
	char * filename;

	error = getname((char *) regs.reg4, &filename);
	if (error)
		return error;
	error = do_execve(filename, (char **) regs.reg5, (char **) regs.reg6, &regs);
	putname(filename);
	return error;
}

/*  $Id: process.c,v 1.49 1996/04/20 07:37:20 davem Exp $
 *  linux/arch/sparc/kernel/process.c
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
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
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/ldt.h>
#include <linux/user.h>
#include <linux/a.out.h>

#include <asm/auxio.h>
#include <asm/oplib.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/delay.h>
#include <asm/processor.h>
#include <asm/psr.h>
#include <asm/system.h>

extern void fpsave(unsigned long *, unsigned long *, void *, unsigned long *);

int active_ds = USER_DS;

#ifndef __SMP__

/*
 * the idle loop on a Sparc... ;)
 */
asmlinkage int sys_idle(void)
{
	if (current->pid != 0)
		return -EPERM;

	/* endless idle loop with no priority at all */
	current->counter = -100;
	for (;;) {
		schedule();
	}
	return 0;
}

#else

/*
 * the idle loop on a SparcMultiPenguin...
 */
asmlinkage int sys_idle(void)
{
	if (current->pid != 0)
		return -EPERM;

	/* endless idle loop with no priority at all */
	current->counter = -100;
	schedule();
	return 0;
}

/* This is being executed in task 0 'user space'. */
int cpu_idle(void *unused)
{
	volatile int *spap = &smp_process_available;
	volatile int cval;

	current->priority = -50;
	while(1) {
                if(0==read_smp_counter(spap))
                	continue;
		while(*spap == -1)
			;
		cli();
		/* Acquire exclusive access. */
		while((cval = smp_swap(spap, -1)) == -1)
			;
                if (0==cval) {
			/* ho hum, release it. */
			smp_process_available = 0;
			sti();
                        continue;
                }
		/* Something interesting happened, whee... */
		smp_swap(spap, (cval - 1));
		sti();
		idle();
	}
}

#endif

extern char saved_command_line[];

void hard_reset_now(void)
{
	sti();
	udelay(8000);
	cli();
	prom_feval("reset");
	panic("Reboot failed!");
}

void show_regwindow(struct reg_window *rw)
{
	printk("l0:%08lx l1:%08lx l2:%08lx l3:%08lx l4:%08lx l5:%08lx l6:%08lx l7:%08lx\n",
	       rw->locals[0], rw->locals[1], rw->locals[2], rw->locals[3],
	       rw->locals[4], rw->locals[5], rw->locals[6], rw->locals[7]);
	printk("i0:%08lx i1:%08lx i2:%08lx i3:%08lx i4:%08lx i5:%08lx i6:%08lx i7:%08lx\n",
	       rw->ins[0], rw->ins[1], rw->ins[2], rw->ins[3],
	       rw->ins[4], rw->ins[5], rw->ins[6], rw->ins[7]);
}

void show_regs(struct pt_regs * regs)
{
        printk("PSR: %08lx PC: %08lx NPC: %08lx Y: %08lx\n", regs->psr,
	       regs->pc, regs->npc, regs->y);
	printk("%%g0: %08lx %%g1: %08lx %%g2: %08lx %%g3: %08lx\n",
	       regs->u_regs[0], regs->u_regs[1], regs->u_regs[2],
	       regs->u_regs[3]);
	printk("%%g4: %08lx %%g5: %08lx %%g6: %08lx %%g7: %08lx\n",
	       regs->u_regs[4], regs->u_regs[5], regs->u_regs[6],
	       regs->u_regs[7]);
	printk("%%o0: %08lx %%o1: %08lx %%o2: %08lx %%o3: %08lx\n",
	       regs->u_regs[8], regs->u_regs[9], regs->u_regs[10],
	       regs->u_regs[11]);
	printk("%%o4: %08lx %%o5: %08lx %%sp: %08lx %%ret_pc: %08lx\n",
	       regs->u_regs[12], regs->u_regs[13], regs->u_regs[14],
	       regs->u_regs[15]);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	flush_user_windows();
#ifndef __SMP__
	if(last_task_used_math == current) {
#else
	if(current->flags & PF_USEDFPU) {
#endif
		/* Keep process from leaving FPU in a bogon state. */
		put_psr(get_psr() | PSR_EF);
		fpsave(&current->tss.float_regs[0], &current->tss.fsr,
		       &current->tss.fpqueue[0], &current->tss.fpqdepth);
#ifndef __SMP__
		last_task_used_math = NULL;
#else
		current->flags &= ~PF_USEDFPU;
#endif
	}
	mmu_exit_hook();
}

/*
 * Free old dead task when we know it can never be on the cpu again.
 */
void release_thread(struct task_struct *dead_task)
{
}

void flush_thread(void)
{
	/* Make sure old user windows don't get in the way. */
	flush_user_windows();
	current->tss.w_saved = 0;
	current->tss.uwinmask = 0;
	current->tss.sig_address = 0;
	current->tss.sig_desc = 0;
	current->tss.sstk_info.cur_status = 0;
	current->tss.sstk_info.the_stack = 0;

#ifndef __SMP__
	if(last_task_used_math == current) {
#else
	if(current->flags & PF_USEDFPU) {
#endif
		/* Clean the fpu. */
		put_psr(get_psr() | PSR_EF);
		fpsave(&current->tss.float_regs[0], &current->tss.fsr,
		       &current->tss.fpqueue[0], &current->tss.fpqdepth);
#ifndef __SMP__
		last_task_used_math = NULL;
#else
		current->flags &= ~PF_USEDFPU;
#endif
	}

	memset(&current->tss.reg_window[0], 0,
	       (sizeof(struct reg_window) * NSWINS));
	memset(&current->tss.rwbuf_stkptrs[0], 0,
	       (sizeof(unsigned long) * NSWINS));
	mmu_flush_hook();
	/* Now, this task is no longer a kernel thread. */
	current->tss.flags &= ~SPARC_FLAG_KTHREAD;
}

/*
 * Copy a Sparc thread.  The fork() return value conventions
 * under SunOS are nothing short of bletcherous:
 * Parent -->  %o0 == childs  pid, %o1 == 0
 * Child  -->  %o0 == parents pid, %o1 == 1
 *
 * NOTE: We have a separate fork kpsr/kwim because
 *       the parent could change these values between
 *       sys_fork invocation and when we reach here
 *       if the parent should sleep while trying to
 *       allocate the task_struct and kernel stack in
 *       do_fork().
 */
extern void ret_sys_call(void);

void copy_thread(int nr, unsigned long clone_flags, unsigned long sp,
		 struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs;
	struct reg_window *old_stack, *new_stack;
	unsigned long stack_offset;

#ifndef __SMP__
	if(last_task_used_math == current) {
#else
	if(current->flags & PF_USEDFPU) {
#endif
		put_psr(get_psr() | PSR_EF);
		fpsave(&p->tss.float_regs[0], &p->tss.fsr,
		       &p->tss.fpqueue[0], &p->tss.fpqdepth);
#ifdef __SMP__
		current->flags &= ~PF_USEDFPU;
#endif
	}

	/* Calculate offset to stack_frame & pt_regs */
	if(sparc_cpu_model == sun4c)
		stack_offset = ((PAGE_SIZE*3) - TRACEREG_SZ);
	else
		stack_offset = ((PAGE_SIZE<<2) - TRACEREG_SZ);

	if(regs->psr & PSR_PS)
		stack_offset -= REGWIN_SZ;
	childregs = ((struct pt_regs *) (p->kernel_stack_page + stack_offset));
	*childregs = *regs;
	new_stack = (((struct reg_window *) childregs) - 1);
	old_stack = (((struct reg_window *) regs) - 1);
	*new_stack = *old_stack;
	p->tss.ksp = p->saved_kernel_stack = (unsigned long) new_stack;
	p->tss.kpc = (((unsigned long) ret_sys_call) - 0x8);
	p->tss.kpsr = current->tss.fork_kpsr;
	p->tss.kwim = current->tss.fork_kwim;
	p->tss.kregs = childregs;
	childregs->u_regs[UREG_FP] = sp;

	if(regs->psr & PSR_PS) {
		stack_offset += TRACEREG_SZ;
		childregs->u_regs[UREG_FP] = p->kernel_stack_page + stack_offset;
		p->tss.flags |= SPARC_FLAG_KTHREAD;
	} else
		p->tss.flags &= ~SPARC_FLAG_KTHREAD;

	/* Set the return value for the child. */
	childregs->u_regs[UREG_I0] = current->pid;
	childregs->u_regs[UREG_I1] = 1;

	/* Set the return value for the parent. */
	regs->u_regs[UREG_I1] = 0;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
	unsigned long first_stack_page;

	dump->magic = SUNOS_CORE_MAGIC;
	dump->len = sizeof(struct user);
	dump->regs.psr = regs->psr;
	dump->regs.pc = regs->pc;
	dump->regs.npc = regs->npc;
	dump->regs.y = regs->y;
	/* fuck me plenty */
	memcpy(&dump->regs.regs[0], &regs->u_regs[1], (sizeof(unsigned long) * 15));
	dump->uexec = current->tss.core_exec;
	dump->u_tsize = (((unsigned long) current->mm->end_code) -
		((unsigned long) current->mm->start_code)) & ~(PAGE_SIZE - 1);
	dump->u_dsize = ((unsigned long) (current->mm->brk + (PAGE_SIZE-1)));
	dump->u_dsize -= dump->u_tsize;
	dump->u_dsize &= ~(PAGE_SIZE - 1);
	first_stack_page = (regs->u_regs[UREG_FP] & ~(PAGE_SIZE - 1));
	dump->u_ssize = (TASK_SIZE - first_stack_page) & ~(PAGE_SIZE - 1);
	memcpy(&dump->fpu.fpstatus.fregs.regs[0], &current->tss.float_regs[0], (sizeof(unsigned long) * 32));
	dump->fpu.fpstatus.fsr = current->tss.fsr;
	dump->fpu.fpstatus.flags = dump->fpu.fpstatus.extra = 0;
	dump->fpu.fpstatus.fpq_count = current->tss.fpqdepth;
	memcpy(&dump->fpu.fpstatus.fpq[0], &current->tss.fpqueue[0],
	       ((sizeof(unsigned long) * 2) * 16));
	dump->sigcode = current->tss.sig_desc;
}

/*
 * fill in the fpu structure for a core dump.
 */
int dump_fpu (void *fpu_structure)
{
	/* Currently we report that we couldn't dump the fpu structure */
	return 0;
}

/*
 * sparc_execve() executes a new program after the asm stub has set
 * things up for us.  This should basically do what I want it to.
 */
asmlinkage int sparc_execve(struct pt_regs *regs)
{
	int error;
	char *filename;

	flush_user_windows();
	error = getname((char *) regs->u_regs[UREG_I0], &filename);
	if(error)
		return error;
	error = do_execve(filename, (char **) regs->u_regs[UREG_I1],
			  (char **) regs->u_regs[UREG_I2], regs);
	putname(filename);
	return error;
}

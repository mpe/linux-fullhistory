/*  $Id: process.c,v 1.29 1995/11/25 00:58:17 davem Exp $
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

#include <asm/auxio.h>
#include <asm/oplib.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/psr.h>

int current_user_segment = USER_DS; /* the return value from get_fs */

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
}

void hard_reset_now(void)
{
	prom_halt();
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
	if(last_task_used_math == current)
		last_task_used_math = NULL;
	mmu_exit_hook(current);
}

/*
 * Free old dead task when we know it can never be on the cpu again.
 */
void release_thread(struct task_struct *dead_task)
{
	mmu_release_hook(dead_task);
}

void flush_thread(void)
{
	/* Make sure old user windows don't get in the way. */
	mmu_flush_hook(current);
	flush_user_windows();
	current->signal &= ~(1<<(SIGILL-1));
	current->tss.w_saved = 0;
	current->tss.uwinmask = 0;

	current->tss.sig_address = 0;
	current->tss.sig_desc = 0;

	/* Signal stack state does not inherit. XXX Really? XXX */
	current->tss.sstk_info.cur_status = 0;
	current->tss.sstk_info.the_stack = 0;

	memset(&current->tss.reg_window[0], 0,
	       (sizeof(struct reg_window) * NSWINS));
	memset(&current->tss.rwbuf_stkptrs[0], 0,
	       (sizeof(unsigned long) * NSWINS));
}

/*
 * Copy a Sparc thread.  The fork() return value conventions
 * under SunOS are nothing short of bletcherous:
 * Parent -->  %o0 == childs  pid, %o1 == 0
 * Child  -->  %o0 == parents pid, %o1 == 1
 *
 * I'm feeling sick...
 */
extern void ret_sys_call(void);

void copy_thread(int nr, unsigned long clone_flags, unsigned long sp,
		 struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs;
	struct sparc_stackf *old_stack, *new_stack;
	unsigned long stack_offset, kthread_usp = 0;

	mmu_task_cacheflush(current);
	p->tss.context = -1;

	/* Calculate offset to stack_frame & pt_regs */
	stack_offset = (PAGE_SIZE - TRACEREG_SZ);
	childregs = ((struct pt_regs *) (p->kernel_stack_page + stack_offset));
	*childregs = *regs;
	new_stack = (((struct sparc_stackf *) childregs) - 1);
	old_stack = (((struct sparc_stackf *) regs) - 1);
	*new_stack = *old_stack;
	p->tss.ksp = (unsigned long) new_stack;
	p->tss.kpc = (((unsigned long) ret_sys_call) - 0x8);

	/* As a special case, if this is a kernel fork we need
	 * to give the child a new fresh stack for when it returns
	 * from the syscall. (ie. the "user" stack)  This happens
	 * only once and we count on the page acquisition happening
	 * successfully.
	 */
	if(regs->psr & PSR_PS) {
		 unsigned long n_stack = get_free_page(GFP_KERNEL);
		 childregs->u_regs[UREG_FP] = (n_stack | (sp & 0xfff));
		 memcpy((char *)n_stack,(char *)(sp & PAGE_MASK),PAGE_SIZE);
		 kthread_usp = n_stack;
	}

	/* Set the return value for the child. */
	childregs->u_regs[UREG_I0] = current->pid;
	childregs->u_regs[UREG_I1] = 1;

	/* Set the return value for the parent. */
	regs->u_regs[UREG_I1] = 0;

	mmu_fork_hook(p, kthread_usp);
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
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
	mmu_task_cacheflush(current);
	error = getname((char *) regs->u_regs[UREG_I0], &filename);
	if(error)
		return error;
	error = do_execve(filename, (char **) regs->u_regs[UREG_I1],
			  (char **) regs->u_regs[UREG_I2], regs);
	putname(filename);
	return error;
}

void start_thread(struct pt_regs * regs, unsigned long pc, unsigned long sp)
{
	unsigned long saved_psr = (regs->psr & (PSR_CWP)) | PSR_S;

	memset(regs, 0, sizeof(struct pt_regs));
	regs->pc = ((pc & (~3)) - 4); /* whee borken a.out header fields... */
	regs->npc = regs->pc + 4;
	regs->psr = saved_psr;
	regs->u_regs[UREG_G1] = sp; /* Base of arg/env stack area */

	/* XXX More mysterious netbsd garbage... XXX */
	regs->u_regs[UREG_G2] = regs->u_regs[UREG_G7] = regs->npc;

	/* Allocate one reg window because the first jump into
	 * user mode will restore one register window by definition
	 * of the 'rett' instruction.  Also, SunOS crt.o code
	 * depends upon the arg/envp area being _exactly_ one
	 * register window above %sp when the process begins
	 * execution.
	 */
	sp -= REGWIN_SZ;
	regs->u_regs[UREG_FP] = sp;
}

/* $Id: process.c,v 1.11 1998/08/17 12:14:53 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1998 by Ralf Baechle and others.
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/mman.h>
#include <linux/sys.h>
#include <linux/user.h>
#include <linux/a.out.h>

#include <asm/bootinfo.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/mipsregs.h>
#include <asm/processor.h>
#include <asm/stackframe.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/elf.h>

struct task_struct *last_task_used_math = NULL;

asmlinkage void ret_from_sys_call(void);

/*
 * Do necessary setup to start up a newly executed thread.
 */
void start_thread(struct pt_regs * regs, unsigned long pc, unsigned long sp)
{
	/* New thread looses kernel privileges. */
	regs->cp0_status = (regs->cp0_status & ~(ST0_CU0|ST0_KSU)) | KSU_USER;
	regs->cp0_epc = pc;
	regs->regs[29] = sp;
	current->tss.current_ds = USER_DS;
}

void exit_thread(void)
{
	/* Forget lazy fpu state */
	if (last_task_used_math == current) {
		set_cp0_status(ST0_CU1, ST0_CU1);
		__asm__ __volatile__("cfc1\t$0,$31");
		last_task_used_math = NULL;
	}
}

void flush_thread(void)
{
	/* Forget lazy fpu state */
	if (last_task_used_math == current) {
		set_cp0_status(ST0_CU1, ST0_CU1);
		__asm__ __volatile__("cfc1\t$0,$31");
		last_task_used_math = NULL;
	}
}

void release_thread(struct task_struct *dead_task)
{
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
                 struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	long childksp;

	childksp = (unsigned long)p + KERNEL_STACK_SIZE - 32;

	if (last_task_used_math == current) {
		set_cp0_status(ST0_CU1, ST0_CU1);
		r4xx0_save_fp(p);
	}
	/* set up new TSS. */
	childregs = (struct pt_regs *) childksp - 1;
	*childregs = *regs;
	childregs->regs[7] = 0;	/* Clear error flag */
	if(current->personality == PER_LINUX) {
		childregs->regs[2] = 0;	/* Child gets zero as return value */
		regs->regs[2] = p->pid;
	} else {
		/* Under IRIX things are a little different. */
		childregs->regs[2] = 0;
		childregs->regs[3] = 1;
		regs->regs[2] = p->pid;
		regs->regs[3] = 0;
	}
	if (childregs->cp0_status & ST0_CU0) {
		childregs->regs[28] = (unsigned long) p;
		childregs->regs[29] = childksp;
		p->tss.current_ds = KERNEL_DS;
	} else {
		childregs->regs[29] = usp;
		p->tss.current_ds = USER_DS;
	}
	p->tss.reg29 = (unsigned long) childregs;
	p->tss.reg31 = (unsigned long) ret_from_sys_call;

	/*
	 * New tasks loose permission to use the fpu. This accelerates context
	 * switching for most programs since they don't use the fpu.
	 */
	p->tss.cp0_status = read_32bit_cp0_register(CP0_STATUS) &
                            ~(ST0_CU3|ST0_CU2|ST0_CU1|ST0_KSU);
	childregs->cp0_status &= ~(ST0_CU3|ST0_CU2|ST0_CU1);
	p->mm->context = 0;

	return 0;
}

/* Fill in the fpu structure for a core dump.. */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *r)
{
	/* We actually store the FPU info in the task->tss
	 * area.
	 */
	if(regs->cp0_status & ST0_CU1) {
		memcpy(r, &current->tss.fpu, sizeof(current->tss.fpu));
		return 1;
	}
	return 0; /* Task didn't use the fpu at all. */
}

/* Fill in the user structure for a core dump.. */
void dump_thread(struct pt_regs *regs, struct user *dump)
{
	dump->magic = CMAGIC;
	dump->start_code  = current->mm->start_code;
	dump->start_data  = current->mm->start_data;
	dump->start_stack = regs->regs[29] & ~(PAGE_SIZE - 1);
	dump->u_tsize = (current->mm->end_code - dump->start_code) >> PAGE_SHIFT;
	dump->u_dsize = (current->mm->brk + (PAGE_SIZE - 1) - dump->start_data) >> PAGE_SHIFT;
	dump->u_ssize =
		(current->mm->start_stack - dump->start_stack + PAGE_SIZE - 1) >> PAGE_SHIFT;
	memcpy(&dump->regs[0], regs, sizeof(struct pt_regs));
	memcpy(&dump->regs[EF_SIZE/4], &current->tss.fpu, sizeof(current->tss.fpu));
}

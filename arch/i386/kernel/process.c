/*
 *  linux/arch/i386/kernel/process.c
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

asmlinkage void ret_from_sys_call(void) __asm__("ret_from_sys_call");

/*
 * The idle loop on a i386..
 */
asmlinkage int sys_idle(void)
{
	int i;

	if (current->pid != 0)
		return -EPERM;

	/* Map out the low memory: it's no longer needed */
	for (i = 0 ; i < 768 ; i++)
		swapper_pg_dir[i] = 0;

	/* endless idle loop with no priority at all */
	current->counter = -100;
	for (;;) {
		if (hlt_works_ok && !need_resched)
			__asm__("hlt");
		schedule();
	}
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
void start_thread(struct pt_regs * regs, unsigned long eip, unsigned long esp)
{
	regs->eip = eip;
	regs->esp = esp;
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	/* forget local segments */
	__asm__ __volatile__("mov %w0,%%fs ; mov %w0,%%gs ; lldt %w0"
		: /* no outputs */
		: "r" (0));
	current->tss.ldt = 0;
	if (current->ldt) {
		void * ldt = current->ldt;
		current->ldt = NULL;
		vfree(ldt);
	}
}

void flush_thread(void)
{
	int i;

	if (current->ldt) {
		free_page((unsigned long) current->ldt);
		current->ldt = NULL;
		for (i=1 ; i<NR_TASKS ; i++) {
			if (task[i] == current)  {
				set_ldt_desc(gdt+(i<<1)+
					     FIRST_LDT_ENTRY,&default_ldt, 1);
				load_ldt(i);
			}
		}	
	}

	for (i=0 ; i<8 ; i++)
		current->debugreg[i] = 0;
}

#define IS_CLONE (regs->orig_eax == __NR_clone)

unsigned long copy_thread(int nr, unsigned long clone_flags, struct task_struct * p, struct pt_regs * regs)
{
	int i;
	struct pt_regs * childregs;

	p->tss.es = KERNEL_DS;
	p->tss.cs = KERNEL_CS;
	p->tss.ss = KERNEL_DS;
	p->tss.ds = KERNEL_DS;
	p->tss.fs = USER_DS;
	p->tss.gs = KERNEL_DS;
	p->tss.ss0 = KERNEL_DS;
	p->tss.esp0 = p->kernel_stack_page + PAGE_SIZE;
	p->tss.tr = _TSS(nr);
	childregs = ((struct pt_regs *) (p->kernel_stack_page + PAGE_SIZE)) - 1;
	p->tss.esp = (unsigned long) childregs;
	p->tss.eip = (unsigned long) ret_from_sys_call;
	*childregs = *regs;
	childregs->eax = 0;
	p->tss.back_link = 0;
	p->tss.eflags = regs->eflags & 0xffffcfff;	/* iopl is always 0 for a new process */
	if (IS_CLONE) {
		if (regs->ebx)
			childregs->esp = regs->ebx;
		clone_flags = regs->ecx;
		if (childregs->esp == regs->esp)
			clone_flags |= COPYVM;
	}
	p->tss.ldt = _LDT(nr);
	if (p->ldt) {
		p->ldt = (struct desc_struct*) vmalloc(LDT_ENTRIES*LDT_ENTRY_SIZE);
		if (p->ldt != NULL)
			memcpy(p->ldt, current->ldt, LDT_ENTRIES*LDT_ENTRY_SIZE);
	}
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	if (p->ldt)
		set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,p->ldt, 512);
	else
		set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&default_ldt, 1);
	p->tss.bitmap = offsetof(struct thread_struct,io_bitmap);
	for (i = 0; i < IO_BITMAP_SIZE+1 ; i++) /* IO bitmap is actually SIZE+1 */
		p->tss.io_bitmap[i] = ~0;
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0 ; frstor %0":"=m" (p->tss.i387));
	return clone_flags;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
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
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(struct pt_regs regs)
{
	int error;
	char * filename;

	error = getname((char *) regs.ebx, &filename);
	if (error)
		return error;
	error = do_execve(filename, (char **) regs.ecx, (char **) regs.edx, &regs);
	putname(filename);
	return error;
}

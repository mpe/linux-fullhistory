/*
 *  linux/arch/m68k/kernel/process.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 *
 *  68060 fixes by Jesper Skov
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/reboot.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/setup.h>
#include <asm/pgtable.h>

/*
 * Initial task structure. Make this a per-architecture thing,
 * because different architectures tend to have different
 * alignment requirements and potentially different initial
 * setup.
 */
static struct vm_area_struct init_mmap = INIT_MMAP;
static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS;
struct mm_struct init_mm = INIT_MM;

union task_union init_task_union
	__attribute__((section("init_task"), aligned(2*PAGE_SIZE)))
	= { task: INIT_TASK };

asmlinkage void ret_from_exception(void);

/*
 * The idle loop on an m68k..
 */
asmlinkage int sys_idle(void)
{
	int ret = -EPERM;

	lock_kernel();
	if (current->pid != 0)
		goto out;

	/* endless idle loop with no priority at all */
	current->priority = -100;
	current->counter = -100;
	for (;;){
		if (!resched_needed())
#if defined(CONFIG_ATARI) && !defined(CONFIG_AMIGA) && !defined(CONFIG_MAC)
			/* block out HSYNC on the atari (falcon) */
			__asm__("stop #0x2200" : : : "cc");
#else /* portable version */
			__asm__("stop #0x2000" : : : "cc");
#endif /* machine compilation types */ 
		run_task_queue(&tq_scheduler);
		schedule();
	}
	ret = 0;
out:
	unlock_kernel();
	return ret;
}

void machine_restart(char * __unused)
{
	if (mach_reset)
		mach_reset();
}

void machine_halt(void)
{
}

void machine_power_off(void)
{
#if defined(CONFIG_APM) && defined(CONFIG_APM_POWER_OFF)
	apm_set_power_state(APM_STATE_OFF);
#endif
}

void show_regs(struct pt_regs * regs)
{
	printk("\n");
	printk("Format %02x  Vector: %04x  PC: %08lx  Status: %04x\n",
	       regs->format, regs->vector, regs->pc, regs->sr);
	printk("ORIG_D0: %08lx  D0: %08lx  A2: %08lx  A1: %08lx\n",
	       regs->orig_d0, regs->d0, regs->a2, regs->a1);
	printk("A0: %08lx  D5: %08lx  D4: %08lx\n",
	       regs->a0, regs->d5, regs->d4);
	printk("D3: %08lx  D2: %08lx  D1: %08lx\n",
	       regs->d3, regs->d2, regs->d1);
	if (!(regs->sr & PS_S))
		printk("USP: %08lx\n", rdusp());
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
}

void flush_thread(void)
{
	set_fs(USER_DS);
	current->tss.fs = USER_DS;
}

/*
 * "m68k_fork()".. By the time we get here, the
 * non-volatile registers have also been saved on the
 * stack. We do some ugly pointer stuff here.. (see
 * also copy_thread)
 */

asmlinkage int m68k_fork(struct pt_regs *regs)
{
	int ret;

	lock_kernel();
	ret = do_fork(SIGCHLD, rdusp(), regs);
	unlock_kernel();
	return ret;
}

asmlinkage int m68k_clone(struct pt_regs *regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	int ret;

	lock_kernel();
	/* syscall2 puts clone_flags in d1 and usp in d2 */
	clone_flags = regs->d1;
	newsp = regs->d2;
	if (!newsp)
	  newsp  = rdusp();
	ret = do_fork(clone_flags, newsp, regs);
	unlock_kernel();
	return ret;
}

void release_thread(struct task_struct *dead_task)
{
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
		 struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	struct switch_stack * childstack, *stack;
	unsigned long stack_offset, *retp;

	stack_offset = 2*PAGE_SIZE - sizeof(struct pt_regs);
	childregs = (struct pt_regs *) ((unsigned long) p + stack_offset);

	*childregs = *regs;
	childregs->d0 = 0;

	retp = ((unsigned long *) regs);
	stack = ((struct switch_stack *) retp) - 1;

	childstack = ((struct switch_stack *) childregs) - 1;
	*childstack = *stack;
	childstack->retpc = (unsigned long) ret_from_exception;

	p->tss.usp = usp;
	p->tss.ksp = (unsigned long)childstack;
	/*
	 * Must save the current SFC/DFC value, NOT the value when
	 * the parent was last descheduled - RGH  10-08-96
	 */
	p->tss.fs = get_fs();

	/* Copy the current fpu state */
	asm volatile ("fsave %0" : : "m" (p->tss.fpstate[0]) : "memory");

	if((!CPU_IS_060 && p->tss.fpstate[0]) ||
	   (CPU_IS_060 && p->tss.fpstate[2]))
	  asm volatile ("fmovemx %/fp0-%/fp7,%0\n\t"
			"fmoveml %/fpiar/%/fpcr/%/fpsr,%1"
			: : "m" (p->tss.fp[0]), "m" (p->tss.fpcntl[0])
			: "memory");
	/* Restore the state in case the fpu was busy */
	asm volatile ("frestore %0" : : "m" (p->tss.fpstate[0]));

	return 0;
}

/* Fill in the fpu structure for a core dump.  */

int dump_fpu (struct pt_regs *regs, struct user_m68kfp_struct *fpu)
{
  char fpustate[216];

  /* First dump the fpu context to avoid protocol violation.  */
  asm volatile ("fsave %0" :: "m" (fpustate[0]) : "memory");
  if((!CPU_IS_060 && !fpustate[0]) || (CPU_IS_060 && !fpustate[2]))
     return 0;

  asm volatile ("fmovem %/fpiar/%/fpcr/%/fpsr,%0"
		:: "m" (fpu->fpcntl[0])
		: "memory");
  asm volatile ("fmovemx %/fp0-%/fp7,%0"
		:: "m" (fpu->fpregs[0])
		: "memory");
  return 1;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
	struct switch_stack *sw;

/* changed the size calculations - should hopefully work better. lbt */
	dump->magic = CMAGIC;
	dump->start_code = 0;
	dump->start_stack = rdusp() & ~(PAGE_SIZE - 1);
	dump->u_tsize = ((unsigned long) current->mm->end_code) >> PAGE_SHIFT;
	dump->u_dsize = ((unsigned long) (current->mm->brk +
					  (PAGE_SIZE-1))) >> PAGE_SHIFT;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;

	if (dump->start_stack < TASK_SIZE)
		dump->u_ssize = ((unsigned long) (TASK_SIZE - dump->start_stack)) >> PAGE_SHIFT;

	dump->u_ar0 = (struct user_regs_struct *)((int)&dump->regs - (int)dump);
	sw = ((struct switch_stack *)regs) - 1;
	dump->regs.d1 = regs->d1;
	dump->regs.d2 = regs->d2;
	dump->regs.d3 = regs->d3;
	dump->regs.d4 = regs->d4;
	dump->regs.d5 = regs->d5;
	dump->regs.d6 = sw->d6;
	dump->regs.d7 = sw->d7;
	dump->regs.a0 = regs->a0;
	dump->regs.a1 = regs->a1;
	dump->regs.a2 = regs->a2;
	dump->regs.a3 = sw->a3;
	dump->regs.a4 = sw->a4;
	dump->regs.a5 = sw->a5;
	dump->regs.a6 = sw->a6;
	dump->regs.d0 = regs->d0;
	dump->regs.orig_d0 = regs->orig_d0;
	dump->regs.stkadj = regs->stkadj;
	dump->regs.sr = regs->sr;
	dump->regs.pc = regs->pc;
	dump->regs.fmtvec = (regs->format << 12) | regs->vector;
	/* dump floating point stuff */
	dump->u_fpvalid = dump_fpu (regs, &dump->m68kfp);
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(char *name, char **argv, char **envp)
{
	int error;
	char * filename;
	struct pt_regs *regs = (struct pt_regs *) &name;

	lock_kernel();
	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
out:
	unlock_kernel();
	return error;
}

/*
 *  linux/arch/arm/kernel/process.c
 *
 *  Copyright (C) 1996-1999 Russell King - Converted to ARM.
 *  Origional Copyright (C) 1995  Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <stdarg.h>

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
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/arch/system.h>
#include <asm/io.h>

extern char *processor_modes[];

asmlinkage void ret_from_sys_call(void) __asm__("ret_from_sys_call");

static int hlt_counter=0;

void disable_hlt(void)
{
	hlt_counter++;
}

void enable_hlt(void)
{
	hlt_counter--;
}

/*
 * The idle loop on an ARM...
 */
void cpu_idle(void)
{
	/* endless idle loop with no priority at all */
	init_idle();
	current->priority = 0;
	current->counter = -100;
	while (1) {
		if (!hlt_counter)
			arch_do_idle();
		if (current->need_resched) {
			schedule();
#ifndef CONFIG_NO_PGT_CACHE
			check_pgt_cache();
#endif
		}
	}
}

static char reboot_mode = 'h';

int __init reboot_setup(char *str)
{
	reboot_mode = str[0];
	return 0;
}

__setup("reboot=", reboot_setup);

void machine_restart(char * __unused)
{
	/*
	 * Turn off caches, interrupts, etc
	 */
	cpu_proc_fin();

	arch_reset(reboot_mode);

	printk("Reboot failed -- System halted\n");

	while (1);
}

void machine_halt(void)
{
}

void machine_power_off(void)
{
}

void show_regs(struct pt_regs * regs)
{
	unsigned long flags;

	flags = condition_codes(regs);

	printk( "pc : [<%08lx>]    lr : [<%08lx>]\n"
		"sp : %08lx  ip : %08lx  fp : %08lx\n",
		instruction_pointer(regs),
		regs->ARM_lr, regs->ARM_sp,
		regs->ARM_ip, regs->ARM_fp);
	printk( "r10: %08lx  r9 : %08lx  r8 : %08lx\n",
		regs->ARM_r10, regs->ARM_r9,
		regs->ARM_r8);
	printk( "r7 : %08lx  r6 : %08lx  r5 : %08lx  r4 : %08lx\n",
		regs->ARM_r7, regs->ARM_r6,
		regs->ARM_r5, regs->ARM_r4);
	printk( "r3 : %08lx  r2 : %08lx  r1 : %08lx  r0 : %08lx\n",
		regs->ARM_r3, regs->ARM_r2,
		regs->ARM_r1, regs->ARM_r0);
	printk("Flags: %c%c%c%c",
		flags & CC_N_BIT ? 'N' : 'n',
		flags & CC_Z_BIT ? 'Z' : 'z',
		flags & CC_C_BIT ? 'C' : 'c',
		flags & CC_V_BIT ? 'V' : 'v');
	printk("  IRQs %s  FIQs %s  Mode %s  Segment %s\n",
		interrupts_enabled(regs) ? "on" : "off",
		fast_interrupts_enabled(regs) ? "on" : "off",
		processor_modes[processor_mode(regs)],
		get_fs() == get_ds() ? "kernel" : "user");
#if defined(CONFIG_CPU_32)
	{
		int ctrl, transbase, dac;
		  __asm__ (
		"	mrc p15, 0, %0, c1, c0\n"
		"	mrc p15, 0, %1, c2, c0\n"
		"	mrc p15, 0, %2, c3, c0\n"
		: "=r" (ctrl), "=r" (transbase), "=r" (dac));
		printk("Control: %04X  Table: %08X  DAC: %08X\n",
		  	ctrl, transbase, dac);
	}
#endif
}

void show_fpregs(struct user_fp *regs)
{
	int i;

	for (i = 0; i < 8; i++) {
		unsigned long *p;
		char type;

		p = (unsigned long *)(regs->fpregs + i);

		switch (regs->ftype[i]) {
			case 1: type = 'f'; break;
			case 2: type = 'd'; break;
			case 3: type = 'e'; break;
			default: type = '?'; break;
		}
		if (regs->init_flag)
			type = '?';

		printk("  f%d(%c): %08lx %08lx %08lx%c",
			i, type, p[0], p[1], p[2], i & 1 ? '\n' : ' ');
	}
			

	printk("FPSR: %08lx FPCR: %08lx\n",
		(unsigned long)regs->fpsr,
		(unsigned long)regs->fpcr);
}

/*
 * Task structure and kernel stack allocation.
 *
 * Taken from the i386 version.
 */
#ifdef CONFIG_CPU_32
#define EXTRA_TASK_STRUCT	8
static struct task_struct *task_struct_stack[EXTRA_TASK_STRUCT];
static int task_struct_stack_ptr = -1;
#endif

struct task_struct *alloc_task_struct(void)
{
	struct task_struct *tsk;

#ifndef EXTRA_TASK_STRUCT
	tsk = ll_alloc_task_struct();
#else
	int index;

	index = task_struct_stack_ptr;
	if (index >= EXTRA_TASK_STRUCT/2)
		goto use_cache;

	tsk = ll_alloc_task_struct();

	if (!tsk) {
		index = task_struct_stack_ptr;

		if (index >= 0) {
use_cache:		tsk = task_struct_stack[index];
			task_struct_stack_ptr = index - 1;
		}
	}
#endif
#ifdef CONFIG_SYSRQ
	/* You need this if you want SYSRQ-T to give sensible stack
	 * usage information
	 */
	if (tsk) {
		char *p = (char *)tsk;
		memzero(p+KERNEL_STACK_SIZE, KERNEL_STACK_SIZE);
	}
#endif

	return tsk;
}

void free_task_struct(struct task_struct *p)
{
#ifdef EXTRA_TASK_STRUCT
	int index = task_struct_stack_ptr + 1;

	if (index < EXTRA_TASK_STRUCT) {
		task_struct_stack[index] = p;
		task_struct_stack_ptr = index;
	} else
#endif
		ll_free_task_struct(p);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
}

void flush_thread(void)
{
	memset(&current->thread.debug, 0, sizeof(current->thread.debug));
	memset(&current->thread.fpstate, 0, sizeof(current->thread.fpstate));
	current->used_math = 0;
	current->flags &= ~PF_USEDFPU;
}

void release_thread(struct task_struct *dead_task)
{
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long esp,
	struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	struct context_save_struct * save;

	childregs = ((struct pt_regs *)((unsigned long)p + 8192)) - 1;
	*childregs = *regs;
	childregs->ARM_r0 = 0;
	childregs->ARM_sp = esp;

	save = ((struct context_save_struct *)(childregs)) - 1;
	init_thread_css(save);
	p->thread.save = save;

	return 0;
}

/*
 * fill in the fpe structure for a core dump...
 */
int dump_fpu (struct pt_regs *regs, struct user_fp *fp)
{
	if (current->used_math)
		memcpy(fp, &current->thread.fpstate.soft, sizeof (fp));

	return current->used_math;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
	dump->magic = CMAGIC;
	dump->start_code = current->mm->start_code;
	dump->start_stack = regs->ARM_sp & ~(PAGE_SIZE - 1);

	dump->u_tsize = (current->mm->end_code - current->mm->start_code) >> PAGE_SHIFT;
	dump->u_dsize = (current->mm->brk - current->mm->start_data + PAGE_SIZE - 1) >> PAGE_SHIFT;
	dump->u_ssize = 0;

	dump->u_debugreg[0] = current->thread.debug.bp[0].address;
	dump->u_debugreg[1] = current->thread.debug.bp[1].address;
	dump->u_debugreg[2] = current->thread.debug.bp[0].insn;
	dump->u_debugreg[3] = current->thread.debug.bp[1].insn;
	dump->u_debugreg[4] = current->thread.debug.nsaved;

	if (dump->start_stack < 0x04000000)
		dump->u_ssize = (0x04000000 - dump->start_stack) >> PAGE_SHIFT;

	dump->regs = *regs;
	dump->u_fpvalid = dump_fpu (regs, &dump->u_fp);
}

/*
 * This is the mechanism for creating a new kernel thread.
 *
 * NOTE! Only a kernel-only process(ie the swapper or direct descendants
 * who haven't done an "execve()") should use this: it will work within
 * a system call from a "real" process, but the process memory space will
 * not be free'd until both the parent and the child have exited.
 */
pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	extern long sys_exit(int) __attribute__((noreturn));
	pid_t __ret;

	__asm__ __volatile__(
	"mov	r0, %1		@ kernel_thread sys_clone\n"
"	mov	r1, #0\n"
	__syscall(clone)"\n"
"	mov	%0, r0"
        : "=r" (__ret)
        : "Ir" (flags | CLONE_VM) : "r0", "r1");
	if (__ret == 0)
		sys_exit((fn)(arg));
	return __ret;
}

/*
 * These bracket the sleeping functions..
 */
extern void scheduling_functions_start_here(void);
extern void scheduling_functions_end_here(void);
#define first_sched	((unsigned long) scheduling_functions_start_here)
#define last_sched	((unsigned long) scheduling_functions_end_here)

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long fp, lr;
	unsigned long stack_page;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	stack_page = 4096 + (unsigned long)p;
	fp = get_css_fp(&p->thread);
	do {
		if (fp < stack_page || fp > 4092+stack_page)
			return 0;
		lr = pc_pointer (((unsigned long *)fp)[-1]);
		if (lr < first_sched || lr > last_sched)
			return lr;
		fp = *(unsigned long *) (fp - 12);
	} while (count ++ < 16);
	return 0;
}

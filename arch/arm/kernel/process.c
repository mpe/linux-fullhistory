/*
 *  linux/arch/arm/kernel/process.c
 *
 *  Copyright (C) 1996 Russell King - Converted to ARM.
 *  Origional Copyright (C) 1995  Linus Torvalds
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
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/unistd.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/reboot.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
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
 * The idle loop on an arm..
 */
asmlinkage int sys_idle(void)
{
	int ret = -EPERM;

	lock_kernel();
	if (current->pid != 0)
		goto out;
	/* endless idle loop with no priority at all */
	current->priority = -100;
	for (;;)
	{
		check_pgt_cache();
#if 0 //def ARCH_IDLE_OK
		if (!hlt_counter && !current->need_resched)
			proc_idle ();
#endif
		run_task_queue(&tq_scheduler);
		schedule();
	}
	ret = 0;
out:
	unlock_kernel();
	return ret;
}

__initfunc(void reboot_setup(char *str, int *ints))
{
}

/*
 * This routine reboots the machine by resetting the expansion cards via
 * their loaders, turning off the processor cache (if ARM3), copying the
 * first instruction of the ROM to 0, and executing it there.
 */
void machine_restart(char * __unused)
{
	proc_hard_reset ();
	arch_hard_reset ();
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

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
}

void flush_thread(void)
{
	int i;

	for (i = 0; i < NR_DEBUGS; i++)
		current->tss.debug[i] = 0;
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

	save = ((struct context_save_struct *)(childregs)) - 1;
	copy_thread_css(save);
	p->tss.save = save;

	return 0;
}

/*
 * fill in the fpe structure for a core dump...
 */
int dump_fpu (struct pt_regs *regs, struct user_fp *fp)
{
	int fpvalid = 0;

	if (current->used_math)
		memcpy (fp, &current->tss.fpstate.soft, sizeof (fp));

	return fpvalid;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
	int i;

	dump->magic = CMAGIC;
	dump->start_code = current->mm->start_code;
	dump->start_stack = regs->ARM_sp & ~(PAGE_SIZE - 1);

	dump->u_tsize = (current->mm->end_code - current->mm->start_code) >> PAGE_SHIFT;
	dump->u_dsize = (current->mm->brk - current->mm->start_data + PAGE_SIZE - 1) >> PAGE_SHIFT;
	dump->u_ssize = 0;

	for (i = 0; i < NR_DEBUGS; i++)
		dump->u_debugreg[i] = current->tss.debug[i];  

	if (dump->start_stack < 0x04000000)
		dump->u_ssize = (0x04000000 - dump->start_stack) >> PAGE_SHIFT;

	dump->regs = *regs;
	dump->u_fpvalid = dump_fpu (regs, &dump->u_fp);
}

/*
 * Architecture-specific setup.
 *
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */
#define __KERNEL_SYSCALLS__	/* see <asm/unistd.h> */
#include <linux/config.h>

#include <linux/pm.h>
#include <linux/elf.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>

#include <asm/delay.h>
#include <asm/efi.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/sal.h>
#include <asm/uaccess.h>
#include <asm/user.h>


void
show_regs (struct pt_regs *regs)
{
	unsigned long ip = regs->cr_iip + ia64_psr(regs)->ri;

	printk("\npsr : %016lx ifs : %016lx ip  : [<%016lx>]\n",
	       regs->cr_ipsr, regs->cr_ifs, ip);
	printk("unat: %016lx pfs : %016lx rsc : %016lx\n",
	       regs->ar_unat, regs->ar_pfs, regs->ar_rsc);
	printk("rnat: %016lx bsps: %016lx pr  : %016lx\n",
	       regs->ar_rnat, regs->ar_bspstore, regs->pr);
	printk("ldrs: %016lx ccv : %016lx fpsr: %016lx\n",
	       regs->loadrs, regs->ar_ccv, regs->ar_fpsr);
	printk("b0  : %016lx b6  : %016lx b7  : %016lx\n", regs->b0, regs->b6, regs->b7);
	printk("f6  : %05lx%016lx f7  : %05lx%016lx\n",
	       regs->f6.u.bits[1], regs->f6.u.bits[0],
	       regs->f7.u.bits[1], regs->f7.u.bits[0]);
	printk("f8  : %05lx%016lx f9  : %05lx%016lx\n",
	       regs->f8.u.bits[1], regs->f8.u.bits[0],
	       regs->f9.u.bits[1], regs->f9.u.bits[0]);

	printk("r1  : %016lx r2  : %016lx r3  : %016lx\n", regs->r1, regs->r2, regs->r3);
	printk("r8  : %016lx r9  : %016lx r10 : %016lx\n", regs->r8, regs->r9, regs->r10);
	printk("r11 : %016lx r12 : %016lx r13 : %016lx\n", regs->r11, regs->r12, regs->r13);
	printk("r14 : %016lx r15 : %016lx r16 : %016lx\n", regs->r14, regs->r15, regs->r16);
	printk("r17 : %016lx r18 : %016lx r19 : %016lx\n", regs->r17, regs->r18, regs->r19);
	printk("r20 : %016lx r21 : %016lx r22 : %016lx\n", regs->r20, regs->r21, regs->r22);
	printk("r23 : %016lx r24 : %016lx r25 : %016lx\n", regs->r23, regs->r24, regs->r25);
	printk("r26 : %016lx r27 : %016lx r28 : %016lx\n", regs->r26, regs->r27, regs->r28);
	printk("r29 : %016lx r30 : %016lx r31 : %016lx\n", regs->r29, regs->r30, regs->r31);

	/* print the stacked registers if cr.ifs is valid: */
	if (regs->cr_ifs & 0x8000000000000000) {
		unsigned long val, sof, *bsp, ndirty;
		int i, is_nat = 0;

		sof = regs->cr_ifs & 0x7f;	/* size of frame */
		ndirty = (regs->loadrs >> 19);
		bsp = ia64_rse_skip_regs((unsigned long *) regs->ar_bspstore, ndirty);
		for (i = 0; i < sof; ++i) {
			get_user(val, ia64_rse_skip_regs(bsp, i));
			printk("r%-3u:%c%016lx%s", 32 + i, is_nat ? '*' : ' ', val,
			       ((i == sof - 1) || (i % 3) == 2) ? "\n" : " ");
		}
	}
}

void __attribute__((noreturn))
cpu_idle (void *unused)
{
	/* endless idle loop with no priority at all */
	init_idle();
	current->priority = 0;
	current->counter = -100;

#ifdef CONFIG_SMP
	if (!current->need_resched)
		min_xtp();
#endif

	while (1) {
		while (!current->need_resched) {
			continue;
		}
#ifdef CONFIG_SMP
		normal_xtp();
#endif
		schedule();
		check_pgt_cache();
		if (pm_idle)
			(*pm_idle)();
#ifdef CONFIG_ITANIUM_ASTEP_SPECIFIC
		if (ia64_get_itm() < ia64_get_itc()) {
			extern void ia64_reset_itm();

			printk("cpu_idle: ITM in past, resetting it...\n");
			ia64_reset_itm();
		}
#endif
	}
}

/*
 * Copy the state of an ia-64 thread.
 *
 * We get here through the following  call chain:
 *
 *	<clone syscall>
 *	sys_clone
 *	do_fork
 *	copy_thread
 *
 * This means that the stack layout is as follows:
 *
 *	+---------------------+ (highest addr)
 *	|   struct pt_regs    |
 *	+---------------------+
 *	| struct switch_stack |
 *	+---------------------+
 *	|                     |
 *	|    memory stack     |
 *	|                     | <-- sp (lowest addr)
 *	+---------------------+
 *
 * Note: if we get called through kernel_thread() then the memory
 * above "(highest addr)" is valid kernel stack memory that needs to
 * be copied as well.
 *
 * Observe that we copy the unat values that are in pt_regs and
 * switch_stack.  Since the interpretation of unat is dependent upon
 * the address to which the registers got spilled, doing this is valid
 * only as long as we preserve the alignment of the stack.  Since the
 * stack is always page aligned, we know this is the case.
 *
 * XXX Actually, the above isn't true when we create kernel_threads().
 * If we ever needs to create kernel_threads() that preserve the unat
 * values we'll need to fix this.  Perhaps an easy workaround would be
 * to always clear the unat bits in the child thread.
 */
int
copy_thread (int nr, unsigned long clone_flags, unsigned long usp,
	     struct task_struct *p, struct pt_regs *regs)
{
	unsigned long rbs, child_rbs, rbs_size, stack_offset, stack_top, stack_used;
	struct switch_stack *child_stack, *stack;
	extern char ia64_ret_from_syscall_clear_r8;
	extern char ia64_strace_clear_r8;
	struct pt_regs *child_ptregs;

#ifdef CONFIG_SMP
	/*
	 * For SMP idle threads, fork_by_hand() calls do_fork with
	 * NULL regs.
	 */
	if (!regs)
		return 0;
#endif

	stack_top = (unsigned long) current + IA64_STK_OFFSET;
	stack = ((struct switch_stack *) regs) - 1;
	stack_used = stack_top - (unsigned long) stack;
	stack_offset = IA64_STK_OFFSET - stack_used;

	child_stack = (struct switch_stack *) ((unsigned long) p + stack_offset);
	child_ptregs = (struct pt_regs *) (child_stack + 1);

	/* copy parent's switch_stack & pt_regs to child: */
	memcpy(child_stack, stack, stack_used);

	rbs = (unsigned long) current + IA64_RBS_OFFSET;
	child_rbs = (unsigned long) p + IA64_RBS_OFFSET;
	rbs_size = stack->ar_bspstore - rbs;

	/* copy the parent's register backing store to the child: */
	memcpy((void *) child_rbs, (void *) rbs, rbs_size);

	child_ptregs->r8 = 0;			/* child gets a zero return value */
	if (user_mode(child_ptregs))
		child_ptregs->r12 = usp;			/* user stack pointer */
	else {
		/*
		 * Note: we simply preserve the relative position of
		 * the stack pointer here.  There is no need to
		 * allocate a scratch area here, since that will have
		 * been taken care of by the caller of sys_clone()
		 * already.
		 */
		child_ptregs->r12 = (unsigned long) (child_ptregs + 1); /* kernel sp */
		child_ptregs->r13 = (unsigned long) p;		/* set `current' pointer */
	}
	if (p->flags & PF_TRACESYS)
		child_stack->b0 = (unsigned long) &ia64_strace_clear_r8;
	else
		child_stack->b0 = (unsigned long) &ia64_ret_from_syscall_clear_r8;
	child_stack->ar_bspstore = child_rbs + rbs_size;

	/* copy the thread_struct: */
	p->thread.ksp = (unsigned long) child_stack - 16;
	/*
	 * NOTE: The calling convention considers all floating point
	 * registers in the high partition (fph) to be scratch.  Since
	 * the only way to get to this point is through a system call,
	 * we know that the values in fph are all dead.  Hence, there
	 * is no need to inherit the fph state from the parent to the
	 * child and all we have to do is to make sure that
	 * IA64_THREAD_FPH_VALID is cleared in the child.
	 *
	 * XXX We could push this optimization a bit further by
	 * clearing IA64_THREAD_FPH_VALID on ANY system call.
	 * However, it's not clear this is worth doing.  Also, it
	 * would be a slight deviation from the normal Linux system
	 * call behavior where scratch registers are preserved across
	 * system calls (unless used by the system call itself).
	 *
	 * If we wanted to inherit the fph state from the parent to the
	 * child, we would have to do something along the lines of:
	 *
	 *	if (ia64_get_fpu_owner() == current && ia64_psr(regs)->mfh) {
	 *		p->thread.flags |= IA64_THREAD_FPH_VALID;
	 *		ia64_save_fpu(&p->thread.fph);
	 *	} else if (current->thread.flags & IA64_THREAD_FPH_VALID) {
	 *		memcpy(p->thread.fph, current->thread.fph, sizeof(p->thread.fph));
	 *	}
	 */
	p->thread.flags = (current->thread.flags & ~IA64_THREAD_FPH_VALID);
	return 0;
}

void
ia64_elf_core_copy_regs (struct pt_regs *pt, elf_gregset_t dst)
{
	struct switch_stack *sw = ((struct switch_stack *) pt) - 1;
	unsigned long ar_ec, cfm, ar_bsp, ndirty, *krbs;

	ar_ec = (sw->ar_pfs >> 52) & 0x3f;

	cfm = pt->cr_ifs & ((1UL << 63) - 1);
	if ((pt->cr_ifs & (1UL << 63)) == 0) {
		/* if cr_ifs isn't valid, we got here through a syscall or a break */
		cfm = sw->ar_pfs & ((1UL << 38) - 1);
	}

	krbs = (unsigned long *) current + IA64_RBS_OFFSET/8;
	ndirty = ia64_rse_num_regs(krbs, krbs + (pt->loadrs >> 16));
	ar_bsp = (long) ia64_rse_skip_regs((long *) pt->ar_bspstore, ndirty);

	/*	r0-r31
	 *	NaT bits (for r0-r31; bit N == 1 iff rN is a NaT)
	 *	predicate registers (p0-p63)
	 *	b0-b7
	 *	ip cfm user-mask
	 *	ar.rsc ar.bsp ar.bspstore ar.rnat
	 *	ar.ccv ar.unat ar.fpsr ar.pfs ar.lc ar.ec
	 */
	memset(dst, 0, sizeof (dst));	/* don't leak any "random" bits */

	/* r0 is zero */   dst[ 1] =  pt->r1; dst[ 2] =  pt->r2; dst[ 3] = pt->r3;
	dst[ 4] =  sw->r4; dst[ 5] =  sw->r5; dst[ 6] =  sw->r6; dst[ 7] = sw->r7;
	dst[ 8] =  pt->r8; dst[ 9] =  pt->r9; dst[10] = pt->r10; dst[11] = pt->r11;
	dst[12] = pt->r12; dst[13] = pt->r13; dst[14] = pt->r14; dst[15] = pt->r15;
	memcpy(dst + 16, &pt->r16, 16*8);	/* r16-r31 are contiguous */

	dst[32] = ia64_get_nat_bits(pt, sw);
	dst[33] = pt->pr;

	/* branch regs: */
	dst[34] = pt->b0; dst[35] = sw->b1; dst[36] = sw->b2; dst[37] = sw->b3;
	dst[38] = sw->b4; dst[39] = sw->b5; dst[40] = pt->b6; dst[41] = pt->b7;

	dst[42] = pt->cr_iip; dst[43] = pt->cr_ifs;
	dst[44] = pt->cr_ipsr;	/* XXX perhaps we should filter out some bits here? --davidm */

	dst[45] = pt->ar_rsc; dst[46] = ar_bsp; dst[47] = pt->ar_bspstore;  dst[48] = pt->ar_rnat;
	dst[49] = pt->ar_ccv; dst[50] = pt->ar_unat; dst[51] = sw->ar_fpsr; dst[52] = pt->ar_pfs;
	dst[53] = sw->ar_lc; dst[54] = (sw->ar_pfs >> 52) & 0x3f;
}

int
dump_fpu (struct pt_regs *pt, elf_fpregset_t dst)
{
	struct switch_stack *sw = ((struct switch_stack *) pt) - 1;
	struct task_struct *fpu_owner = ia64_get_fpu_owner();

	memset(dst, 0, sizeof (dst));	/* don't leak any "random" bits */

	/* f0 is 0.0 */  /* f1 is 1.0 */  dst[2] = sw->f2; dst[3] = sw->f3;
	dst[4] = sw->f4; dst[5] = sw->f5; dst[6] = pt->f6; dst[7] = pt->f7;
	dst[8] = pt->f8; dst[9] = pt->f9;
	memcpy(dst + 10, &sw->f10, 22*16);	/* f10-f31 are contiguous */

	if ((fpu_owner == current) || (current->thread.flags & IA64_THREAD_FPH_VALID)) {
		if (fpu_owner == current) {
			__ia64_save_fpu(current->thread.fph);
		}
		memcpy(dst + 32, current->thread.fph, 96*16);
	}
	return 1;	/* f0-f31 are always valid so we always return 1 */
}

asmlinkage long
sys_execve (char *filename, char **argv, char **envp, struct pt_regs *regs)
{
	int error;

	lock_kernel();
	filename = getname(filename);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
out:
	unlock_kernel();
	return error;
}

pid_t
kernel_thread (int (*fn)(void *), void *arg, unsigned long flags)
{
	struct task_struct *parent = current;
	int result;

	clone(flags | CLONE_VM, 0);
	if (parent != current) {
		result = (*fn)(arg);
		_exit(result);
	}
	return 0;		/* parent: just return */
}

/*
 * Flush thread state.  This is called when a thread does an execve().
 */
void
flush_thread (void)
{
	/* drop floating-point and debug-register state if it exists: */
	current->thread.flags &= ~(IA64_THREAD_FPH_VALID | IA64_THREAD_DBG_VALID);

	if (ia64_get_fpu_owner() == current) {
		ia64_set_fpu_owner(0);
	}
}

/*
 * Clean up state associated with current thread.  This is called when
 * the thread calls exit().
 */
void
exit_thread (void)
{
	if (ia64_get_fpu_owner() == current) {
		ia64_set_fpu_owner(0);
	}
}

/*
 * Free remaining state associated with DEAD_TASK.  This is called
 * after the parent of DEAD_TASK has collected the exist status of the
 * task via wait().
 */
void
release_thread (struct task_struct *dead_task)
{
	/* nothing to do */
}

unsigned long
get_wchan (struct task_struct *p)
{
	struct ia64_frame_info info;
	unsigned long ip;
	int count = 0;
	/*
	 * These bracket the sleeping functions..
	 */
	extern void scheduling_functions_start_here(void);
	extern void scheduling_functions_end_here(void);
#	define first_sched	((unsigned long) scheduling_functions_start_here)
#	define last_sched	((unsigned long) scheduling_functions_end_here)

	/*
	 * Note: p may not be a blocked task (it could be current or
	 * another process running on some other CPU.  Rather than
	 * trying to determine if p is really blocked, we just assume
	 * it's blocked and rely on the unwind routines to fail
	 * gracefully if the process wasn't really blocked after all.
	 * --davidm 99/12/15
	 */
	ia64_unwind_init_from_blocked_task(&info, p);
	do {
		if (ia64_unwind_to_previous_frame(&info) < 0)
			return 0;
		ip = ia64_unwind_get_ip(&info);
		if (ip < first_sched || ip >= last_sched)
			return ip;
	} while (count++ < 16);
	return 0;
#	undef first_sched
#	undef last_sched
}

void
machine_restart (char *restart_cmd)
{
	(*efi.reset_system)(EFI_RESET_WARM, 0, 0, 0);
}

void
machine_halt (void)
{
	printk("machine_halt: need PAL or ACPI version here!!\n");
	machine_restart(0);
}

void
machine_power_off (void)
{
	printk("machine_power_off: unimplemented (need ACPI version here)\n");
	machine_halt ();
}

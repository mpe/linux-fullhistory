/*  $Id: process.c,v 1.29 1997/07/17 02:20:40 davem Exp $
 *  arch/sparc64/kernel/process.c
 *
 *  Copyright (C) 1995, 1996 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1996 Eddie C. Dost   (ecd@skynet.be)
 *  Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
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
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/config.h>
#include <linux/reboot.h>
#include <linux/delay.h>

#include <asm/oplib.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/pstate.h>
#include <asm/elf.h>
#include <asm/fpumacro.h>

#define PGTCACHE_HIGH_WATER		50
#define PGTCACHE_LOW_WATER		25

#ifndef __SMP__

/*
 * the idle loop on a Sparc... ;)
 */
asmlinkage int sys_idle(void)
{
	if (current->pid != 0)
		return -EPERM;

	/* endless idle loop with no priority at all */
	current->priority = -100;
	current->counter = -100;
	for (;;) {
		if(pgtable_cache_size > PGTCACHE_LOW_WATER) {
			do {
				if(pgd_quicklist)
					free_page((unsigned long) get_pgd_fast());
				if(pmd_quicklist)
					free_page((unsigned long) get_pmd_fast());
				if(pte_quicklist)
					free_page((unsigned long) get_pte_fast());
			} while(pgtable_cache_size > PGTCACHE_HIGH_WATER);
		}
		run_task_queue(&tq_scheduler);
		schedule();
	}
	return 0;
}

#else

/*
 * the idle loop on a UltraMultiPenguin...
 */
asmlinkage int cpu_idle(void)
{
	current->priority = -100;
	while(1) {
		if(pgtable_cache_size > PGTCACHE_LOW_WATER) {
			do {
				if(pgd_quicklist)
					free_page((unsigned long) get_pgd_fast());
				if(pmd_quicklist)
					free_page((unsigned long) get_pmd_fast());
				if(pte_quicklist)
					free_page((unsigned long) get_pte_fast());
			} while(pgtable_cache_size > PGTCACHE_HIGH_WATER);
		}
		if(tq_scheduler) {
			lock_kernel();
			run_task_queue(&tq_scheduler);
			unlock_kernel();
		}
		barrier();
		current->counter = -100;
		if(resched_needed())
			schedule();
		barrier();
	}
}

asmlinkage int sys_idle(void)
{
	if(current->pid != 0)
		return -EPERM;

	cpu_idle();
	return 0;
}

#endif

extern char reboot_command [];

#ifdef CONFIG_SUN_CONSOLE
extern void console_restore_palette (void);
extern int serial_console;
#endif

void machine_halt(void)
{
	sti();
	udelay(8000);
	cli();
#ifdef CONFIG_SUN_CONSOLE
	if (!serial_console)
		console_restore_palette ();
#endif
	prom_halt();
	panic("Halt failed!");
}

void machine_restart(char * cmd)
{
	char *p;
	
	sti();
	udelay(8000);
	cli();

	p = strchr (reboot_command, '\n');
	if (p) *p = 0;
#ifdef CONFIG_SUN_CONSOLE
	if (!serial_console)
		console_restore_palette ();
#endif
	if (cmd)
		prom_reboot(cmd);
	if (*reboot_command)
		prom_reboot(reboot_command);
	prom_reboot("");
	panic("Reboot failed!");
}

void machine_power_off(void)
{
	machine_halt();
}

static void show_regwindow32(struct pt_regs *regs)
{
	struct reg_window32 *rw;
	struct reg_window32 r_w;
	unsigned long old_fs;
	
	__asm__ __volatile__ ("flushw");
	rw = (struct reg_window32 *)((long)(unsigned)regs->u_regs[14]);
	old_fs = get_fs();
	set_fs (USER_DS);
	if (copy_from_user (&r_w, rw, sizeof(r_w))) {
		set_fs (old_fs);
		return;
	}
	rw = &r_w;
	set_fs (old_fs);			
	printk("l0: %016x l1: %016x l2: %016x l3: %016x\n"
	       "l4: %016x l5: %016x l6: %016x l7: %016x\n",
	       rw->locals[0], rw->locals[1], rw->locals[2], rw->locals[3],
	       rw->locals[4], rw->locals[5], rw->locals[6], rw->locals[7]);
	printk("i0: %016x i1: %016x i2: %016x i3: %016x\n"
	       "i4: %016x i5: %016x i6: %016x i7: %016x\n",
	       rw->ins[0], rw->ins[1], rw->ins[2], rw->ins[3],
	       rw->ins[4], rw->ins[5], rw->ins[6], rw->ins[7]);
}

static void show_regwindow(struct pt_regs *regs)
{
	struct reg_window *rw;
	struct reg_window r_w;
	unsigned long old_fs;

	if ((regs->tstate & TSTATE_PRIV) || !(current->tss.flags & SPARC_FLAG_32BIT)) {
		__asm__ __volatile__ ("flushw");
		rw = (struct reg_window *)(regs->u_regs[14] + STACK_BIAS);
		if (!(regs->tstate & TSTATE_PRIV)) {
			old_fs = get_fs();
			set_fs (USER_DS);
			if (copy_from_user (&r_w, rw, sizeof(r_w))) {
				set_fs (old_fs);
				return;
			}
			rw = &r_w;
			set_fs (old_fs);			
		}
	} else {
		show_regwindow32(regs);
		return;
	}
	printk("l0: %016lx l1: %016lx l2: %016lx l3: %016lx\n",
	       rw->locals[0], rw->locals[1], rw->locals[2], rw->locals[3]);
	printk("l4: %016lx l5: %016lx l6: %016lx l7: %016lx\n",
	       rw->locals[4], rw->locals[5], rw->locals[6], rw->locals[7]);
	printk("i0: %016lx i1: %016lx i2: %016lx i3: %016lx\n",
	       rw->ins[0], rw->ins[1], rw->ins[2], rw->ins[3]);
	printk("i4: %016lx i5: %016lx i6: %016lx i7: %016lx\n",
	       rw->ins[4], rw->ins[5], rw->ins[6], rw->ins[7]);
}

void show_stackframe(struct sparc_stackf *sf)
{
	unsigned long size;
	unsigned long *stk;
	int i;

	printk("l0: %016lx l1: %016lx l2: %016lx l3: %016lx\n"
	       "l4: %016lx l5: %016lx l6: %016lx l7: %016lx\n",
	       sf->locals[0], sf->locals[1], sf->locals[2], sf->locals[3],
	       sf->locals[4], sf->locals[5], sf->locals[6], sf->locals[7]);
	printk("i0: %016lx i1: %016lx i2: %016lx i3: %016lx\n"
	       "i4: %016lx i5: %016lx fp: %016lx ret_pc: %016lx\n",
	       sf->ins[0], sf->ins[1], sf->ins[2], sf->ins[3],
	       sf->ins[4], sf->ins[5], (unsigned long)sf->fp, sf->callers_pc);
	printk("sp: %016lx x0: %016lx x1: %016lx x2: %016lx\n"
	       "x3: %016lx x4: %016lx x5: %016lx xx: %016lx\n",
	       (unsigned long)sf->structptr, sf->xargs[0], sf->xargs[1],
	       sf->xargs[2], sf->xargs[3], sf->xargs[4], sf->xargs[5],
	       sf->xxargs[0]);
	size = ((unsigned long)sf->fp) - ((unsigned long)sf);
	size -= STACKFRAME_SZ;
	stk = (unsigned long *)((unsigned long)sf + STACKFRAME_SZ);
	i = 0;
	do {
		printk("s%d: %016lx\n", i++, *stk++);
	} while ((size -= sizeof(unsigned long)));
}

void show_stackframe32(struct sparc_stackf32 *sf)
{
	unsigned long size;
	unsigned *stk;
	int i;

	printk("l0: %08x l1: %08x l2: %08x l3: %08x\n",
	       sf->locals[0], sf->locals[1], sf->locals[2], sf->locals[3]);
	printk("l4: %08x l5: %08x l6: %08x l7: %08x\n",
	       sf->locals[4], sf->locals[5], sf->locals[6], sf->locals[7]);
	printk("i0: %08x i1: %08x i2: %08x i3: %08x\n",
	       sf->ins[0], sf->ins[1], sf->ins[2], sf->ins[3]);
	printk("i4: %08x i5: %08x fp: %08x ret_pc: %08x\n",
	       sf->ins[4], sf->ins[5], sf->fp, sf->callers_pc);
	printk("sp: %08x x0: %08x x1: %08x x2: %08x\n"
	       "x3: %08x x4: %08x x5: %08x xx: %08x\n",
	       sf->structptr, sf->xargs[0], sf->xargs[1],
	       sf->xargs[2], sf->xargs[3], sf->xargs[4], sf->xargs[5],
	       sf->xxargs[0]);
	size = ((unsigned long)sf->fp) - ((unsigned long)sf);
	size -= STACKFRAME32_SZ;
	stk = (unsigned *)((unsigned long)sf + STACKFRAME32_SZ);
	i = 0;
	do {
		printk("s%d: %08x\n", i++, *stk++);
	} while ((size -= sizeof(unsigned)));
}

#ifdef __SMP__
static spinlock_t regdump_lock = SPIN_LOCK_UNLOCKED;
#endif

void __show_regs(struct pt_regs * regs)
{
#ifdef __SMP__
	unsigned long flags;

	spin_lock_irqsave(&regdump_lock, flags);
	printk("CPU[%d]: local_irq_count[%ld] global_irq_count[%d]\n",
	       smp_processor_id(), local_irq_count,
	       atomic_read(&global_irq_count));
#endif
        printk("TSTATE: %016lx TPC: %016lx TNPC: %016lx Y: %08x\n", regs->tstate,
	       regs->tpc, regs->tnpc, regs->y);
	printk("g0: %016lx g1: %016lx g2: %016lx g3: %016lx\n",
	       regs->u_regs[0], regs->u_regs[1], regs->u_regs[2],
	       regs->u_regs[3]);
	printk("g4: %016lx g5: %016lx g6: %016lx g7: %016lx\n",
	       regs->u_regs[4], regs->u_regs[5], regs->u_regs[6],
	       regs->u_regs[7]);
	printk("o0: %016lx o1: %016lx o2: %016lx o3: %016lx\n",
	       regs->u_regs[8], regs->u_regs[9], regs->u_regs[10],
	       regs->u_regs[11]);
	printk("o4: %016lx o5: %016lx sp: %016lx ret_pc: %016lx\n",
	       regs->u_regs[12], regs->u_regs[13], regs->u_regs[14],
	       regs->u_regs[15]);
	show_regwindow(regs);
#ifdef __SMP__
	spin_unlock_irqrestore(&regdump_lock, flags);
#endif
}

void show_regs(struct pt_regs *regs)
{
	__show_regs(regs);
#ifdef __SMP__
	{
		extern void smp_report_regs(void);

		smp_report_regs();
	}
#endif
}

void show_regs32(struct pt_regs32 *regs)
{
        printk("PSR: %08x PC: %08x NPC: %08x Y: %08x\n", regs->psr,
	       regs->pc, regs->npc, regs->y);
	printk("g0: %08x g1: %08x g2: %08x g3: %08x\n",
	       regs->u_regs[0], regs->u_regs[1], regs->u_regs[2],
	       regs->u_regs[3]);
	printk("g4: %08x g5: %08x g6: %08x g7: %08x\n",
	       regs->u_regs[4], regs->u_regs[5], regs->u_regs[6],
	       regs->u_regs[7]);
	printk("o0: %08x o1: %08x o2: %08x o3: %08x\n",
	       regs->u_regs[8], regs->u_regs[9], regs->u_regs[10],
	       regs->u_regs[11]);
	printk("o4: %08x o5: %08x sp: %08x ret_pc: %08x\n",
	       regs->u_regs[12], regs->u_regs[13], regs->u_regs[14],
	       regs->u_regs[15]);
}

void show_thread(struct thread_struct *tss)
{
	int i;

#if 0
	printk("kregs:             0x%016lx\n", (unsigned long)tss->kregs);
	show_regs(tss->kregs);
#endif	
	printk("sig_address:       0x%016lx\n", tss->sig_address);
	printk("sig_desc:          0x%016lx\n", tss->sig_desc);
	printk("ksp:               0x%016lx\n", tss->ksp);
	printk("kpc:               0x%016lx\n", tss->kpc);

	for (i = 0; i < NSWINS; i++) {
		if (!tss->rwbuf_stkptrs[i])
			continue;
		printk("reg_window[%d]:\n", i);
		printk("stack ptr:         0x%016lx\n", tss->rwbuf_stkptrs[i]);
	}
	printk("w_saved:           0x%08lx\n", tss->w_saved);

	printk("sstk_info.stack:   0x%016lx\n",
	        (unsigned long)tss->sstk_info.the_stack);
	printk("sstk_info.status:  0x%016lx\n",
	        (unsigned long)tss->sstk_info.cur_status);
	printk("flags:             0x%08x\n", tss->flags);
	printk("current_ds:        0x%016lx\n", tss->current_ds);

	/* XXX missing: core_exec */
}

/* Free current thread data structures etc.. */
void exit_thread(void)
{
}

void flush_thread(void)
{
	current->tss.w_saved = 0;
	current->tss.sstk_info.cur_status = 0;
	current->tss.sstk_info.the_stack = 0;

	/* No new signal delivery by default. */
	current->tss.new_signal = 0;
	current->flags &= ~PF_USEDFPU;
	
	/* Now, this task is no longer a kernel thread. */
	current->tss.current_ds = USER_DS;
	if(current->tss.flags & SPARC_FLAG_KTHREAD) {
		extern spinlock_t scheduler_lock;

		current->tss.flags &= ~SPARC_FLAG_KTHREAD;

		/* exec_mmap() set context to NO_CONTEXT, here is
		 * where we grab a new one.
		 */
		spin_lock(&scheduler_lock);
		get_mmu_context(current);
		spin_unlock(&scheduler_lock);
	}
	current->tss.ctx = current->mm->context & 0x1fff;
	spitfire_set_secondary_context (current->tss.ctx);
	__asm__ __volatile__("flush %g6");
}

/* It's a bit more tricky when 64-bit tasks are involved... */
static unsigned long clone_stackframe(unsigned long csp, unsigned long psp)
{
	unsigned long fp, distance, rval;

	if(!(current->tss.flags & SPARC_FLAG_32BIT)) {
		csp += STACK_BIAS;
		psp += STACK_BIAS;
		__get_user(fp, &(((struct reg_window *)psp)->ins[6]));
	} else
		__get_user(fp, &(((struct reg_window32 *)psp)->ins[6]));
	distance = fp - psp;
	rval = (csp - distance);
	if(copy_in_user(rval, psp, distance))
		return 0;
	if(current->tss.flags & SPARC_FLAG_32BIT) {
		if(put_user(((u32)csp), &(((struct reg_window32 *)rval)->ins[6])))
			return 0;
		return rval;
	} else {
		if(put_user(((u64)csp - STACK_BIAS),
			    &(((struct reg_window *)rval)->ins[6])))
			return 0;
		return rval - STACK_BIAS;
	}
}

/* Standard stuff. */
static inline void shift_window_buffer(int first_win, int last_win,
				       struct thread_struct *tp)
{
	int i;

	for(i = first_win; i < last_win; i++) {
		tp->rwbuf_stkptrs[i] = tp->rwbuf_stkptrs[i+1];
		memcpy(&tp->reg_window[i], &tp->reg_window[i+1],
		       sizeof(struct reg_window));
	}
}

void synchronize_user_stack(void)
{
	struct thread_struct *tp = &current->tss;
	unsigned long window;

	flush_user_windows();
	if((window = tp->w_saved) != 0) {
		int winsize = REGWIN_SZ;
		int bias = 0;

		if(tp->flags & SPARC_FLAG_32BIT)
			winsize = REGWIN32_SZ;
		else
			bias = STACK_BIAS;

		window -= 1;
		do {
			unsigned long sp = (tp->rwbuf_stkptrs[window] + bias);
			struct reg_window *rwin = &tp->reg_window[window];

			if(!copy_to_user((char *)sp, rwin, winsize)) {
				shift_window_buffer(window, tp->w_saved - 1, tp);
				tp->w_saved--;
			}
		} while(window--);
	}
}

void fault_in_user_windows(struct pt_regs *regs)
{
	struct thread_struct *tp = &current->tss;
	unsigned long window;
	int winsize = REGWIN_SZ;
	int bias = 0;

	if(tp->flags & SPARC_FLAG_32BIT)
		winsize = REGWIN32_SZ;
	else
		bias = STACK_BIAS;
	flush_user_windows();
	window = tp->w_saved;
	if(window != 0) {
		window -= 1;
		do {
			unsigned long sp = (tp->rwbuf_stkptrs[window] + bias);
			struct reg_window *rwin = &tp->reg_window[window];

			if(copy_to_user((char *)sp, rwin, winsize))
				goto barf;
		} while(window--);
	}
	current->tss.w_saved = 0;
	return;
barf:
	lock_kernel();
	do_exit(SIGILL);
}

/* Copy a Sparc thread.  The fork() return value conventions
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
#ifdef __SMP__
extern void ret_from_smpfork(void);
#else
extern void ret_from_syscall(void);
#endif

int copy_thread(int nr, unsigned long clone_flags, unsigned long sp,
		struct task_struct *p, struct pt_regs *regs)
{
	unsigned long stack_offset;
	char *child_trap_frame;
	int tframe_size;

	/* Calculate offset to stack_frame & pt_regs */
	stack_offset = (((PAGE_SIZE << 1) -
			((sizeof(unsigned int)*64) + (2*sizeof(unsigned long)))) &
		~(64 - 1)) - (TRACEREG_SZ+REGWIN_SZ);
	tframe_size = (TRACEREG_SZ + REGWIN_SZ) +
		(sizeof(unsigned int) * 64) + (2 * sizeof(unsigned long));
	child_trap_frame = ((char *)p) + stack_offset;
	memcpy(child_trap_frame, (((struct reg_window *)regs)-1), tframe_size);
	p->tss.ksp = ((unsigned long) child_trap_frame) - STACK_BIAS;
#ifdef __SMP__
	p->tss.kpc = ((unsigned long) ret_from_smpfork) - 0x8;
#else
	p->tss.kpc = ((unsigned long) ret_from_syscall) - 0x8;
#endif
	p->tss.kregs = (struct pt_regs *)(child_trap_frame+sizeof(struct reg_window));
	p->tss.cwp = (regs->tstate + 1) & TSTATE_CWP;
	if(regs->tstate & TSTATE_PRIV) {
		p->tss.kregs->u_regs[UREG_FP] = p->tss.ksp;
		p->tss.flags |= SPARC_FLAG_KTHREAD;
		p->tss.current_ds = KERNEL_DS;
		p->tss.ctx = 0;
		p->tss.kregs->u_regs[UREG_G6] = (unsigned long) p;
	} else {
		if(current->tss.flags & SPARC_FLAG_32BIT) {
			sp &= 0x00000000ffffffff;
			regs->u_regs[UREG_FP] &= 0x00000000ffffffff;
		}
		p->tss.kregs->u_regs[UREG_FP] = sp;
		p->tss.flags &= ~SPARC_FLAG_KTHREAD;
		p->tss.current_ds = USER_DS;
		p->tss.ctx = (p->mm->context & 0x1fff);
		if (sp != regs->u_regs[UREG_FP]) {
			unsigned long csp;

			csp = clone_stackframe(sp, regs->u_regs[UREG_FP]);
			if(!csp)
				return -EFAULT;
			p->tss.kregs->u_regs[UREG_FP] = csp;
		}
	}

	/* Set the return value for the child. */
	p->tss.kregs->u_regs[UREG_I0] = current->pid;
	p->tss.kregs->u_regs[UREG_I1] = 1;

	/* Set the second return value for the parent. */
	regs->u_regs[UREG_I1] = 0;
	return 0;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
#if 0
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
	dump->sigcode = current->tss.sig_desc;
#endif	
}

/*
 * fill in the fpu structure for a core dump.
 */
int dump_fpu (struct pt_regs * regs, elf_fpregset_t * fpregs)
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
	int error, base = 0;
	char *filename;

	/* Check for indirect call. */
	if(regs->u_regs[UREG_G1] == 0)
		base = 1;

	lock_kernel();
	filename = getname((char *)regs->u_regs[base + UREG_I0]);
	error = PTR_ERR(filename);
	if(IS_ERR(filename))
		goto out;
	error = do_execve(filename, (char **) regs->u_regs[base + UREG_I1],
			  (char **) regs->u_regs[base + UREG_I2], regs);
	putname(filename);
	if(!error) {
		fprs_write(0);
		regs->fprs = 0;
	}
out:
	unlock_kernel();
	return error;
}

/* $Id: signal.c,v 1.24 1998/09/16 22:50:42 ralf Exp $
 *
 *  linux/arch/mips/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1994, 1995, 1996, 1997, 1998  Ralf Baechle
 *
 * XXX Handle lazy fp context switches correctly.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>

#include <asm/asm.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/stackframe.h>
#include <asm/uaccess.h>

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

asmlinkage int sys_wait4(pid_t pid, unsigned long *stat_addr,
                         int options, unsigned long *ru);
asmlinkage int do_signal(sigset_t *oldset, struct pt_regs *regs);
extern asmlinkage void (*save_fp_context)(struct sigcontext *sc);
extern asmlinkage void (*restore_fp_context)(struct sigcontext *sc);

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage inline int
sys_sigsuspend(struct pt_regs regs)
{
	sigset_t *uset, saveset, newset;

	save_static(&regs);
	uset = (sigset_t *) regs.regs[4];
	if (copy_from_user(&newset, uset, sizeof(sigset_t)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	current->blocked = newset;
	spin_unlock_irq(&current->sigmask_lock);

	regs.regs[2] = EINTR;
	regs.regs[7] = 1;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, &regs))
			return -EINTR;
	}
}

asmlinkage int
sys_rt_sigsuspend(struct pt_regs regs)
{
	sigset_t *uset, saveset, newset;

	save_static(&regs);
	uset = (sigset_t *) regs.regs[4];
	if (copy_from_user(&newset, uset, sizeof(sigset_t)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	current->blocked = newset;
	spin_unlock_irq(&current->sigmask_lock);

	regs.regs[2] = EINTR;
	regs.regs[7] = 1;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, &regs))
			return -EINTR;
	}
}

asmlinkage int 
sys_sigaction(int sig, const struct sigaction *act,
	      struct sigaction *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		sigset_t mask;
		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags))
			return -EFAULT;

		__copy_from_user(&mask, &act->sa_mask, sizeof(sigset_t));
		new_ka.ka_restorer = NULL;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (verify_area(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_flags, &oact->sa_flags))
			return -EFAULT;
		__copy_to_user(&old_ka.sa.sa_mask, &oact->sa_mask,
		               sizeof(sigset_t));
	}

	return ret;
}

asmlinkage int
sys_sigaltstack(const stack_t *uss, stack_t *uoss)
{
	struct pt_regs *regs = (struct pt_regs *) &uss;

	return do_sigaltstack(uss, uoss, regs->regs[29]);
}

/*
 * To do: this entire function should be accessed over a function pointer
 * such that we can handle stack frames for different ABIs.
 */

asmlinkage void
restore_sigcontext(struct pt_regs *regs, struct sigcontext *context)
{
	long long reg;
	int owned_fp;

	__get_user(regs->cp0_epc, &context->sc_pc);

	__get_user(reg, &context->sc_mdhi);
	regs->hi = (int) reg;
	__get_user(reg, &context->sc_mdlo);
	regs->lo = (int) reg;

#define restore_gp_reg(i) __get_user(reg, &context->sc_regs[i]); \
			  regs->regs[i] = (int) reg;
	restore_gp_reg( 1); restore_gp_reg( 2); restore_gp_reg( 3);
	restore_gp_reg( 4); restore_gp_reg( 5); restore_gp_reg( 6);
	restore_gp_reg( 7); restore_gp_reg( 8); restore_gp_reg( 9);
	restore_gp_reg(10); restore_gp_reg(11); restore_gp_reg(12);
	restore_gp_reg(13); restore_gp_reg(14); restore_gp_reg(15);
	restore_gp_reg(16); restore_gp_reg(17); restore_gp_reg(18);
	restore_gp_reg(19); restore_gp_reg(20); restore_gp_reg(21);
	restore_gp_reg(22); restore_gp_reg(23); restore_gp_reg(24);
	restore_gp_reg(25); restore_gp_reg(26); restore_gp_reg(27);
	restore_gp_reg(28); restore_gp_reg(29); restore_gp_reg(30);
	restore_gp_reg(31);
#undef restore_gp_reg

	/* FP depends on what FPU in what mode we have.  */
	__get_user(owned_fp, &context->sc_ownedfp);
#if 0
	if (owned_fp) {
		restore_fp_context(context);
		last_task_used_math = current;
	}
#endif
restore_fp_context(context);
}

/*
 * The structure sc describes the stackframe on the userstack.  The frames
 * are identical for normal and realtime signal stackframes with the
 * exception of the additional struct ucontext for rt frames.
 */
struct sigframe {
	unsigned long	ass[4];		/* argument save space for o32 */
	unsigned int	code[4];	/* signal trampoline */
	struct sigcontext scc;
};

struct rt_sigframe {
	unsigned long	ass[4];
	unsigned int	code[4];
	struct sigcontext scc;
	// struct ucontext uc;
};

asmlinkage int sys_sigreturn(struct pt_regs regs)
{
	struct sigcontext *context;
	sigset_t blocked;

	context = (struct sigcontext *)(long) regs.regs[29];
	if (!access_ok(VERIFY_READ, context, sizeof(struct sigcontext)) ||
	    (regs.regs[29] & (SZREG - 1)))
		goto badframe;

#if 1
	if (__get_user(blocked.sig[0], &context->sc_sigset[0]) ||
	    __get_user(blocked.sig[1], &context->sc_sigset[1]) ||
	    __get_user(blocked.sig[2], &context->sc_sigset[2]) ||
	    __get_user(blocked.sig[3], &context->sc_sigset[3]))
		goto badframe;
#else
	if (__copy_from_user(&blocked, &context->sc_sigset, sizeof(blocked)))
		goto badframe;
#endif

	sigdelsetmask(&blocked, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = blocked;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	restore_sigcontext(&regs, context);

	/*
	 * Don't let your children do this ...
	 */
	__asm__ __volatile__(
		"move\t$29,%0\n\t"
		"j\tret_from_sys_call"
		:/* no outputs */
		:"r" (&regs));
	/* Unreached */

badframe:
	lock_kernel();
	do_exit(SIGSEGV);
	unlock_kernel();
}

/* same as sys_sigreturn for now */
asmlinkage int sys_rt_sigreturn(struct pt_regs regs)
{
	return -ENOSYS;
}

#define scc_offset ((size_t)&((struct sigframe *)0)->scc)

/*
 * Set up the return code ...
 *
 *         .set    noreorder
 *         addiu   sp,0x20
 *         li      v0,__NR_sigreturn
 *         syscall
 *         .set    reorder
 */
static void inline
setup_trampoline(unsigned int *code)
{
	__put_user(0x27bd0000 + scc_offset    , code + 0);
	__put_user(0x24020000 + __NR_sigreturn, code + 1);
	__put_user(0x0000000c                 , code + 2);

	/*
	 * Flush caches so that the instructions will be correctly executed.
	 */
	flush_cache_sigtramp((unsigned long) code);
}

static void inline
setup_sigcontext(struct pt_regs *regs, struct sigcontext *sc, sigset_t *set)
{
	int owned_fp;

	__put_user(regs->cp0_epc, &sc->sc_pc);
	__put_user(regs->cp0_status, &sc->sc_status);	/* Status register */

#define save_gp_reg(i) __put_user(regs->regs[(i)], &sc->sc_regs[(i)])
	__put_user(0, &sc->sc_regs[0]); save_gp_reg(1); save_gp_reg(2);
	save_gp_reg(3); save_gp_reg(4); save_gp_reg(5); save_gp_reg(6);
	save_gp_reg(7); save_gp_reg(8); save_gp_reg(9); save_gp_reg(10);
	save_gp_reg(11); save_gp_reg(12); save_gp_reg(13); save_gp_reg(14);
	save_gp_reg(15); save_gp_reg(16); save_gp_reg(17); save_gp_reg(18);
	save_gp_reg(19); save_gp_reg(20); save_gp_reg(21); save_gp_reg(22);
	save_gp_reg(23); save_gp_reg(24); save_gp_reg(25); save_gp_reg(26);
	save_gp_reg(27); save_gp_reg(28); save_gp_reg(29); save_gp_reg(30);
	save_gp_reg(31);
#undef save_gp_reg

	__put_user(regs->hi, &sc->sc_mdhi);
	__put_user(regs->lo, &sc->sc_mdlo);
	__put_user(regs->cp0_cause, &sc->sc_cause);

	owned_fp = (current == last_task_used_math);
	__put_user(owned_fp, &sc->sc_ownedfp);

#if 0
	if (current->used_math) {	/* fp is active.  */
		set_cp0_status(ST0_CU1, ST0_CU1);
		save_fp_context(sc);		/* CPU-dependent */
		last_task_used_math = NULL;
		regs->cp0_status &= ~ST0_CU1;
		current->used_math = 0;
	}
#endif
set_cp0_status(ST0_CU1, ST0_CU1);
save_fp_context(sc);		/* CPU-dependent */

	__put_user(set->sig[0], &sc->sc_sigset[0]);
	__put_user(set->sig[1], &sc->sc_sigset[1]);
	__put_user(set->sig[2], &sc->sc_sigset[2]);
	__put_user(set->sig[3], &sc->sc_sigset[3]);
}

static void inline
setup_frame(struct k_sigaction * ka, struct pt_regs *regs,
            int signr, sigset_t *oldmask)
{
	struct sigframe *frame;
	struct sigcontext *sc;

	/* Align the stackframe to an adequate boundary for the architecture. */
	frame = (struct sigframe *) (long) regs->regs[29];
	frame--;
	frame = (struct sigframe *)((unsigned long)frame & ALMASK);

	if (verify_area(VERIFY_WRITE, frame, sizeof (*frame)))
		goto segv_and_exit;
	sc = &frame->scc;

	setup_trampoline(frame->code);
	setup_sigcontext(regs, &frame->scc, oldmask);

	regs->regs[4] = signr;				/* arguments */
        regs->regs[5] = 0;                              /* should be cause  */
        regs->regs[6] = (long) frame;                   /* ptr to sigcontext */
        regs->regs[29] = (unsigned long) frame;         /* Stack pointer */
        regs->regs[31] = (unsigned long) frame->code;   /* Return address */
        regs->cp0_epc = (unsigned long) ka->sa.sa_handler; /* handler address */
        regs->regs[25] = regs->cp0_epc;                 /* PIC shit... */

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=0x%p pc=0x%p ra=0x%p\n",
	       current->comm, current->pid, frame, regs->cp0_epc, frame->code);
#endif
        return;

segv_and_exit:
	lock_kernel();
	do_exit(SIGSEGV);
	unlock_kernel();
}

static void inline
setup_rt_frame(struct k_sigaction * ka, struct pt_regs *regs,
               int signr, sigset_t *oldmask, siginfo_t *info)
{
	printk("Aiee: setup_tr_frame wants to be written");
}

/* ------------------------------------------------------------------------- */

static inline void handle_signal(unsigned long sig, struct k_sigaction *ka,
	siginfo_t *info, sigset_t *oldset, struct pt_regs * regs)
{
	if (ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame(ka, regs, sig, oldset, info);
	else
		setup_frame(ka, regs, sig, oldset);

	if (ka->sa.sa_flags & SA_ONESHOT)
		ka->sa.sa_handler = SIG_DFL;
	if (!(ka->sa.sa_flags & SA_NODEFER)) {
		spin_lock_irq(&current->sigmask_lock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,sig);
		recalc_sigpending(current);
		spin_unlock_irq(&current->sigmask_lock);
	}
}

static inline void syscall_restart(struct pt_regs *regs, struct k_sigaction *ka)
{
	switch(regs->regs[0]) {
	case ERESTARTNOHAND:
		regs->regs[2] = EINTR;
		break;
	case ERESTARTSYS:
		if(!(ka->sa.sa_flags & SA_RESTART)) {
			regs->regs[2] = EINTR;
			break;
		}
	/* fallthrough */
	case ERESTARTNOINTR:		/* Userland will reload $v0.  */
		regs->cp0_epc -= 8;
	}

	regs->regs[0] = 0;		/* Don't deal with this again.  */
}

extern int do_irix_signal(sigset_t *oldset, struct pt_regs *regs);

asmlinkage int do_signal(sigset_t *oldset, struct pt_regs *regs)
{
	struct k_sigaction *ka;
	siginfo_t info;

#ifdef CONFIG_BINFMT_IRIX
	if (current->personality != PER_LINUX)           /* XXX */
		return do_irix_signal(oldset, regs);
#endif

	if (!oldset)
		oldset = &current->blocked;

	for (;;) {
		unsigned long signr;

		spin_lock_irq(&current->sigmask_lock);
		signr = dequeue_signal(&current->blocked, &info);
		spin_unlock_irq(&current->sigmask_lock);

		if (!signr)
			break;

		if ((current->flags & PF_PTRACED) && signr != SIGKILL) {
			/* Let the debugger run.  */
			current->exit_code = signr;
			current->state = TASK_STOPPED;
			notify_parent(current, SIGCHLD);
			schedule();

			/* We're back.  Did the debugger cancel the sig?  */
			if (!(signr = current->exit_code))
				continue;
			current->exit_code = 0;

			/* The debugger continued.  Ignore SIGSTOP.  */
			if (signr == SIGSTOP)
				continue;

			/* Update the siginfo structure.  Is this good?  */
			if (signr != info.si_signo) {
				info.si_signo = signr;
				info.si_errno = 0;
				info.si_code = SI_USER;
				info.si_pid = current->p_pptr->pid;
				info.si_uid = current->p_pptr->uid;
			}

			/* If the (new) signal is now blocked, requeue it.  */
			if (sigismember(&current->blocked, signr)) {
				send_sig_info(signr, &info, current);
				continue;
			}
		}

		ka = &current->sig->action[signr-1];
		if (ka->sa.sa_handler == SIG_IGN) {
			if (signr != SIGCHLD)
				continue;
			/* Check for SIGCHLD: it's special.  */
			while (sys_wait4(-1, NULL, WNOHANG, NULL) > 0)
				/* nothing */;
			continue;
		}

		if (ka->sa.sa_handler == SIG_DFL) {
			int exit_code = signr;

			/* Init gets no signals it doesn't want.  */
			if (current->pid == 1)
				continue;

			switch (signr) {
			case SIGCONT: case SIGCHLD: case SIGWINCH:
				continue;

			case SIGTSTP: case SIGTTIN: case SIGTTOU:
				if (is_orphaned_pgrp(current->pgrp))
					continue;
				/* FALLTHRU */

			case SIGSTOP:
				current->state = TASK_STOPPED;
				current->exit_code = signr;
				if (!(current->p_pptr->sig->action[SIGCHLD-1].sa.sa_flags & SA_NOCLDSTOP))
					notify_parent(current, SIGCHLD);
				schedule();
				continue;

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGABRT: case SIGFPE: case SIGSEGV:
				lock_kernel();
				if (current->binfmt
				    && current->binfmt->core_dump
				    && current->binfmt->core_dump(signr, regs))
					exit_code |= 0x80;
				unlock_kernel();
				/* FALLTHRU */

			default:
				lock_kernel();
				sigaddset(&current->signal, signr);
				current->flags |= PF_SIGNALED;
				do_exit(exit_code);
				/* NOTREACHED */
			}
		}

		if (regs->regs[0])
			syscall_restart(regs, ka);
		/* Whee!  Actually deliver the signal.  */
		handle_signal(signr, ka, &info, oldset, regs);
		return 1;
	}

	/*
	 * Who's code doesn't conform to the restartable syscall convention
	 * dies here!!!  The li instruction, a single machine instruction,
	 * must directly be followed by the syscall instruction.
	 */
	if (regs->regs[0]) {
		if (regs->regs[2] == ERESTARTNOHAND ||
		    regs->regs[2] == ERESTARTSYS ||
		    regs->regs[2] == ERESTARTNOINTR) {
			regs->cp0_epc -= 8;
		}
	}
	return 0;
}

/*
 * Compatibility syscall.  Can be replaced in libc.
 */
asmlinkage int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

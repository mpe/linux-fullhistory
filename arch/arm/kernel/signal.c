/*
 *  linux/arch/arm/kernel/signal.c
 *
 *  Copyright (C) 1995, 1996 Russell King
 */

#include <linux/config.h> /* for CONFIG_CPU_ARM6 and CONFIG_CPU_SA110 */
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
#include <linux/stddef.h>

#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

#define SWI_SYS_SIGRETURN (0xef000000|(__NR_sigreturn))
#define SWI_SYS_RT_SIGRETURN (0xef000000|(__NR_rt_sigreturn))

asmlinkage int sys_wait4(pid_t pid, unsigned long * stat_addr,
			 int options, unsigned long *ru);
asmlinkage int do_signal(sigset_t *oldset, struct pt_regs * regs);
extern int ptrace_cancel_bpt (struct task_struct *);
extern int ptrace_set_bpt (struct task_struct *);

/*
 * atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int sys_sigsuspend(int restart, unsigned long oldmask, old_sigset_t mask, struct pt_regs *regs)
{

	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	regs->ARM_r0 = -EINTR;

	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs))
			return regs->ARM_r0;
	}
}

asmlinkage int
sys_rt_sigsuspend(sigset_t *unewset, size_t sigsetsize, struct pt_regs *regs)
{
	sigset_t saveset, newset;

	/* XXX: Don't preclude handling different sized sigset_t's. */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	regs->ARM_r0 = -EINTR;

	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs))
			return regs->ARM_r0;
	}
}

asmlinkage int 
sys_sigaction(int sig, const struct old_sigaction *act,
	      struct old_sigaction *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (verify_area(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

/*
 * Do a signal return; undo the signal stack.
 */
struct sigframe
{
	struct sigcontext sc;
	unsigned long extramask[_NSIG_WORDS-1];
	unsigned long retcode;
};

struct rt_sigframe
{
	struct siginfo *pinfo;
	void *puc;
	struct siginfo info;
	struct ucontext uc;
	unsigned long retcode;
};

static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext *sc)
{
	__get_user(regs->ARM_r0, &sc->arm_r0);
	__get_user(regs->ARM_r1, &sc->arm_r1);
	__get_user(regs->ARM_r2, &sc->arm_r2);
	__get_user(regs->ARM_r3, &sc->arm_r3);
	__get_user(regs->ARM_r4, &sc->arm_r4);
	__get_user(regs->ARM_r5, &sc->arm_r5);
	__get_user(regs->ARM_r6, &sc->arm_r6);
	__get_user(regs->ARM_r7, &sc->arm_r7);
	__get_user(regs->ARM_r8, &sc->arm_r8);
	__get_user(regs->ARM_r9, &sc->arm_r9);
	__get_user(regs->ARM_r10, &sc->arm_r10);
	__get_user(regs->ARM_fp, &sc->arm_fp);
	__get_user(regs->ARM_ip, &sc->arm_ip);
	__get_user(regs->ARM_sp, &sc->arm_sp);
	__get_user(regs->ARM_lr, &sc->arm_lr);
	__get_user(regs->ARM_pc, &sc->arm_pc);		/* security! */
#ifdef CONFIG_CPU_32
	__get_user(regs->ARM_cpsr, &sc->arm_cpsr);	/* security! */
#endif

	/* send SIGTRAP if we're single-stepping */
	if (ptrace_cancel_bpt (current))
		send_sig (SIGTRAP, current, 1);

	return regs->ARM_r0;
}

asmlinkage int sys_sigreturn(struct pt_regs *regs)
{
	struct sigframe *frame;
	sigset_t set;

	frame = (struct sigframe *)regs->ARM_sp;

	if (verify_area(VERIFY_READ, frame, sizeof (*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.oldmask)
	    || (_NSIG_WORDS > 1
	        && __copy_from_user(&set.sig[1], &frame->extramask,
				    sizeof(frame->extramask))))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	return restore_sigcontext(regs, &frame->sc);

badframe:
	lock_kernel();
	do_exit(SIGSEGV);
}

asmlinkage int sys_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_sigframe *frame;
	sigset_t set;

	frame = (struct rt_sigframe *)regs->ARM_sp;

	if (verify_area(VERIFY_READ, frame, sizeof (*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	return restore_sigcontext(regs, &frame->uc.uc_mcontext);

badframe:
	lock_kernel();
	do_exit(SIGSEGV);
}

static void
setup_sigcontext(struct sigcontext *sc, /*struct _fpstate *fpstate,*/
		 struct pt_regs *regs, unsigned long mask)
{
	__put_user (regs->ARM_r0, &sc->arm_r0);
	__put_user (regs->ARM_r1, &sc->arm_r1);
	__put_user (regs->ARM_r2, &sc->arm_r2);
	__put_user (regs->ARM_r3, &sc->arm_r3);
	__put_user (regs->ARM_r4, &sc->arm_r4);
	__put_user (regs->ARM_r5, &sc->arm_r5);
	__put_user (regs->ARM_r6, &sc->arm_r6);
	__put_user (regs->ARM_r7, &sc->arm_r7);
	__put_user (regs->ARM_r8, &sc->arm_r8);
	__put_user (regs->ARM_r9, &sc->arm_r9);
	__put_user (regs->ARM_r10, &sc->arm_r10);
	__put_user (regs->ARM_fp, &sc->arm_fp);
	__put_user (regs->ARM_ip, &sc->arm_ip);
	__put_user (regs->ARM_sp, &sc->arm_sp);
	__put_user (regs->ARM_lr, &sc->arm_lr);
	__put_user (regs->ARM_pc, &sc->arm_pc);		/* security! */
#ifdef CONFIG_CPU_32
	__put_user (regs->ARM_cpsr, &sc->arm_cpsr);	/* security! */
#endif

	__put_user (current->tss.trap_no, &sc->trap_no);
	__put_user (current->tss.error_code, &sc->error_code);
	__put_user (mask, &sc->oldmask);
}

static void setup_frame(int sig, struct k_sigaction *ka,
			sigset_t *set, struct pt_regs *regs)
{
	struct sigframe *frame;
	unsigned long retcode;

	frame = (struct sigframe *)regs->ARM_sp - 1;

	if (!access_ok(VERIFT_WRITE, frame, sizeof (*frame)))
		goto segv_and_exit;

	setup_sigcontext(&frame->sc, /*&frame->fpstate,*/ regs, set->sig[0]);

	if (_NSIG_WORDS > 1) {
		__copy_to_user(frame->extramask, &set->sig[1],
			       sizeof(frame->extramask));
	}

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		retcode = (unsigned long)ka->sa.sa_restorer; /* security! */
	} else {
		retcode = (unsigned long)&frame->retcode;
		__put_user(SWI_SYS_SIGRETURN, &frame->retcode);
		__flush_entry_to_ram (&frame->retcode);
	}

	if (current->exec_domain && current->exec_domain->signal_invmap && sig < 32)
		regs->ARM_r0 = current->exec_domain->signal_invmap[sig];
	else
		regs->ARM_r0 = sig;
	regs->ARM_sp = (unsigned long)frame;
	regs->ARM_lr = retcode;
	regs->ARM_pc = (unsigned long)ka->sa.sa_handler;		/* security! */
	return;

segv_and_exit:
	lock_kernel();
	do_exit (SIGSEGV);
}

static void setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe *frame;
	unsigned long retcode;

	frame = (struct rt_sigframe *)regs->ARM_sp - 1;
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		goto segv_and_exit;

	__put_user(&frame->info, &frame->pinfo);
	__put_user(&frame->uc, &frame->puc);
	__copy_to_user(&frame->info, info, sizeof(*info));

	/* Clear all the bits of the ucontext we don't use.  */
	__clear_user(&frame->uc, offsetof(struct ucontext, uc_mcontext));

	setup_sigcontext(&frame->uc.uc_mcontext, /*&frame->fpstate,*/
			 regs, set->sig[0]);
	__copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		retcode = (unsigned long)ka->sa.sa_restorer; /* security! */
	} else {
		retcode = (unsigned long)&frame->retcode;
		__put_user(SWI_SYS_RT_SIGRETURN, &frame->retcode);
		__flush_entry_to_ram (&frame->retcode);
	}

	if (current->exec_domain && current->exec_domain->signal_invmap && sig < 32)
		regs->ARM_r0 = current->exec_domain->signal_invmap[sig];
	else
		regs->ARM_r0 = sig;
	regs->ARM_sp = (unsigned long)frame;
	regs->ARM_lr = retcode;
	regs->ARM_pc = (unsigned long)ka->sa.sa_handler;		/* security! */
	return;

segv_and_exit:
	lock_kernel();
	do_exit (SIGSEGV);
}

/*
 * OK, we're invoking a handler
 */	
static void
handle_signal(unsigned long sig, struct k_sigaction *ka,
	      siginfo_t *info, sigset_t *oldset, struct pt_regs * regs)
{
	/* Set up the stack frame */
	if (ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame(sig, ka, info, oldset, regs);
	else
		setup_frame(sig, ka, oldset, regs);

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

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals that
 * the kernel can handle, and then we build all the user-level signal handling
 * stack-frames in one go after that.
 */
asmlinkage int do_signal(sigset_t *oldset, struct pt_regs *regs)
{
	unsigned long instr, *pc = (unsigned long *)(instruction_pointer(regs)-4);
	struct k_sigaction *ka;
	siginfo_t info;
	int single_stepping, swi_instr;

	if (!oldset)
		oldset = &current->blocked;

	single_stepping = ptrace_cancel_bpt (current);
	swi_instr = (!get_user (instr, pc) && (instr & 0x0f000000) == 0x0f000000);

	for (;;) {
		unsigned long signr;

		spin_lock_irq (&current->sigmask_lock);
		signr = dequeue_signal(&current->blocked, &info);
		spin_unlock_irq (&current->sigmask_lock);

		if (!signr)
			break;

		if ((current->flags & PF_PTRACED) && signr != SIGKILL) {
			/* Let the debugger run.  */
			current->exit_code = signr;
			current->state = TASK_STOPPED;
			notify_parent(current, SIGCHLD);
			schedule();
			single_stepping |= ptrace_cancel_bpt (current);

			/* We're back.  Did the debugger cancel the sig?  */
			if (!(signr = current->exit_code))
				continue;
			current->exit_code = 0;

			/* The debugger continued.  Ignore SIGSTOP.  */
			if (signr == SIGSTOP)
				continue;

			/* Update the siginfo structure.  Is this good? */
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

		/* Are we from a system call? */
		if (swi_instr) {
			switch (regs->ARM_r0) {
			case -ERESTARTNOHAND:
				regs->ARM_r0 = -EINTR;
				break;

			case -ERESTARTSYS:
				if (!(ka->sa.sa_flags & SA_RESTART)) {
					regs->ARM_r0 = -EINTR;
					break;
				}
				/* fallthrough */
			case -ERESTARTNOINTR:
				regs->ARM_r0 = regs->ARM_ORIG_r0;
				regs->ARM_pc -= 4;
			}
		}
		/* Whee!  Actually deliver the signal.  */
		handle_signal(signr, ka, &info, oldset, regs);
		if (single_stepping)
		    	ptrace_set_bpt (current);
		return 1;
	}

	if (swi_instr &&
	    (regs->ARM_r0 == -ERESTARTNOHAND ||
	     regs->ARM_r0 == -ERESTARTSYS ||
	     regs->ARM_r0 == -ERESTARTNOINTR)) {
		regs->ARM_r0 = regs->ARM_ORIG_r0;
		regs->ARM_pc -= 4;
	}
	if (single_stepping)
		ptrace_set_bpt (current);
	return 0;
}

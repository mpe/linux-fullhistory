/*
 *  linux/arch/sh/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *
 *  SuperH version:  Copyright (C) 1999  Niibe Yutaka
 *
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
#include <linux/stddef.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

asmlinkage int sys_wait4(pid_t pid, unsigned long *stat_addr,
			 int options, unsigned long *ru);
asmlinkage int do_signal(struct pt_regs *regs, sigset_t *oldset);

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int
sys_sigsuspend(old_sigset_t mask,
	       unsigned long r5, unsigned long r6, unsigned long r7,
	       struct pt_regs regs)
{
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	regs.u_regs[0] = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&regs,&saveset))
			return -EINTR;
	}
}

asmlinkage int
sys_rt_sigsuspend(sigset_t *unewset, size_t sigsetsize,
		  unsigned long r6, unsigned long r7,
		  struct pt_regs regs)
{
	sigset_t saveset, newset;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
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

	regs.u_regs[0] = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&regs, &saveset))
			return -EINTR;
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

asmlinkage int
sys_sigaltstack(const stack_t *uss, stack_t *uoss,
		unsigned long r6, unsigned long r7,
		struct pt_regs regs)
{
	return do_sigaltstack(uss, uoss, regs.u_regs[UREG_SP]);
}


/*
 * Do a signal return; undo the signal stack.
 */

struct sigframe
{
	struct sigcontext sc;
	/* FPU should come here: SH-3 has no FPU */
	unsigned long extramask[_NSIG_WORDS-1];
	char retcode[4];
};

struct rt_sigframe
{
	struct siginfo *pinfo;
	void *puc;
	struct siginfo info;
	struct ucontext uc;
	/* FPU should come here: SH-3 has no FPU */
	char retcode[4];
};


static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext *sc, int *r0_p)
{
	unsigned int err = 0;

#define COPY(x)		err |= __get_user(regs->x, &sc->x)
				COPY(u_regs[1]);
	COPY(u_regs[2]);	COPY(u_regs[3]);
	COPY(u_regs[4]);	COPY(u_regs[5]);
	COPY(u_regs[6]);	COPY(u_regs[7]);
	COPY(u_regs[8]);	COPY(u_regs[9]);
	COPY(u_regs[10]);	COPY(u_regs[11]);
	COPY(u_regs[12]);	COPY(u_regs[13]);
	COPY(u_regs[14]);	COPY(u_regs[15]);
	COPY(gbr);		COPY(mach);
	COPY(macl);		COPY(pr);
	COPY(sr);		COPY(pc);
#undef COPY

	regs->syscall_nr = -1;		/* disable syscall checks */
	err |= __get_user(*r0_p, &sc->u_regs[0]);

	return err;
}

asmlinkage int sys_sigreturn(unsigned long r4, unsigned long r5,
			     unsigned long r6, unsigned long r7,
			     struct pt_regs regs)
{
	struct sigframe *frame = (struct sigframe *)regs.u_regs[UREG_SP];
	sigset_t set;
	int r0;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
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

	if (restore_sigcontext(&regs, &frame->sc, &r0))
		goto badframe;
	return r0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage int sys_rt_sigreturn(unsigned long r4, unsigned long r5,
				unsigned long r6, unsigned long r7,
				struct pt_regs regs)
{
	struct rt_sigframe *frame = (struct rt_sigframe *)regs.u_regs[UREG_SP];
	sigset_t set;
	stack_t st;
	int r0;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	
	if (restore_sigcontext(&regs, &frame->uc.uc_mcontext, &r0))
		goto badframe;

	if (__copy_from_user(&st, &frame->uc.uc_stack, sizeof(st)))
		goto badframe;
	/* It is more difficult to avoid calling this function than to
	   call it and ignore errors.  */
	do_sigaltstack(&st, NULL, regs.u_regs[UREG_SP]);

	return r0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}	

/*
 * Set up a signal frame.
 */

static int
setup_sigcontext(struct sigcontext *sc, struct pt_regs *regs,
		 unsigned long mask)
{
	int err = 0;

#define COPY(x)		err |= __put_user(regs->x, &sc->x)
	COPY(u_regs[0]);	COPY(u_regs[1]);
	COPY(u_regs[2]);	COPY(u_regs[3]);
	COPY(u_regs[4]);	COPY(u_regs[5]);
	COPY(u_regs[6]);	COPY(u_regs[7]);
	COPY(u_regs[8]);	COPY(u_regs[9]);
	COPY(u_regs[10]);	COPY(u_regs[11]);
	COPY(u_regs[12]);	COPY(u_regs[13]);
	COPY(u_regs[14]);	COPY(u_regs[15]);
	COPY(gbr);		COPY(mach);
	COPY(macl);		COPY(pr);
	COPY(sr);		COPY(pc);
#undef COPY

	/* non-iBCS2 extensions.. */
	err |= __put_user(mask, &sc->oldmask);

	return err;
}

/*
 * Determine which stack to use..
 */
static inline void *
get_sigframe(struct k_sigaction *ka, unsigned long sp, size_t frame_size)
{
	if ((ka->sa.sa_flags & SA_ONSTACK) != 0 && ! on_sig_stack(sp))
		sp = current->sas_ss_sp + current->sas_ss_size;

	return (void *)((sp - frame_size) & -8ul);
}

static void setup_frame(int sig, struct k_sigaction *ka,
			sigset_t *set, struct pt_regs *regs)
{
	struct sigframe *frame;
	int err = 0;
	int signal;

	frame = get_sigframe(ka, regs->u_regs[UREG_SP], sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	signal = current->exec_domain
		&& current->exec_domain->signal_invmap
		&& sig < 32
		? current->exec_domain->signal_invmap[sig]
		: sig;

	err |= setup_sigcontext(&frame->sc, regs, set->sig[0]);

	if (_NSIG_WORDS > 1) {
		err |= __copy_to_user(frame->extramask, &set->sig[1],
				      sizeof(frame->extramask));
	}

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		regs->pr = (unsigned long) ka->sa.sa_restorer;
	} else {
		/* This is ; mov  #__NR_sigreturn,r0 ; trapa #0 */
#ifdef CONFIG_LITTLE_ENDIAN
		unsigned long code = 0x00c300e0 | (__NR_sigreturn << 8);
#else
		unsigned long code = 0xe000c300 | (__NR_sigreturn << 16);
#endif

		regs->pr = (unsigned long) frame->retcode;
		err |= __put_user(code, (long *)(frame->retcode+0));
	}

	if (err)
		goto give_sigsegv;

	/* Set up registers for signal handler */
	regs->u_regs[UREG_SP] = (unsigned long) frame;
	regs->u_regs[4] = signal; /* Arg for signal handler */
	regs->pc = (unsigned long) ka->sa.sa_handler;

	set_fs(USER_DS);

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%08lx pr=%08lx\n",
		current->comm, current->pid, frame, regs->pc, regs->pr);
#endif

	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

static void setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe *frame;
	int err = 0;
	int signal;

	frame = get_sigframe(ka, regs->u_regs[UREG_SP], sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	signal = current->exec_domain
		&& current->exec_domain->signal_invmap
		&& sig < 32
		? current->exec_domain->signal_invmap[sig]
		: sig;

	err |= __put_user(&frame->info, &frame->pinfo);
	err |= __put_user(&frame->uc, &frame->puc);
	err |= __copy_to_user(&frame->info, info, sizeof(*info));

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(current->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->u_regs[UREG_SP]),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.uc_mcontext,
			        regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		regs->pr = (unsigned long) ka->sa.sa_restorer;
	} else {
		/* This is ; mov  #__NR_sigreturn,r0 ; trapa #0 */
#ifdef CONFIG_LITTLE_ENDIAN
		unsigned long code = 0x00c300e0 | (__NR_sigreturn << 8);
#else
		unsigned long code = 0xe000c300 | (__NR_sigreturn << 16);
#endif

		regs->pr = (unsigned long) frame->retcode;
		err |= __put_user(code, (long *)(frame->retcode+0));
	}

	if (err)
		goto give_sigsegv;

	/* Set up registers for signal handler */
	regs->u_regs[UREG_SP] = (unsigned long) frame;
	regs->u_regs[4] = signal; /* Arg for signal handler */
	regs->pc = (unsigned long) ka->sa.sa_handler;

	set_fs(USER_DS);

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%08lx pr=%08lx\n",
		current->comm, current->pid, frame, regs->pc, regs->pr);
#endif

	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

/*
 * OK, we're invoking a handler
 */	

static void
handle_signal(unsigned long sig, struct k_sigaction *ka,
	      siginfo_t *info, sigset_t *oldset, struct pt_regs * regs)
{
	/* Are we from a system call? */
	if (regs->syscall_nr >= 0) {
		/* If so, check system call restarting.. */
		switch (regs->u_regs[0]) {
			case -ERESTARTNOHAND:
				regs->u_regs[0] = -EINTR;
				break;

			case -ERESTARTSYS:
				if (!(ka->sa.sa_flags & SA_RESTART)) {
					regs->u_regs[0] = -EINTR;
					break;
				}
			/* fallthrough */
			case -ERESTARTNOINTR:
				regs->u_regs[0] = regs->syscall_nr;
				regs->pc -= 2;
		}
	}

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
int do_signal(struct pt_regs *regs, sigset_t *oldset)
{
	siginfo_t info;
	struct k_sigaction *ka;

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
				if (do_coredump(signr, regs))
					exit_code |= 0x80;
				/* FALLTHRU */

			default:
				lock_kernel();
				sigaddset(&current->signal, signr);
				recalc_sigpending(current);
				current->flags |= PF_SIGNALED;
				do_exit(exit_code);
				/* NOTREACHED */
			}
		}

		/* Whee!  Actually deliver the signal.  */
		handle_signal(signr, ka, &info, oldset, regs);
		return 1;
	}

	/* Did we come from a system call? */
	if (regs->syscall_nr >= 0) {
		/* Restart the system call - no handlers present */
		if (regs->u_regs[0] == -ERESTARTNOHAND ||
		    regs->u_regs[0] == -ERESTARTSYS ||
		    regs->u_regs[0] == -ERESTARTNOINTR) {
			regs->u_regs[0] = regs->syscall_nr;
			regs->pc -= 2;
		}
	}
	return 0;
}

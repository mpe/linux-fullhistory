/* signal.c: FRV specific bits of signal handling
 *
 * Copyright (C) 2003-5 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from arch/m68k/kernel/signal.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

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
#include <linux/personality.h>
#include <linux/suspend.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

struct fdpic_func_descriptor {
	unsigned long	text;
	unsigned long	GOT;
};

asmlinkage int do_signal(struct pt_regs *regs, sigset_t *oldset);

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int sys_sigsuspend(int history0, int history1, old_sigset_t mask)
{
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	__frame->gr8 = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(__frame, &saveset))
			/* return the signal number as the return value of this function
			 * - this is an utterly evil hack. syscalls should not invoke do_signal()
			 *   as entry.S sets regs->gr8 to the return value of the system call
			 * - we can't just use sigpending() as we'd have to discard SIG_IGN signals
			 *   and call waitpid() if SIGCHLD needed discarding
			 * - this only works on the i386 because it passes arguments to the signal
			 *   handler on the stack, and the return value in EAX is effectively
			 *   discarded
			 */
			return __frame->gr8;
	}
}

asmlinkage int sys_rt_sigsuspend(sigset_t __user *unewset, size_t sigsetsize)
{
	sigset_t saveset, newset;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	__frame->gr8 = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(__frame, &saveset))
			/* return the signal number as the return value of this function
			 * - this is an utterly evil hack. syscalls should not invoke do_signal()
			 *   as entry.S sets regs->gr8 to the return value of the system call
			 * - we can't just use sigpending() as we'd have to discard SIG_IGN signals
			 *   and call waitpid() if SIGCHLD needed discarding
			 * - this only works on the i386 because it passes arguments to the signal
			 *   handler on the stack, and the return value in EAX is effectively
			 *   discarded
			 */
			return __frame->gr8;
	}
}

asmlinkage int sys_sigaction(int sig,
			     const struct old_sigaction __user *act,
			     struct old_sigaction __user *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (!access_ok(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

asmlinkage
int sys_sigaltstack(const stack_t __user *uss, stack_t __user *uoss)
{
	return do_sigaltstack(uss, uoss, __frame->sp);
}


/*
 * Do a signal return; undo the signal stack.
 */

struct sigframe
{
	void (*pretcode)(void);
	int sig;
	struct sigcontext sc;
	unsigned long extramask[_NSIG_WORDS-1];
	uint32_t retcode[2];
};

struct rt_sigframe
{
	void (*pretcode)(void);
	int sig;
	struct siginfo *pinfo;
	void *puc;
	struct siginfo info;
	struct ucontext uc;
	uint32_t retcode[2];
};

static int restore_sigcontext(struct sigcontext __user *sc, int *_gr8)
{
	struct user_context *user = current->thread.user;
	unsigned long tbr, psr;

	tbr = user->i.tbr;
	psr = user->i.psr;
	if (copy_from_user(user, &sc->sc_context, sizeof(sc->sc_context)))
		goto badframe;
	user->i.tbr = tbr;
	user->i.psr = psr;

	restore_user_regs(user);

	user->i.syscallno = -1;		/* disable syscall checks */

	*_gr8 = user->i.gr[8];
	return 0;

 badframe:
	return 1;
}

asmlinkage int sys_sigreturn(void)
{
	struct sigframe __user *frame = (struct sigframe __user *) __frame->sp;
	sigset_t set;
	int gr8;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.sc_oldmask))
		goto badframe;

	if (_NSIG_WORDS > 1 &&
	    __copy_from_user(&set.sig[1], &frame->extramask, sizeof(frame->extramask)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(&frame->sc, &gr8))
		goto badframe;
	return gr8;

 badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage int sys_rt_sigreturn(void)
{
	struct rt_sigframe __user *frame = (struct rt_sigframe __user *) __frame->sp;
	sigset_t set;
	int gr8;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(&frame->uc.uc_mcontext, &gr8))
		goto badframe;

	if (do_sigaltstack(&frame->uc.uc_stack, NULL, __frame->sp) == -EFAULT)
		goto badframe;

	return gr8;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

/*
 * Set up a signal frame
 */
static int setup_sigcontext(struct sigcontext __user *sc, unsigned long mask)
{
	save_user_regs(current->thread.user);

	if (copy_to_user(&sc->sc_context, current->thread.user, sizeof(sc->sc_context)) != 0)
		goto badframe;

	/* non-iBCS2 extensions.. */
	if (__put_user(mask, &sc->sc_oldmask) < 0)
		goto badframe;

	return 0;

 badframe:
	return 1;
}

/*****************************************************************************/
/*
 * Determine which stack to use..
 */
static inline void __user *get_sigframe(struct k_sigaction *ka,
					struct pt_regs *regs,
					size_t frame_size)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = regs->sp;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (! on_sig_stack(sp))
			sp = current->sas_ss_sp + current->sas_ss_size;
	}

	return (void __user *) ((sp - frame_size) & ~7UL);
} /* end get_sigframe() */

/*****************************************************************************/
/*
 *
 */
static void setup_frame(int sig, struct k_sigaction *ka, sigset_t *set, struct pt_regs * regs)
{
	struct sigframe __user *frame;
	int rsig;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	rsig = sig;
	if (sig < 32 &&
	    __current_thread_info->exec_domain &&
	    __current_thread_info->exec_domain->signal_invmap)
		rsig = __current_thread_info->exec_domain->signal_invmap[sig];

	if (__put_user(rsig, &frame->sig) < 0)
		goto give_sigsegv;

	if (setup_sigcontext(&frame->sc, set->sig[0]))
		goto give_sigsegv;

	if (_NSIG_WORDS > 1) {
		if (__copy_to_user(frame->extramask, &set->sig[1],
				   sizeof(frame->extramask)))
			goto give_sigsegv;
	}

	/* Set up to return from userspace.  If provided, use a stub
	 * already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		if (__put_user(ka->sa.sa_restorer, &frame->pretcode) < 0)
			goto give_sigsegv;
	}
	else {
		/* Set up the following code on the stack:
		 *	setlos	#__NR_sigreturn,gr7
		 *	tira	gr0,0
		 */
		if (__put_user((void (*)(void))frame->retcode, &frame->pretcode) ||
		    __put_user(0x8efc0000|__NR_sigreturn, &frame->retcode[0]) ||
		    __put_user(0xc0700000, &frame->retcode[1]))
			goto give_sigsegv;

		flush_icache_range((unsigned long) frame->retcode,
				   (unsigned long) (frame->retcode + 2));
	}

	/* set up registers for signal handler */
	regs->sp   = (unsigned long) frame;
	regs->lr   = (unsigned long) &frame->retcode;
	regs->gr8  = sig;

	if (get_personality & FDPIC_FUNCPTRS) {
		struct fdpic_func_descriptor __user *funcptr =
			(struct fdpic_func_descriptor *) ka->sa.sa_handler;
		__get_user(regs->pc, &funcptr->text);
		__get_user(regs->gr15, &funcptr->GOT);
	} else {
		regs->pc   = (unsigned long) ka->sa.sa_handler;
		regs->gr15 = 0;
	}

	set_fs(USER_DS);

#if DEBUG_SIG
	printk("SIG deliver %d (%s:%d): sp=%p pc=%lx ra=%p\n",
		sig, current->comm, current->pid, frame, regs->pc, frame->pretcode);
#endif

	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;

	force_sig(SIGSEGV, current);
} /* end setup_frame() */

/*****************************************************************************/
/*
 *
 */
static void setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs * regs)
{
	struct rt_sigframe __user *frame;
	int rsig;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	rsig = sig;
	if (sig < 32 &&
	    __current_thread_info->exec_domain &&
	    __current_thread_info->exec_domain->signal_invmap)
		rsig = __current_thread_info->exec_domain->signal_invmap[sig];

	if (__put_user(rsig,		&frame->sig) ||
	    __put_user(&frame->info,	&frame->pinfo) ||
	    __put_user(&frame->uc,	&frame->puc))
		goto give_sigsegv;

	if (copy_siginfo_to_user(&frame->info, info))
		goto give_sigsegv;

	/* Create the ucontext.  */
	if (__put_user(0, &frame->uc.uc_flags) ||
	    __put_user(0, &frame->uc.uc_link) ||
	    __put_user((void*)current->sas_ss_sp, &frame->uc.uc_stack.ss_sp) ||
	    __put_user(sas_ss_flags(regs->sp), &frame->uc.uc_stack.ss_flags) ||
	    __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size))
		goto give_sigsegv;

	if (setup_sigcontext(&frame->uc.uc_mcontext, set->sig[0]))
		goto give_sigsegv;

	if (__copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set)))
		goto give_sigsegv;

	/* Set up to return from userspace.  If provided, use a stub
	 * already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		if (__put_user(ka->sa.sa_restorer, &frame->pretcode))
			goto give_sigsegv;
	}
	else {
		/* Set up the following code on the stack:
		 *	setlos	#__NR_sigreturn,gr7
		 *	tira	gr0,0
		 */
		if (__put_user((void (*)(void))frame->retcode, &frame->pretcode) ||
		    __put_user(0x8efc0000|__NR_rt_sigreturn, &frame->retcode[0]) ||
		    __put_user(0xc0700000, &frame->retcode[1]))
			goto give_sigsegv;

		flush_icache_range((unsigned long) frame->retcode,
				   (unsigned long) (frame->retcode + 2));
	}

	/* Set up registers for signal handler */
	regs->sp  = (unsigned long) frame;
	regs->lr   = (unsigned long) &frame->retcode;
	regs->gr8 = sig;
	regs->gr9 = (unsigned long) &frame->info;

	if (get_personality & FDPIC_FUNCPTRS) {
		struct fdpic_func_descriptor *funcptr =
			(struct fdpic_func_descriptor __user *) ka->sa.sa_handler;
		__get_user(regs->pc, &funcptr->text);
		__get_user(regs->gr15, &funcptr->GOT);
	} else {
		regs->pc   = (unsigned long) ka->sa.sa_handler;
		regs->gr15 = 0;
	}

	set_fs(USER_DS);

#if DEBUG_SIG
	printk("SIG deliver %d (%s:%d): sp=%p pc=%lx ra=%p\n",
		sig, current->comm, current->pid, frame, regs->pc, frame->pretcode);
#endif

	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);

} /* end setup_rt_frame() */

/*****************************************************************************/
/*
 * OK, we're invoking a handler
 */
static void handle_signal(unsigned long sig, siginfo_t *info,
			  struct k_sigaction *ka, sigset_t *oldset,
			  struct pt_regs *regs)
{
	/* Are we from a system call? */
	if (in_syscall(regs)) {
		/* If so, check system call restarting.. */
		switch (regs->gr8) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->gr8 = -EINTR;
			break;

		case -ERESTARTSYS:
			if (!(ka->sa.sa_flags & SA_RESTART)) {
				regs->gr8 = -EINTR;
				break;
			}
			/* fallthrough */
		case -ERESTARTNOINTR:
			regs->gr8 = regs->orig_gr8;
			regs->pc -= 4;
		}
	}

	/* Set up the stack frame */
	if (ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame(sig, ka, info, oldset, regs);
	else
		setup_frame(sig, ka, oldset, regs);

	if (!(ka->sa.sa_flags & SA_NODEFER)) {
		spin_lock_irq(&current->sighand->siglock);
		sigorsets(&current->blocked, &current->blocked, &ka->sa.sa_mask);
		sigaddset(&current->blocked, sig);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}
} /* end handle_signal() */

/*****************************************************************************/
/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
int do_signal(struct pt_regs *regs, sigset_t *oldset)
{
	struct k_sigaction ka;
	siginfo_t info;
	int signr;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return 1;

	if (current->flags & PF_FREEZE) {
		refrigerator(0);
		goto no_signal;
	}

	if (!oldset)
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		handle_signal(signr, &info, &ka, oldset, regs);
		return 1;
	}

 no_signal:
	/* Did we come from a system call? */
	if (regs->syscallno >= 0) {
		/* Restart the system call - no handlers present */
		if (regs->gr8 == -ERESTARTNOHAND ||
		    regs->gr8 == -ERESTARTSYS ||
		    regs->gr8 == -ERESTARTNOINTR) {
			regs->gr8 = regs->orig_gr8;
			regs->pc -= 4;
		}

		if (regs->gr8 == -ERESTART_RESTARTBLOCK){
			regs->gr8 = __NR_restart_syscall;
			regs->pc -= 4;
		}
	}

	return 0;
} /* end do_signal() */

/*****************************************************************************/
/*
 * notification of userspace execution resumption
 * - triggered by current->work.notify_resume
 */
asmlinkage void do_notify_resume(__u32 thread_info_flags)
{
	/* pending single-step? */
	if (thread_info_flags & _TIF_SINGLESTEP)
		clear_thread_flag(TIF_SINGLESTEP);

	/* deal with pending signal delivery */
	if (thread_info_flags & _TIF_SIGPENDING)
		do_signal(__frame, NULL);

} /* end do_notify_resume() */

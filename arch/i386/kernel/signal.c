/*
 *  linux/arch/i386/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
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

#include <asm/uaccess.h>

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

asmlinkage int sys_wait4(pid_t pid, unsigned long *stat_addr,
			 int options, unsigned long *ru);
asmlinkage int do_signal(sigset_t *oldset, struct pt_regs *regs);

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int
sys_sigsuspend(int history0, int history1, old_sigset_t mask)
{
	struct pt_regs * regs = (struct pt_regs *) &history0;
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	regs->eax = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs))
			return -EINTR;
	}
}

asmlinkage int
sys_rt_sigsuspend(sigset_t *unewset, size_t sigsetsize)
{
	struct pt_regs * regs = (struct pt_regs *) &unewset;
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

	regs->eax = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs))
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


/*
 * Do a signal return; undo the signal stack.
 */

struct sigframe
{
	char *pretcode;
	int sig;
	struct sigcontext sc;
	struct _fpstate fpstate;
	unsigned long extramask[_NSIG_WORDS-1];
	char retcode[8];
};

struct rt_sigframe
{
	char *pretcode;
	int sig;
	struct siginfo *pinfo;
	void *puc;
	struct siginfo info;
	struct ucontext uc;
	struct _fpstate fpstate;
	char retcode[8];
};


static inline void restore_i387_hard(struct _fpstate *buf)
{
#ifdef __SMP__
	if (current->flags & PF_USEDFPU) {
		stts();
	}
#else
	if (current == last_task_used_math) {
		last_task_used_math = NULL;
		stts();
	}
#endif
	current->used_math = 1;
	current->flags &= ~PF_USEDFPU;
	__copy_from_user(&current->tss.i387.hard, buf, sizeof(*buf));
}

static inline void restore_i387(struct _fpstate *buf)
{
#ifndef CONFIG_MATH_EMULATION
	restore_i387_hard(buf);
#else
	if (hard_math)
		restore_i387_hard(buf);
	else
		restore_i387_soft(buf);
#endif
}

static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext *sc)
{
	unsigned int tmp;

#define COPY(x)		__get_user(regs->x, &sc->x)

#define COPY_SEG(seg)							\
	{ __get_user(tmp, &sc->seg);					\
	  if ((tmp & 0xfffc)		/* not a NULL selectors */	\
	      && (tmp & 0x4) != 0x4	/* not a LDT selector */	\
	      && (tmp & 3) != 3)	/* not a RPL3 GDT selector */	\
		  goto badframe;					\
	  regs->x##seg = tmp; }

#define COPY_SEG_STRICT(seg)						\
	{ __get_user(tmp, &sc->seg);					\
	  if ((tmp & 0xfffc) && (tmp & 3) != 3) goto badframe;		\
	  regs->x##seg = tmp; }

#define GET_SEG(seg)							\
	{ __get_user(tmp, &sc->seg);					\
	  if ((tmp & 0xfffc)		/* not a NULL selectors */	\
	      && (tmp & 0x4) != 0x4	/* not a LDT selector */	\
	      && (tmp & 3) != 3)	/* not a RPL3 GDT selector */	\
		  goto badframe;					\
	  __asm__ __volatile__("mov %w0,%%" #seg : : "r"(tmp)); }

	GET_SEG(gs);
	GET_SEG(fs);
	COPY_SEG(es);
	COPY_SEG(ds);
	COPY(edi);
	COPY(esi);
	COPY(ebp);
	COPY(esp);
	COPY(ebx);
	COPY(edx);
	COPY(ecx);
	COPY(eip);
	COPY_SEG_STRICT(cs);
	COPY_SEG_STRICT(ss);
	
	__get_user(tmp, &sc->eflags);
	regs->eflags = (regs->eflags & ~0x40DD5) | (tmp & 0x40DD5);
	regs->orig_eax = -1;		/* disable syscall checks */

	__get_user(tmp, (unsigned long *)&sc->fpstate);
	if (tmp) {
		struct _fpstate * buf = (struct _fpstate *) tmp;
		if (verify_area(VERIFY_READ, buf, sizeof(*buf)))
			goto badframe;
		restore_i387(buf);
	}

	__get_user(tmp, &sc->eax);
	return tmp;

badframe:
	lock_kernel();
	do_exit(SIGSEGV);
}

asmlinkage int sys_sigreturn(unsigned long __unused)
{
	struct pt_regs *regs = (struct pt_regs *) &__unused;
	struct sigframe *frame = (struct sigframe *)(regs->esp - 8);
	sigset_t set;

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
	
	return restore_sigcontext(regs, &frame->sc);

badframe:
	lock_kernel();
	do_exit(SIGSEGV);
}	

asmlinkage int sys_rt_sigreturn(unsigned long __unused)
{
	struct pt_regs *regs = (struct pt_regs *) &__unused;
	struct rt_sigframe *frame = (struct rt_sigframe *)(regs->esp - 4);
	sigset_t set;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
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

/*
 * Set up a signal frame.
 */

static inline struct _fpstate * save_i387_hard(struct _fpstate * buf)
{
#ifdef __SMP__
	if (current->flags & PF_USEDFPU) {
		__asm__ __volatile__("fnsave %0":"=m"(current->tss.i387.hard));
		stts();
		current->flags &= ~PF_USEDFPU;
	}
#else
	if (current == last_task_used_math) {
		__asm__ __volatile__("fnsave %0":"=m"(current->tss.i387.hard));
		last_task_used_math = NULL;
		__asm__ __volatile__("fwait");	/* not needed on 486+ */
		stts();
	}
#endif
	current->tss.i387.hard.status = current->tss.i387.hard.swd;
	copy_to_user(buf, &current->tss.i387.hard, sizeof(*buf));
	current->used_math = 0;
	return buf;
}

static struct _fpstate * save_i387(struct _fpstate *buf)
{
	if (!current->used_math)
		return NULL;

#ifndef CONFIG_MATH_EMULATION
	return save_i387_hard(buf);
#else
	return hard_math ? save_i387_hard(buf) : save_i387_soft(buf);
#endif
}

static void
setup_sigcontext(struct sigcontext *sc, struct _fpstate *fpstate,
		 struct pt_regs *regs, unsigned long mask)
{
	unsigned int tmp;

	tmp = 0;
	__asm__("mov %%gs,%w0" : "=r"(tmp): "0"(tmp));
	__put_user(tmp, (unsigned int *)&sc->gs);
	__asm__("mov %%fs,%w0" : "=r"(tmp): "0"(tmp));
	__put_user(tmp, (unsigned int *)&sc->fs);

	__put_user(regs->xes, (unsigned int *)&sc->es);
	__put_user(regs->xds, (unsigned int *)&sc->ds);
	__put_user(regs->edi, &sc->edi);
	__put_user(regs->esi, &sc->esi);
	__put_user(regs->ebp, &sc->ebp);
	__put_user(regs->esp, &sc->esp);
	__put_user(regs->ebx, &sc->ebx);
	__put_user(regs->edx, &sc->edx);
	__put_user(regs->ecx, &sc->ecx);
	__put_user(regs->eax, &sc->eax);
	__put_user(current->tss.trap_no, &sc->trapno);
	__put_user(current->tss.error_code, &sc->err);
	__put_user(regs->eip, &sc->eip);
	__put_user(regs->xcs, (unsigned int *)&sc->cs);
	__put_user(regs->eflags, &sc->eflags);
	__put_user(regs->esp, &sc->esp_at_signal);
	__put_user(regs->xss, (unsigned int *)&sc->ss);

	__put_user(save_i387(fpstate), &sc->fpstate);

	/* non-iBCS2 extensions.. */
	__put_user(mask, &sc->oldmask);
	__put_user(current->tss.cr2, &sc->cr2);
}	

static void setup_frame(int sig, struct k_sigaction *ka,
			sigset_t *set, struct pt_regs * regs)
{
	struct sigframe *frame;

	frame = (struct sigframe *)((regs->esp - sizeof(*frame)) & -8);

	/* XXX: Check here if we need to switch stacks.. */

	/* This is legacy signal stack switching.  */
	if ((regs->xss & 0xffff) != USER_DS
	    && !(ka->sa.sa_flags & SA_RESTORER) && ka->sa.sa_restorer)
		frame = (struct sigframe *) ka->sa.sa_restorer;

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto segv_and_exit;

	__put_user((current->exec_domain
		    && current->exec_domain->signal_invmap
		    && sig < 32
		    ? current->exec_domain->signal_invmap[sig]
		    : sig),
		   &frame->sig);

	setup_sigcontext(&frame->sc, &frame->fpstate, regs, set->sig[0]);

	if (_NSIG_WORDS > 1) {
		__copy_to_user(frame->extramask, &set->sig[1],
			       sizeof(frame->extramask));
	}

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		__put_user(ka->sa.sa_restorer, &frame->pretcode);
	} else {
		__put_user(frame->retcode, &frame->pretcode);
		/* This is popl %eax ; movl $,%eax ; int $0x80 */
		__put_user(0xb858, (short *)(frame->retcode+0));
		__put_user(__NR_sigreturn, (int *)(frame->retcode+2));
		__put_user(0x80cd, (short *)(frame->retcode+6));
	}

	/* Set up registers for signal handler */
	regs->esp = (unsigned long) frame;
	regs->eip = (unsigned long) ka->sa.sa_handler;
	{
		unsigned long seg = USER_DS;
		__asm__("mov %w0,%%fs ; mov %w0,%%gs": "=r"(seg) : "0"(seg));
		set_fs(seg);
		regs->xds = seg;
		regs->xes = seg;
		regs->xss = seg;
		regs->xcs = USER_CS;
	}
	regs->eflags &= ~TF_MASK;

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%p ra=%p\n",
		current->comm, current->pid, frame, regs->eip, frame->pretcode);
#endif

	return;

segv_and_exit:
	lock_kernel();
	do_exit(SIGSEGV);
}

static void setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs * regs)
{
	struct rt_sigframe *frame;

	frame = (struct rt_sigframe *)((regs->esp - sizeof(*frame)) & -8);

	/* XXX: Check here if we need to switch stacks.. */

	/* This is legacy signal stack switching.  */
	if ((regs->xss & 0xffff) != USER_DS
	    && !(ka->sa.sa_flags & SA_RESTORER) && ka->sa.sa_restorer)
		frame = (struct rt_sigframe *) ka->sa.sa_restorer;

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto segv_and_exit;

	__put_user((current->exec_domain
		    && current->exec_domain->signal_invmap
		    && sig < 32
		    ? current->exec_domain->signal_invmap[sig]
		    : sig),
		   &frame->sig);
	__put_user(&frame->info, &frame->pinfo);
	__put_user(&frame->uc, &frame->puc);
	__copy_to_user(&frame->info, info, sizeof(*info));

	/* Clear all the bits of the ucontext we don't use.  */
	__clear_user(&frame->uc, offsetof(struct ucontext, uc_mcontext));

	setup_sigcontext(&frame->uc.uc_mcontext, &frame->fpstate,
			 regs, set->sig[0]);
	__copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		__put_user(ka->sa.sa_restorer, &frame->pretcode);
	} else {
		__put_user(frame->retcode, &frame->pretcode);
		/* This is movl $,%eax ; int $0x80 */
		__put_user(0xb8, (char *)(frame->retcode+0));
		__put_user(__NR_rt_sigreturn, (int *)(frame->retcode+1));
		__put_user(0x80cd, (short *)(frame->retcode+5));
	}

	/* Set up registers for signal handler */
	regs->esp = (unsigned long) frame;
	regs->eip = (unsigned long) ka->sa.sa_handler;
	{
		unsigned long seg = USER_DS;
		__asm__("mov %w0,%%fs ; mov %w0,%%gs": "=r"(seg) : "0"(seg));
		set_fs(seg);
		regs->xds = seg;
		regs->xes = seg;
		regs->xss = seg;
		regs->xcs = USER_CS;
	}
	regs->eflags &= ~TF_MASK;

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%p ra=%p\n",
		current->comm, current->pid, frame, regs->eip, frame->pretcode);
#endif

	return;

segv_and_exit:
	lock_kernel();
	do_exit(SIGSEGV);
}

/*
 * OK, we're invoking a handler
 */	

static void
handle_signal(unsigned long sig, struct k_sigaction *ka,
	      siginfo_t *info, sigset_t *oldset, struct pt_regs * regs)
{
	/* Are we from a system call? */
	if (regs->orig_eax >= 0) {
		/* If so, check system call restarting.. */
		switch (regs->eax) {
			case -ERESTARTNOHAND:
				regs->eax = -EINTR;
				break;

			case -ERESTARTSYS:
				if (!(ka->sa.sa_flags & SA_RESTART)) {
					regs->eax = -EINTR;
					break;
				}
			/* fallthrough */
			case -ERESTARTNOINTR:
				regs->eax = regs->orig_eax;
				regs->eip -= 2;
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
asmlinkage int do_signal(sigset_t *oldset, struct pt_regs *regs)
{
	sigset_t _oldset;
	siginfo_t info;
	unsigned long signr, core = 0;
	struct k_sigaction *ka;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if ((regs->xcs & 3) != 3)
		return 1;

	spin_lock_irq(&current->sigmask_lock);
	if (!oldset) {
		_oldset = current->blocked;
		oldset = &_oldset;
	}
 	while ((signr = dequeue_signal(&current->blocked, &info)) != 0) {
		spin_unlock_irq(&current->sigmask_lock);

		if ((current->flags & PF_PTRACED) && signr != SIGKILL) {
			/* Let the debugger run.  */
			current->exit_code = signr;
			current->state = TASK_STOPPED;
			notify_parent(current, SIGCHLD);
			schedule();

			/* We're back.  Did the debugger cancel the sig?  */
			if (!(signr = current->exit_code))
				goto skip_signal;
			current->exit_code = 0;

			/* The debugger continued.  Ignore SIGSTOP.  */
			if (signr == SIGSTOP)
				goto skip_signal;

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
				goto skip_signal;
			}
		}

		ka = &current->sig->action[signr-1];
		if (ka->sa.sa_handler == SIG_DFL) {
			/* Init gets no signals it doesn't want.  */
			if (current->pid == 1)
				goto skip_signal;

			switch (signr) {
			case SIGCONT: case SIGCHLD: case SIGWINCH:
				goto skip_signal;

			case SIGTSTP: case SIGTTIN: case SIGTTOU:
				if (is_orphaned_pgrp(current->pgrp))
					goto skip_signal;
				/* FALLTHRU */

			case SIGSTOP:
				current->state = TASK_STOPPED;
				current->exit_code = signr;
				if (!(current->p_pptr->sig->action[SIGCHLD-1]
				      .sa.sa_flags & SA_NOCLDSTOP))
					notify_parent(current, SIGCHLD);
				schedule();
				break;

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGABRT: case SIGFPE: case SIGSEGV:
				lock_kernel();
				if (current->binfmt
				    && current->binfmt->core_dump
				    &&current->binfmt->core_dump(signr, regs))
					core = 0x80;
				unlock_kernel();
				/* FALLTHRU */

			default:
				lock_kernel();
				sigaddset(&current->signal, signr);
				current->flags |= PF_SIGNALED;
				do_exit((signr & 0x7f) | core);
			}
		} else if (ka->sa.sa_handler == SIG_IGN) {
			if (signr == SIGCHLD) {
				/* Check for SIGCHLD: it's special.  */
				while (sys_wait4(-1, NULL, WNOHANG, NULL) > 0)
					/* nothing */;
			}
		} else {
			/* Whee!  Actually deliver the signal.  */
			handle_signal(signr, ka, &info, oldset, regs);
			return 1;
		}
	skip_signal:
		spin_lock_irq(&current->sigmask_lock);
	}

	/* Did we come from a system call? */
	if (regs->orig_eax >= 0) {
		/* Restart the system call - no handlers present */
		if (regs->eax == -ERESTARTNOHAND ||
		    regs->eax == -ERESTARTSYS ||
		    regs->eax == -ERESTARTNOINTR) {
			regs->eax = regs->orig_eax;
			regs->eip -= 2;
		}
	}
	return 0;
}

/*
 *  linux/arch/alpha/kernel/signal.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  1997-11-02  Modified for POSIX.1b signals by Richard Henderson
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/signal.h>
#include <linux/stddef.h>

#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <asm/sigcontext.h>
#include <asm/ucontext.h>

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

asmlinkage int sys_wait4(int, int *, int, struct rusage *);
asmlinkage void ret_from_sys_call(void);
asmlinkage int do_signal(sigset_t *, struct pt_regs *,
			 struct switch_stack *, unsigned long, unsigned long);

extern int ptrace_set_bpt (struct task_struct *child);
extern int ptrace_cancel_bpt (struct task_struct *child);


/*
 * The OSF/1 sigprocmask calling sequence is different from the
 * C sigprocmask() sequence..
 *
 * how:
 * 1 - SIG_BLOCK
 * 2 - SIG_UNBLOCK
 * 3 - SIG_SETMASK
 *
 * We change the range to -1 .. 1 in order to let gcc easily
 * use the conditional move instructions.
 *
 * Note that we don't need to acquire the kernel lock for SMP
 * operation, as all of this is local to this thread.
 */
asmlinkage unsigned long
osf_sigprocmask(int how, unsigned long newmask, long a2, long a3,
		long a4, long a5, struct pt_regs regs)
{
	unsigned long oldmask = -EINVAL;

	if ((unsigned long)how-1 <= 2) {
		long sign = how-2;		/* -1 .. 1 */
		unsigned long block, unblock;

		newmask &= _BLOCKABLE;
		spin_lock_irq(&current->sigmask_lock);
		oldmask = current->blocked.sig[0];

		unblock = oldmask & ~newmask;
		block = oldmask | newmask;
		if (!sign)
			block = unblock;
		if (sign <= 0)
			newmask = block;
		if (_NSIG_WORDS > 1 && sign > 0)
			sigemptyset(&current->blocked);
		current->blocked.sig[0] = newmask;
		spin_unlock_irq(&current->sigmask_lock);

		(&regs)->r0 = 0;		/* special no error return */
	}
	return oldmask;
}

asmlinkage int 
osf_sigaction(int sig, const struct osf_sigaction *act,
	      struct osf_sigaction *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags))
			return -EFAULT;
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
		new_ka.ka_restorer = NULL;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (verify_area(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_flags, &oact->sa_flags))
			return -EFAULT;
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

asmlinkage int 
sys_rt_sigaction(int sig, const struct sigaction *act, struct sigaction *oact,
		 void *restorer, size_t sigsetsize)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (act) {
		new_ka.ka_restorer = restorer;
		if (copy_from_user(&new_ka.sa, act, sizeof(*act)))
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (copy_to_user(oact, &old_ka.sa, sizeof(*oact)))
			return -EFAULT;
	}

	return ret;
}

asmlinkage int
osf_sigpending(old_sigset_t *set)
{
        sigset_t pending;

        spin_lock_irq(&current->sigmask_lock);
        sigandsets(&pending, &current->blocked, &current->signal);
        spin_unlock_irq(&current->sigmask_lock);

        return copy_to_user(set, &pending, sizeof(*set));
}

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int
do_sigsuspend(old_sigset_t mask, struct pt_regs *reg, struct switch_stack *sw)
{
	sigset_t oldset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sigmask_lock);
	oldset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&oldset, reg, sw, 0, 0))
			return -EINTR;
	}
}

asmlinkage int
do_rt_sigsuspend(sigset_t *uset, size_t sigsetsize,
		 struct pt_regs *reg, struct switch_stack *sw)
{
	sigset_t oldset, set;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;
	if (copy_from_user(&set, uset, sizeof(set)))
		return -EFAULT;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	oldset = current->blocked;
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&oldset, reg, sw, 0, 0))
			return -EINTR;
	}
}

/*
 * Do a signal return; undo the signal stack.
 */

struct sigframe
{
	struct sigcontext sc;
	unsigned long extramask[_NSIG_WORDS-1];
	unsigned int retcode[3];
};

struct rt_sigframe
{
	struct siginfo info;
	struct ucontext uc;
	unsigned int retcode[3];
};

#define INSN_MOV_R30_R16	0x47fe0410
#define INSN_LDI_R0		0x201f0000
#define INSN_CALLSYS		0x00000083


static void
restore_sigcontext(struct sigcontext *sc, struct pt_regs *regs,
		   struct switch_stack *sw)
{
	unsigned long usp;
	int i;

	__get_user(regs->pc, &sc->sc_pc);
	sw->r26 = (unsigned long) ret_from_sys_call;

	__get_user(regs->r0, sc->sc_regs+0);
	__get_user(regs->r1, sc->sc_regs+1);
	__get_user(regs->r2, sc->sc_regs+2);
	__get_user(regs->r3, sc->sc_regs+3);
	__get_user(regs->r4, sc->sc_regs+4);
	__get_user(regs->r5, sc->sc_regs+5);
	__get_user(regs->r6, sc->sc_regs+6);
	__get_user(regs->r7, sc->sc_regs+7);
	__get_user(regs->r8, sc->sc_regs+8);
	__get_user(sw->r9, sc->sc_regs+9);
	__get_user(sw->r10, sc->sc_regs+10);
	__get_user(sw->r11, sc->sc_regs+11);
	__get_user(sw->r12, sc->sc_regs+12);
	__get_user(sw->r13, sc->sc_regs+13);
	__get_user(sw->r14, sc->sc_regs+14);
	__get_user(sw->r15, sc->sc_regs+15);
	__get_user(regs->r16, sc->sc_regs+16);
	__get_user(regs->r17, sc->sc_regs+17);
	__get_user(regs->r18, sc->sc_regs+18);
	__get_user(regs->r19, sc->sc_regs+19);
	__get_user(regs->r20, sc->sc_regs+20);
	__get_user(regs->r21, sc->sc_regs+21);
	__get_user(regs->r22, sc->sc_regs+22);
	__get_user(regs->r23, sc->sc_regs+23);
	__get_user(regs->r24, sc->sc_regs+24);
	__get_user(regs->r25, sc->sc_regs+25);
	__get_user(regs->r26, sc->sc_regs+26);
	__get_user(regs->r27, sc->sc_regs+27);
	__get_user(regs->r28, sc->sc_regs+28);
	__get_user(regs->gp, sc->sc_regs+29);
	__get_user(usp, sc->sc_regs+30);
	wrusp(usp);

	for (i = 0; i < 31; i++)
		__get_user(sw->fp[i], sc->sc_fpregs+i);
	__get_user(sw->fp[31], &sc->sc_fpcr);
}

asmlinkage void
do_sigreturn(struct sigframe *frame, struct pt_regs *regs,
	     struct switch_stack *sw)
{
	unsigned long ps;
	sigset_t set;

	/* Verify that it's a good sigcontext before using it */
	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto give_sigsegv;
	if (__get_user(ps, &frame->sc.sc_ps) || ps != 8)
		goto give_sigsegv;
	if (__get_user(set.sig[0], &frame->sc.sc_mask)
	    || (_NSIG_WORDS > 1
		&& __copy_from_user(&set.sig[1], &frame->extramask,
				    sizeof(frame->extramask))))
		goto give_sigsegv;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	restore_sigcontext(&frame->sc, regs, sw);

	/* Send SIGTRAP if we're single-stepping: */
	if (ptrace_cancel_bpt (current))
		send_sig(SIGTRAP, current, 1);
	return;

give_sigsegv:
	lock_kernel();
	do_exit(SIGSEGV);
}

asmlinkage void
do_rt_sigreturn(struct rt_sigframe *frame, struct pt_regs *regs,
		struct switch_stack *sw)
{
	unsigned long ps;
	sigset_t set;

	/* Verify that it's a good sigcontext before using it */
	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto give_sigsegv;
	if (__get_user(ps, &frame->uc.uc_mcontext.sc_ps) || ps != 8)
		goto give_sigsegv;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto give_sigsegv;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	restore_sigcontext(&frame->uc.uc_mcontext, regs, sw);

	/* Send SIGTRAP if we're single-stepping: */
	if (ptrace_cancel_bpt (current))
		send_sig(SIGTRAP, current, 1);
	return;

give_sigsegv:
	lock_kernel();
	do_exit(SIGSEGV);
}


/*
 * Set up a signal frame.
 */

static void 
setup_sigcontext(struct sigcontext *sc, struct pt_regs *regs, 
		 struct switch_stack *sw, unsigned long mask, unsigned long sp)
{
	long i;

	__put_user(0, &sc->sc_onstack);
	__put_user(mask, &sc->sc_mask);
	__put_user(regs->pc, &sc->sc_pc);
	__put_user(8, &sc->sc_ps);

	__put_user(regs->r0 , sc->sc_regs+0);
	__put_user(regs->r1 , sc->sc_regs+1);
	__put_user(regs->r2 , sc->sc_regs+2);
	__put_user(regs->r3 , sc->sc_regs+3);
	__put_user(regs->r4 , sc->sc_regs+4);
	__put_user(regs->r5 , sc->sc_regs+5);
	__put_user(regs->r6 , sc->sc_regs+6);
	__put_user(regs->r7 , sc->sc_regs+7);
	__put_user(regs->r8 , sc->sc_regs+8);
	__put_user(sw->r9   , sc->sc_regs+9);
	__put_user(sw->r10  , sc->sc_regs+10);
	__put_user(sw->r11  , sc->sc_regs+11);
	__put_user(sw->r12  , sc->sc_regs+12);
	__put_user(sw->r13  , sc->sc_regs+13);
	__put_user(sw->r14  , sc->sc_regs+14);
	__put_user(sw->r15  , sc->sc_regs+15);
	__put_user(regs->r16, sc->sc_regs+16);
	__put_user(regs->r17, sc->sc_regs+17);
	__put_user(regs->r18, sc->sc_regs+18);
	__put_user(regs->r19, sc->sc_regs+19);
	__put_user(regs->r20, sc->sc_regs+20);
	__put_user(regs->r21, sc->sc_regs+21);
	__put_user(regs->r22, sc->sc_regs+22);
	__put_user(regs->r23, sc->sc_regs+23);
	__put_user(regs->r24, sc->sc_regs+24);
	__put_user(regs->r25, sc->sc_regs+25);
	__put_user(regs->r26, sc->sc_regs+26);
	__put_user(regs->r27, sc->sc_regs+27);
	__put_user(regs->r28, sc->sc_regs+28);
	__put_user(regs->gp , sc->sc_regs+29);
	__put_user(sp, sc->sc_regs+30);
	__put_user(0, sc->sc_regs+31);

	for (i = 0; i < 31; i++)
		__put_user(sw->fp[i], sc->sc_fpregs+i);
	__put_user(0, sc->sc_fpregs+31);
	__put_user(sw->fp[31], &sc->sc_fpcr);

	__put_user(regs->trap_a0, &sc->sc_traparg_a0);
	__put_user(regs->trap_a1, &sc->sc_traparg_a1);
	__put_user(regs->trap_a2, &sc->sc_traparg_a2);
}

static void
setup_frame(int sig, struct k_sigaction *ka, sigset_t *set,
	    struct pt_regs *regs, struct switch_stack * sw)
{
	unsigned long oldsp;
	struct sigframe *frame;

	oldsp = rdusp();
	frame = (struct sigframe *)((oldsp - sizeof(*frame)) & -32);

	/* XXX: Check here if we would need to switch stacks.. */
	if (verify_area(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	setup_sigcontext(&frame->sc, regs, sw, set->sig[0], oldsp);
	if (_NSIG_WORDS > 1) {
		__copy_to_user(frame->extramask, &set->sig[1], 
			       sizeof(frame->extramask));
	}

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->ka_restorer) {
		regs->r26 = (unsigned long) ka->ka_restorer;
	} else {
		__put_user(INSN_MOV_R30_R16, frame->retcode+0);
		__put_user(INSN_LDI_R0+__NR_sigreturn, frame->retcode+1);
		__put_user(INSN_CALLSYS, frame->retcode+2);
		imb();
		regs->r26 = (unsigned long) frame->retcode;
	}

	/* "Return" to the handler */
	regs->r27 = regs->pc = (unsigned long) ka->sa.sa_handler;
	regs->r16 = sig;			/* a0: signal number */
	regs->r17 = 0;				/* a1: exception code */
	regs->r18 = (unsigned long) &frame->sc;	/* a2: sigcontext pointer */
	wrusp((unsigned long) frame);

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%p ra=%p\n",
		current->comm, current->pid, frame, regs->pc, regs->r26);
#endif

	return;

give_sigsegv:
	lock_kernel();
	do_exit(SIGSEGV);
}

static void
setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
	       sigset_t *set, struct pt_regs *regs, struct switch_stack * sw)
{
	unsigned long oldsp;
	struct rt_sigframe *frame;

	oldsp = rdusp();
	frame = (struct rt_sigframe *)((oldsp - sizeof(*frame)) & -32);

	/* XXX: Check here if we would need to switch stacks.. */
	if (verify_area(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	__copy_to_user(&frame->info, info, sizeof(siginfo_t));

	/* Zero all bits of the ucontext besides the sigcontext.  */
	__clear_user(&frame->uc, offsetof(struct ucontext, uc_mcontext));

	/* Copy in the bits we actually use.  */
	__put_user(set->sig[0], &frame->uc.uc_osf_sigmask);
	setup_sigcontext(&frame->uc.uc_mcontext, regs, sw, set->sig[0], oldsp);
	__copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->ka_restorer) {
		regs->r26 = (unsigned long) ka->ka_restorer;
	} else {
		__put_user(INSN_MOV_R30_R16, frame->retcode+0);
		__put_user(INSN_LDI_R0+__NR_rt_sigreturn, frame->retcode+1);
		__put_user(INSN_CALLSYS, frame->retcode+2);
		imb();
		regs->r26 = (unsigned long) frame->retcode;
	}

	/* "Return" to the handler */
	regs->r27 = regs->pc = (unsigned long) ka->sa.sa_handler;
	regs->r16 = sig;			  /* a0: signal number */
	regs->r17 = (unsigned long) &frame->info; /* a1: siginfo pointer */
	regs->r18 = (unsigned long) &frame->uc;	  /* a2: ucontext pointer */
	wrusp((unsigned long) frame);

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%p ra=%p\n",
		current->comm, current->pid, frame, regs->pc, regs->r26);
#endif

	return;

give_sigsegv:
	lock_kernel();
	do_exit(SIGSEGV);
}


/*
 * OK, we're invoking a handler.
 */
static inline void
handle_signal(int sig, struct k_sigaction *ka, siginfo_t *info,
	      sigset_t *oldset, struct pt_regs * regs, struct switch_stack *sw)
{
	if (ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame(sig, ka, info, oldset, regs, sw);
	else
		setup_frame(sig, ka, oldset, regs, sw);

	if (ka->sa.sa_flags & SA_RESETHAND)
		ka->sa.sa_handler = SIG_DFL;

	if (!(ka->sa.sa_flags & SA_NODEFER)) {
		spin_lock_irq(&current->sigmask_lock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,sig);
		recalc_sigpending(current);
		spin_unlock_irq(&current->sigmask_lock);
	}
}

static inline void
syscall_restart(unsigned long r0, unsigned long r19,
		struct pt_regs *regs, struct k_sigaction *ka)
{
	switch (regs->r0) {
	case ERESTARTSYS:
		if (!(ka->sa.sa_flags & SA_RESTART)) {
		case ERESTARTNOHAND:
			regs->r0 = EINTR;
			break;
		}
		/* fallthrough */
	case ERESTARTNOINTR:
		regs->r0 = r0;	/* reset v0 and a3 and replay syscall */
		regs->r19 = r19;
		regs->pc -= 4;
		break;
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
 *
 * "r0" and "r19" are the registers we need to restore for system call
 * restart. "r0" is also used as an indicator whether we can restart at
 * all (if we get here from anything but a syscall return, it will be 0)
 */
asmlinkage int
do_signal(sigset_t *oldset, struct pt_regs * regs, struct switch_stack * sw,
	  unsigned long r0, unsigned long r19)
{
	sigset_t _oldset;
	siginfo_t info;
	unsigned long signr, single_stepping, core = 0;
	struct k_sigaction *ka;

	single_stepping = ptrace_cancel_bpt(current);

	spin_lock_irq(current->sigmask_lock);
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
			single_stepping |= ptrace_cancel_bpt(current);

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
				single_stepping |= ptrace_cancel_bpt(current);
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
			if (r0) syscall_restart(r0, r19, regs, ka);
			handle_signal(signr, ka, &info, oldset, regs, sw);
			if (single_stepping) 
				ptrace_set_bpt(current); /* re-set bpt */
			return 1;
		}
	skip_signal:
		spin_lock_irq(&current->sigmask_lock);
	}
	spin_unlock_irq(&current->sigmask_lock);

	if (r0 &&
	    (regs->r0 == ERESTARTNOHAND ||
	     regs->r0 == ERESTARTSYS ||
	     regs->r0 == ERESTARTNOINTR)) {
		regs->r0 = r0;	/* reset v0 and a3 and replay syscall */
		regs->r19 = r19;
		regs->pc -= 4;
	}
	if (single_stepping)
		ptrace_set_bpt(current);	/* re-set breakpoint */

	return 0;
}

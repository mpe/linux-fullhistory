/*
 *  linux/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
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

#include <asm/uaccess.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

#ifndef __alpha__

/*
 * This call isn't used by all ports, in particular, the Alpha
 * uses osf_sigprocmask instead.  Maybe it should be moved into
 * arch-dependent dir?
 *
 * We don't need to get the kernel lock - this is all local to this
 * particular thread.. (and that's good, because this is _heavily_
 * used by various programs)
 *
 * No SMP locking would prevent the inherent races present in this
 * routine, thus we do not perform any locking at all.
 */
asmlinkage int sys_sigprocmask(int how, sigset_t *set, sigset_t *oset)
{
	sigset_t old_set = current->blocked;

	if (set) {
		sigset_t new_set;

		if(get_user(new_set, set))
			return -EFAULT;

		new_set &= _BLOCKABLE;
		switch (how) {
		default:
			return -EINVAL;
		case SIG_BLOCK:
			new_set |= old_set;
			break;
		case SIG_UNBLOCK:
			new_set = old_set & ~new_set;
			break;
		case SIG_SETMASK:
			break;
		}
		current->blocked = new_set;
	}
	if (oset) {
		if(put_user(old_set, oset))
			return -EFAULT;
	}
	return 0;
}

/*
 * For backwards compatibility?  Functionality superseded by sigprocmask.
 */
asmlinkage int sys_sgetmask(void)
{
	/* SMP safe */
	return current->blocked;
}

asmlinkage int sys_ssetmask(int newmask)
{
	int old;

	spin_lock_irq(&current->sigmask_lock);
	old = current->blocked;
	current->blocked = newmask & _BLOCKABLE;
	spin_unlock_irq(&current->sigmask_lock);

	return old;
}

#endif

asmlinkage int sys_sigpending(sigset_t *set)
{
	int ret;

	/* fill in "set" with signals pending but blocked. */
	spin_lock_irq(&current->sigmask_lock);
	ret = put_user(current->blocked & current->signal, set);
	spin_unlock_irq(&current->sigmask_lock);
	return ret;
}

/*
 * POSIX 3.3.1.3:
 *  "Setting a signal action to SIG_IGN for a signal that is pending
 *   shall cause the pending signal to be discarded, whether or not
 *   it is blocked."
 *
 *  "Setting a signal action to SIG_DFL for a signal that is pending
 *   and whose default action is to ignore the signal (for example,
 *   SIGCHLD), shall cause the pending signal to be discarded, whether
 *   or not it is blocked"
 *
 * Note the silly behaviour of SIGCHLD: SIG_IGN means that the signal
 * isn't actually ignored, but does automatic child reaping, while
 * SIG_DFL is explicitly said by POSIX to force the signal to be ignored..
 *
 * All callers of check_pending must be holding current->sig->siglock.
 */
inline void check_pending(int signum)
{
	struct sigaction *p;

	p = signum - 1 + current->sig->action;
	spin_lock(&current->sigmask_lock);
	if (p->sa_handler == SIG_IGN) {
		current->signal &= ~_S(signum);
	} else if (p->sa_handler == SIG_DFL) {
		if (signum == SIGCONT ||
		    signum == SIGCHLD ||
		    signum != SIGWINCH)
			current->signal &= ~_S(signum);
	}	
	spin_unlock(&current->sigmask_lock);
}

#ifndef __alpha__
/*
 * For backwards compatibility?  Functionality superseded by sigaction.
 */
asmlinkage unsigned long sys_signal(int signum, __sighandler_t handler)
{
	struct sigaction tmp;

	if (signum<1 || signum>32)
		return -EINVAL;
	if (signum==SIGKILL || signum==SIGSTOP)
		return -EINVAL;
	if (handler != SIG_DFL && handler != SIG_IGN) {
		if(verify_area(VERIFY_READ, handler, 1))
			return -EFAULT;
	}

	memset(&tmp, 0, sizeof(tmp));
	tmp.sa_handler = handler;
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK;

	spin_lock_irq(&current->sig->siglock);
	handler = current->sig->action[signum-1].sa_handler;
	current->sig->action[signum-1] = tmp;
	check_pending(signum);
	spin_unlock_irq(&current->sig->siglock);

	return (unsigned long) handler;
}
#endif

#ifndef __sparc__
asmlinkage int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction new_sa, *p;

	if (signum < 1 || signum > 32)
		return -EINVAL;

	p = signum - 1 + current->sig->action;

	if (action) {
		if (copy_from_user(&new_sa, action, sizeof(struct sigaction)))
			return -EFAULT;
		if (signum==SIGKILL || signum==SIGSTOP)
			return -EINVAL;
	}

	if (oldaction) {
		/* In the clone() case we could copy half consistant
		 * state to the user, however this could sleep and
		 * deadlock us if we held the signal lock on SMP.  So for
		 * now I take the easy way out and do no locking.
		 */
		if (copy_to_user(oldaction, p, sizeof(struct sigaction)))
			return -EFAULT;
	}

	if (action) {
		spin_lock_irq(&current->sig->siglock);
		*p = new_sa;
		check_pending(signum);
		spin_unlock_irq(&current->sig->siglock);
	}
	return 0;
}
#endif

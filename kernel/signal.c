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
 */
asmlinkage int sys_sigprocmask(int how, sigset_t *set, sigset_t *oset)
{
	sigset_t new_set, old_set = current->blocked;
	int error;

	lock_kernel();
	if (set) {
		error = get_user(new_set, set);
		if (error)
			goto out;
		new_set &= _BLOCKABLE;
		switch (how) {
		case SIG_BLOCK:
			current->blocked |= new_set;
			break;
		case SIG_UNBLOCK:
			current->blocked &= ~new_set;
			break;
		case SIG_SETMASK:
			current->blocked = new_set;
			break;
		default:
			error = -EINVAL;
			goto out;
		}
	}
	if (oset) {
		error = put_user(old_set, oset);
		if (error)
			goto out;
	}
	error = 0;
out:
	unlock_kernel();
	return error;
}

/*
 * For backwards compatibility?  Functionality superseded by sigprocmask.
 */
asmlinkage int sys_sgetmask(void)
{
	int ret;

	lock_kernel();
	ret = current->blocked;
	unlock_kernel();
	return ret;
}

asmlinkage int sys_ssetmask(int newmask)
{
	int old;

	lock_kernel();
	old = current->blocked;
	current->blocked = newmask & _BLOCKABLE;
	unlock_kernel();
	return old;
}

#endif

asmlinkage int sys_sigpending(sigset_t *set)
{
	int ret;

	/* fill in "set" with signals pending but blocked. */
	lock_kernel();
	ret = put_user(current->blocked & current->signal, set);
	unlock_kernel();
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
 */
inline void check_pending(int signum)
{
	struct sigaction *p;

	p = signum - 1 + current->sig->action;
	if (p->sa_handler == SIG_IGN) {
		current->signal &= ~_S(signum);
		return;
	}
	if (p->sa_handler == SIG_DFL) {
		if (signum != SIGCONT && signum != SIGCHLD && signum != SIGWINCH)
			return;
		current->signal &= ~_S(signum);
		return;
	}	
}

#ifndef __alpha__
/*
 * For backwards compatibility?  Functionality superseded by sigaction.
 */
asmlinkage unsigned long sys_signal(int signum, __sighandler_t handler)
{
	unsigned long err;
	struct sigaction tmp;

	lock_kernel();
	err = -EINVAL;
	if (signum<1 || signum>32)
		goto out;
	if (signum==SIGKILL || signum==SIGSTOP)
		goto out;
	if (handler != SIG_DFL && handler != SIG_IGN) {
		err = verify_area(VERIFY_READ, handler, 1);
		if (err)
			goto out;
	}
	memset(&tmp, 0, sizeof(tmp));
	tmp.sa_handler = handler;
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK;
	handler = current->sig->action[signum-1].sa_handler;
	current->sig->action[signum-1] = tmp;
	check_pending(signum);
	err = (unsigned long) handler;
out:
	unlock_kernel();
	return err;
}
#endif

#ifndef __sparc__
asmlinkage int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction new_sa, *p;
	int ret = -EINVAL;

	lock_kernel();
	if (signum<1 || signum>32)
		goto out;
	p = signum - 1 + current->sig->action;
	if (action) {
		ret = verify_area(VERIFY_READ, action, sizeof(*action));
		if (ret)
			goto out;
		ret = -EINVAL;
		if (signum==SIGKILL || signum==SIGSTOP)
			goto out;
		ret = -EFAULT;
		if (copy_from_user(&new_sa, action, sizeof(struct sigaction)))
			goto out;
		if (new_sa.sa_handler != SIG_DFL && new_sa.sa_handler != SIG_IGN) {
			ret = verify_area(VERIFY_READ, new_sa.sa_handler, 1);
			if (ret)
				goto out;
		}
	}
	ret = -EFAULT;
	if (oldaction) {
		if (copy_to_user(oldaction, p, sizeof(struct sigaction)))
			goto out;	
	}
	if (action) {
		*p = new_sa;
		check_pending(signum);
	}
	ret = 0;
out:
	unlock_kernel();
	return ret;
}
#endif

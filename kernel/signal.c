/*
 *  linux/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-02  Modified for POSIX.1b signals by Richard Henderson
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/unistd.h>
#include <linux/smp_lock.h>
#include <linux/init.h>

#include <asm/uaccess.h>

/*
 * SLAB caches for signal bits.
 */

#define DEBUG_SIG 0

#if DEBUG_SIG
#define SIG_SLAB_DEBUG	(SLAB_DEBUG_FREE | SLAB_RED_ZONE /* | SLAB_POISON */)
#else
#define SIG_SLAB_DEBUG	0
#endif

static kmem_cache_t *signal_queue_cachep;

int nr_queued_signals;
int max_queued_signals = 1024;

void __init signals_init(void)
{
	signal_queue_cachep =
		kmem_cache_create("signal_queue",
				  sizeof(struct signal_queue),
				  __alignof__(struct signal_queue),
				  SIG_SLAB_DEBUG, NULL, NULL);
}


/*
 * Flush all pending signals for a task.
 */

void
flush_signals(struct task_struct *t)
{
	struct signal_queue *q, *n;

	t->sigpending = 0;
	sigemptyset(&t->signal);
	q = t->sigqueue;
	t->sigqueue = NULL;
	t->sigqueue_tail = &t->sigqueue;

	while (q) {
		n = q->next;
		kmem_cache_free(signal_queue_cachep, q);
		nr_queued_signals--;
		q = n;
	}
}

/*
 * Flush all handlers for a task.
 */

void
flush_signal_handlers(struct task_struct *t)
{
	int i;
	struct k_sigaction *ka = &t->sig->action[0];
	for (i = _NSIG ; i != 0 ; i--) {
		if (ka->sa.sa_handler != SIG_IGN)
			ka->sa.sa_handler = SIG_DFL;
		ka->sa.sa_flags = 0;
		sigemptyset(&ka->sa.sa_mask);
		ka++;
	}
}

/*
 * Dequeue a signal and return the element to the caller, which is 
 * expected to free it.
 *
 * All callers of must be holding current->sigmask_lock.
 */

int
dequeue_signal(sigset_t *mask, siginfo_t *info)
{
	unsigned long i, *s, *m, x;
	int sig = 0;

#if DEBUG_SIG
printk("SIG dequeue (%s:%d): %d ", current->comm, current->pid,
	signal_pending(current));
#endif

	/* Find the first desired signal that is pending.  */
	s = current->signal.sig;
	m = mask->sig;
	switch (_NSIG_WORDS) {
	default:
		for (i = 0; i < _NSIG_WORDS; ++i, ++s, ++m)
			if ((x = *s &~ *m) != 0) {
				sig = ffz(~x) + i*_NSIG_BPW + 1;
				break;
			}
		break;

	case 2: if ((x = s[0] &~ m[0]) != 0)
			sig = 1;
		else if ((x = s[1] &~ m[1]) != 0)
			sig = _NSIG_BPW + 1;
		else
			break;
		sig += ffz(~x);
		break;

	case 1: if ((x = *s &~ *m) != 0)
			sig = ffz(~x) + 1;
		break;
	}

	if (sig) {
		int reset = 1;

		/* Collect the siginfo appropriate to this signal.  */
		if (sig < SIGRTMIN) {
			/* XXX: As an extension, support queueing exactly
			   one non-rt signal if SA_SIGINFO is set, so that
			   we can get more detailed information about the
			   cause of the signal.  */
			/* Deciding not to init these couple of fields is
			   more expensive that just initializing them.  */
			info->si_signo = sig;
			info->si_errno = 0;
			info->si_code = 0;
			info->si_pid = 0;
			info->si_uid = 0;
		} else {
			struct signal_queue *q, **pp;
			pp = &current->sigqueue;
			q = current->sigqueue;

			/* Find the one we're interested in ... */
			for ( ; q ; pp = &q->next, q = q->next)
				if (q->info.si_signo == sig)
					break;
			if (q) {
				if ((*pp = q->next) == NULL)
					current->sigqueue_tail = pp;
				*info = q->info;
				kmem_cache_free(signal_queue_cachep,q);
				nr_queued_signals--;
				
				/* then see if this signal is still pending. */
				q = *pp;
				while (q) {
					if (q->info.si_signo == sig) {
						reset = 0;
						break;
					}
					q = q->next;
				}
			} else {
				/* Ok, it wasn't in the queue.  It must have
				   been sent either by a non-rt mechanism and
				   we ran out of queue space.  So zero out the
				   info.  */
				info->si_signo = sig;
				info->si_errno = 0;
				info->si_code = 0;
				info->si_pid = 0;
				info->si_uid = 0;
			}
		}

		if (reset)
			sigdelset(&current->signal, sig);
		recalc_sigpending(current);

		/* XXX: Once POSIX.1b timers are in, if si_code == SI_TIMER,
		   we need to xchg out the timer overrun values.  */
	} else {
		/* XXX: Once CLONE_PID is in to join those "threads" that are
		   part of the same "process", look for signals sent to the
		   "process" as well.  */

		/* Sanity check... */
		if (mask == &current->blocked && signal_pending(current)) {
			printk(KERN_CRIT "SIG: sigpending lied\n");
			current->sigpending = 0;
		}
	}

#if DEBUG_SIG
printk(" %d -> %d\n", signal_pending(current), sig);
#endif

	return sig;
}

/*
 * Determine whether a signal should be posted or not.
 *
 * Signals with SIG_IGN can be ignored, except for the
 * special case of a SIGCHLD. 
 *
 * Some signals with SIG_DFL default to a non-action.
 */
static int ignored_signal(int sig, struct task_struct *t)
{
	struct signal_struct *signals;
	struct k_sigaction *ka;

	/* Don't ignore traced or blocked signals */
	if ((t->flags & PF_PTRACED) || sigismember(&t->blocked, sig))
		return 0;
	
	signals = t->sig;
	if (!signals)
		return 1;

	ka = &signals->action[sig-1];
	switch ((unsigned long) ka->sa.sa_handler) {
	case (unsigned long) SIG_DFL:
		if (sig == SIGCONT ||
		    sig == SIGWINCH ||
		    sig == SIGCHLD ||
		    sig == SIGURG)
			break;
		return 0;

	case (unsigned long) SIG_IGN:
		if (sig != SIGCHLD)
			break;
	/* fallthrough */
	default:
		return 0;
	}
	return 1;
}

int
send_sig_info(int sig, struct siginfo *info, struct task_struct *t)
{
	unsigned long flags;
	int ret;

#if DEBUG_SIG
printk("SIG queue (%s:%d): %d ", t->comm, t->pid, sig);
#endif

	ret = -EINVAL;
	if (sig < 0 || sig > _NSIG)
		goto out_nolock;
	/* The somewhat baroque permissions check... */
	ret = -EPERM;
	if ((!info || ((unsigned long)info != 1 && SI_FROMUSER(info)))
	    && ((sig != SIGCONT) || (current->session != t->session))
	    && (current->euid ^ t->suid) && (current->euid ^ t->uid)
	    && (current->uid ^ t->suid) && (current->uid ^ t->uid)
	    && !capable(CAP_SYS_ADMIN))
		goto out_nolock;

	/* The null signal is a permissions and process existance probe.
	   No signal is actually delivered.  Same goes for zombies. */
	ret = 0;
	if (!sig || !t->sig)
		goto out_nolock;

	spin_lock_irqsave(&t->sigmask_lock, flags);
	switch (sig) {
	case SIGKILL: case SIGCONT:
		/* Wake up the process if stopped.  */
		if (t->state == TASK_STOPPED)
			wake_up_process(t);
		t->exit_code = 0;
		sigdelsetmask(&t->signal, (sigmask(SIGSTOP)|sigmask(SIGTSTP)|
					   sigmask(SIGTTOU)|sigmask(SIGTTIN)));
		/* Inflict this corner case with recalculations, not mainline */
		recalc_sigpending(t);
		break;

	case SIGSTOP: case SIGTSTP:
	case SIGTTIN: case SIGTTOU:
		/* If we're stopping again, cancel SIGCONT */
		sigdelset(&t->signal, SIGCONT);
		/* Inflict this corner case with recalculations, not mainline */
		recalc_sigpending(t);
		break;
	}

	/* Optimize away the signal, if it's a signal that can be
	   handled immediately (ie non-blocked and untraced) and
	   that is ignored (either explicitly or by default).  */

	if (ignored_signal(sig, t))
		goto out;

	if (sig < SIGRTMIN) {
		/* Non-real-time signals are not queued.  */
		/* XXX: As an extension, support queueing exactly one
		   non-rt signal if SA_SIGINFO is set, so that we can
		   get more detailed information about the cause of
		   the signal.  */
		if (sigismember(&t->signal, sig))
			goto out;
	} else {
		/* Real-time signals must be queued if sent by sigqueue, or
		   some other real-time mechanism.  It is implementation
		   defined whether kill() does so.  We attempt to do so, on
		   the principle of least surprise, but since kill is not
		   allowed to fail with EAGAIN when low on memory we just
		   make sure at least one signal gets delivered and don't
		   pass on the info struct.  */

		struct signal_queue *q = 0;

		if (nr_queued_signals < max_queued_signals) {
			q = (struct signal_queue *)
			    kmem_cache_alloc(signal_queue_cachep, GFP_KERNEL);
		}
		
		if (q) {
			nr_queued_signals++;
			q->next = NULL;
			*t->sigqueue_tail = q;
			t->sigqueue_tail = &q->next;
			switch ((unsigned long) info) {
			case 0:
				q->info.si_signo = sig;
				q->info.si_errno = 0;
				q->info.si_code = SI_USER;
				q->info.si_pid = current->pid;
				q->info.si_uid = current->uid;
				break;
			case 1:
				q->info.si_signo = sig;
				q->info.si_errno = 0;
				q->info.si_code = SI_KERNEL;
				q->info.si_pid = 0;
				q->info.si_uid = 0;
				break;
			default:
				q->info = *info;
				break;
			}
		} else {
			/* If this was sent by a rt mechanism, try again.  */
			if (info->si_code < 0) {
				ret = -EAGAIN;
				goto out;
			}
			/* Otherwise, mention that the signal is pending,
			   but don't queue the info.  */
		}
	}

	sigaddset(&t->signal, sig);
	if (!sigismember(&t->blocked, sig))
		t->sigpending = 1;

out:
	spin_unlock_irqrestore(&t->sigmask_lock, flags);
        if (t->state == TASK_INTERRUPTIBLE && signal_pending(t))
                wake_up_process(t);

out_nolock:
#if DEBUG_SIG
printk(" %d -> %d\n", signal_pending(t), ret);
#endif

	return ret;
}

/*
 * Force a signal that the process can't ignore: if necessary
 * we unblock the signal and change any SIG_IGN to SIG_DFL.
 */

int
force_sig_info(int sig, struct siginfo *info, struct task_struct *t)
{
	unsigned long int flags;

	spin_lock_irqsave(&t->sigmask_lock, flags);
	if (t->sig == NULL) {
		spin_unlock_irqrestore(&t->sigmask_lock, flags);
		return -ESRCH;
	}

	if (t->sig->action[sig-1].sa.sa_handler == SIG_IGN)
		t->sig->action[sig-1].sa.sa_handler = SIG_DFL;
	sigdelset(&t->blocked, sig);
	spin_unlock_irqrestore(&t->sigmask_lock, flags);

	return send_sig_info(sig, info, t);
}

/*
 * kill_pg() sends a signal to a process group: this is what the tty
 * control characters do (^C, ^Z etc)
 */

int
kill_pg_info(int sig, struct siginfo *info, pid_t pgrp)
{
	int retval = -EINVAL;
	if (pgrp > 0) {
		struct task_struct *p;
		int found = 0;

		retval = -ESRCH;
		read_lock(&tasklist_lock);
		for_each_task(p) {
			if (p->pgrp == pgrp) {
				int err = send_sig_info(sig, info, p);
				if (err != 0)
					retval = err;
				else
					found++;
			}
		}
		read_unlock(&tasklist_lock);
		if (found)
			retval = 0;
	}
	return retval;
}

/*
 * kill_sl() sends a signal to the session leader: this is used
 * to send SIGHUP to the controlling process of a terminal when
 * the connection is lost.
 */

int
kill_sl_info(int sig, struct siginfo *info, pid_t sess)
{
	int retval = -EINVAL;
	if (sess > 0) {
		struct task_struct *p;
		int found = 0;

		retval = -ESRCH;
		read_lock(&tasklist_lock);
		for_each_task(p) {
			if (p->leader && p->session == sess) {
				int err = send_sig_info(sig, info, p);
				if (err)
					retval = err;
				else
					found++;
			}
		}
		read_unlock(&tasklist_lock);
		if (found)
			retval = 0;
	}
	return retval;
}

inline int
kill_proc_info(int sig, struct siginfo *info, pid_t pid)
{
	int error;
	struct task_struct *p;

	read_lock(&tasklist_lock);
	p = find_task_by_pid(pid);
	error = -ESRCH;
	if (p)
		error = send_sig_info(sig, info, p);
	read_unlock(&tasklist_lock);
	return error;
}

/*
 * kill_something() interprets pid in interesting ways just like kill(2).
 *
 * POSIX specifies that kill(-1,sig) is unspecified, but what we have
 * is probably wrong.  Should make it like BSD or SYSV.
 */

int
kill_something_info(int sig, struct siginfo *info, int pid)
{
	if (!pid) {
		return kill_pg_info(sig, info, current->pgrp);
	} else if (pid == -1) {
		int retval = 0, count = 0;
		struct task_struct * p;

		read_lock(&tasklist_lock);
		for_each_task(p) {
			if (p->pid > 1 && p != current) {
				int err = send_sig_info(sig, info, p);
				++count;
				if (err != -EPERM)
					retval = err;
			}
		}
		read_unlock(&tasklist_lock);
		return count ? retval : -ESRCH;
	} else if (pid < 0) {
		return kill_pg_info(sig, info, -pid);
	} else {
		return kill_proc_info(sig, info, pid);
	}
}

/*
 * These are for backward compatibility with the rest of the kernel source.
 */

int
send_sig(int sig, struct task_struct *p, int priv)
{
	return send_sig_info(sig, (void*)(long)(priv != 0), p);
}

void
force_sig(int sig, struct task_struct *p)
{
	force_sig_info(sig, (void*)1L, p);
}

int
kill_pg(pid_t pgrp, int sig, int priv)
{
	return kill_pg_info(sig, (void *)(long)(priv != 0), pgrp);
}

int
kill_sl(pid_t sess, int sig, int priv)
{
	return kill_sl_info(sig, (void *)(long)(priv != 0), sess);
}

int
kill_proc(pid_t pid, int sig, int priv)
{
	return kill_proc_info(sig, (void *)(long)(priv != 0), pid);
}

/*
 * Let a parent know about a status change of a child.
 */

void
notify_parent(struct task_struct *tsk, int sig)
{
	struct siginfo info;
	int why;

	info.si_signo = sig;
	info.si_errno = 0;
	info.si_pid = tsk->pid;

	/* FIXME: find out whether or not this is supposed to be c*time. */
	info.si_utime = tsk->times.tms_utime;
	info.si_stime = tsk->times.tms_stime;

	why = SI_KERNEL;	/* shouldn't happen */
	switch (tsk->state) {
	case TASK_ZOMBIE:
		if (tsk->exit_code & 0x80)
			why = CLD_DUMPED;
		else if (tsk->exit_code & 0x7f)
			why = CLD_KILLED;
		else
			why = CLD_EXITED;
		break;
	case TASK_STOPPED:
		/* FIXME -- can we deduce CLD_TRAPPED or CLD_CONTINUED? */
		why = CLD_STOPPED;
		break;

	default:
		printk(KERN_DEBUG "eh? notify_parent with state %ld?\n",
		       tsk->state);
		break;
	}
	info.si_code = why;

	send_sig_info(sig, &info, tsk->p_pptr);
	wake_up_interruptible(&tsk->p_pptr->wait_chldexit);
}

EXPORT_SYMBOL(dequeue_signal);
EXPORT_SYMBOL(flush_signals);
EXPORT_SYMBOL(force_sig);
EXPORT_SYMBOL(force_sig_info);
EXPORT_SYMBOL(kill_pg);
EXPORT_SYMBOL(kill_pg_info);
EXPORT_SYMBOL(kill_proc);
EXPORT_SYMBOL(kill_proc_info);
EXPORT_SYMBOL(kill_sl);
EXPORT_SYMBOL(kill_sl_info);
EXPORT_SYMBOL(notify_parent);
EXPORT_SYMBOL(recalc_sigpending);
EXPORT_SYMBOL(send_sig);
EXPORT_SYMBOL(send_sig_info);


/*
 * System call entry points.
 */

/*
 * We don't need to get the kernel lock - this is all local to this
 * particular thread.. (and that's good, because this is _heavily_
 * used by various programs)
 */

asmlinkage int
sys_rt_sigprocmask(int how, sigset_t *set, sigset_t *oset, size_t sigsetsize)
{
	int error = -EINVAL;
	sigset_t old_set, new_set;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		goto out;

	if (set) {
		error = -EFAULT;
		if (copy_from_user(&new_set, set, sizeof(*set)))
			goto out;
		sigdelsetmask(&new_set, sigmask(SIGKILL)|sigmask(SIGSTOP));

		spin_lock_irq(&current->sigmask_lock);
		old_set = current->blocked;

		error = 0;
		switch (how) {
		default:
			error = -EINVAL;
			break;
		case SIG_BLOCK:
			sigorsets(&new_set, &old_set, &new_set);
			break;
		case SIG_UNBLOCK:
			signandsets(&new_set, &old_set, &new_set);
			break;
		case SIG_SETMASK:
			break;
		}

		current->blocked = new_set;
		recalc_sigpending(current);
		spin_unlock_irq(&current->sigmask_lock);
		if (error)
			goto out;
		if (oset)
			goto set_old;
	} else if (oset) {
		spin_lock_irq(&current->sigmask_lock);
		old_set = current->blocked;
		spin_unlock_irq(&current->sigmask_lock);

	set_old:
		error = -EFAULT;
		if (copy_to_user(oset, &old_set, sizeof(*oset)))
			goto out;
	}
	error = 0;
out:
	return error;
}

asmlinkage int
sys_rt_sigpending(sigset_t *set, size_t sigsetsize)
{
	int error = -EINVAL;
	sigset_t pending;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		goto out;

	spin_lock_irq(&current->sigmask_lock);
	sigandsets(&pending, &current->blocked, &current->signal);
	spin_unlock_irq(&current->sigmask_lock);

	error = -EFAULT;
	if (!copy_to_user(set, &pending, sizeof(*set)))
		error = 0;
out:
	return error;
}

asmlinkage int
sys_rt_sigtimedwait(const sigset_t *uthese, siginfo_t *uinfo,
		    const struct timespec *uts, size_t sigsetsize)
{
	int ret, sig;
	sigset_t these;
	struct timespec ts;
	siginfo_t info;
	long timeout = 0;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&these, uthese, sizeof(these)))
		return -EFAULT;
	else {
		/* Invert the set of allowed signals to get those we
		   want to block.  */
		signotset(&these);
	}

	if (uts) {
		if (copy_from_user(&ts, uts, sizeof(ts)))
			return -EFAULT;
		if (ts.tv_nsec >= 1000000000L || ts.tv_nsec < 0
		    || ts.tv_sec < 0)
			return -EINVAL;
	}

	spin_lock_irq(&current->sigmask_lock);
	sig = dequeue_signal(&these, &info);
	if (!sig) {
		/* None ready -- temporarily unblock those we're interested
		   in so that we'll be awakened when they arrive.  */
		sigset_t oldblocked = current->blocked;
		sigandsets(&current->blocked, &current->blocked, &these);
		recalc_sigpending(current);
		spin_unlock_irq(&current->sigmask_lock);

		timeout = MAX_SCHEDULE_TIMEOUT;
		if (uts)
			timeout = (timespec_to_jiffies(&ts)
				   + (ts.tv_sec || ts.tv_nsec));

		current->state = TASK_INTERRUPTIBLE;
		timeout = schedule_timeout(timeout);

		spin_lock_irq(&current->sigmask_lock);
		sig = dequeue_signal(&these, &info);
		current->blocked = oldblocked;
		recalc_sigpending(current);
	}
	spin_unlock_irq(&current->sigmask_lock);

	if (sig) {
		ret = sig;
		if (uinfo) {
			if (copy_to_user(uinfo, &info, sizeof(siginfo_t)))
				ret = -EFAULT;
		}
	} else {
		ret = -EAGAIN;
		if (timeout)
			ret = -EINTR;
	}

	return ret;
}

asmlinkage int
sys_kill(int pid, int sig)
{
	struct siginfo info;

	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = SI_USER;
	info.si_pid = current->pid;
	info.si_uid = current->uid;

	return kill_something_info(sig, &info, pid);
}

asmlinkage int
sys_rt_sigqueueinfo(int pid, int sig, siginfo_t *uinfo)
{
	siginfo_t info;

	if (copy_from_user(&info, uinfo, sizeof(siginfo_t)))
		return -EFAULT;

	/* Not even root can pretend to send signals from the kernel.
	   Nor can they impersonate a kill(), which adds source info.  */
	if (info.si_code >= 0)
		return -EPERM;
	info.si_signo = sig;

	/* POSIX.1b doesn't mention process groups.  */
	return kill_proc_info(sig, &info, pid);
}

int
do_sigaction(int sig, const struct k_sigaction *act, struct k_sigaction *oact)
{
	struct k_sigaction *k;

	if (sig < 1 || sig > _NSIG ||
	    (act && (sig == SIGKILL || sig == SIGSTOP)))
		return -EINVAL;

	spin_lock_irq(&current->sigmask_lock);
	k = &current->sig->action[sig-1];

	if (oact) *oact = *k;

	if (act) {
		*k = *act;
		sigdelsetmask(&k->sa.sa_mask, sigmask(SIGKILL) | sigmask(SIGSTOP));

		/*
		 * POSIX 3.3.1.3:
		 *  "Setting a signal action to SIG_IGN for a signal that is
		 *   pending shall cause the pending signal to be discarded,
		 *   whether or not it is blocked."
		 *
		 *  "Setting a signal action to SIG_DFL for a signal that is
		 *   pending and whose default action is to ignore the signal
		 *   (for example, SIGCHLD), shall cause the pending signal to
		 *   be discarded, whether or not it is blocked"
		 *
		 * Note the silly behaviour of SIGCHLD: SIG_IGN means that the
		 * signal isn't actually ignored, but does automatic child
		 * reaping, while SIG_DFL is explicitly said by POSIX to force
		 * the signal to be ignored.
		 */

		if (k->sa.sa_handler == SIG_IGN
		    || (k->sa.sa_handler == SIG_DFL
			&& (sig == SIGCONT ||
			    sig == SIGCHLD ||
			    sig == SIGWINCH))) {
			/* So dequeue any that might be pending.
			   XXX: process-wide signals? */
			if (sig >= SIGRTMIN &&
			    sigismember(&current->signal, sig)) {
				struct signal_queue *q, **pp;
				pp = &current->sigqueue;
				q = current->sigqueue;
				while (q) {
					if (q->info.si_signo != sig)
						pp = &q->next;
					else {
						*pp = q->next;
						kmem_cache_free(signal_queue_cachep, q);
						nr_queued_signals--;
					}
					q = *pp;
				}
				
			}
			sigdelset(&current->signal, sig);
			recalc_sigpending(current);
		}
	}

	spin_unlock_irq(&current->sigmask_lock);

	return 0;
}

int 
do_sigaltstack (const stack_t *uss, stack_t *uoss, unsigned long sp)
{
	stack_t oss;
	int error;

	if (uoss) {
		oss.ss_sp = (void *) current->sas_ss_sp;
		oss.ss_size = current->sas_ss_size;
		oss.ss_flags = sas_ss_flags(sp);
	}

	if (uss) {
		void *ss_sp;
		size_t ss_size;
		int ss_flags;

		error = -EFAULT;
		if (verify_area(VERIFY_READ, uss, sizeof(*uss))
		    || __get_user(ss_sp, &uss->ss_sp)
		    || __get_user(ss_flags, &uss->ss_flags)
		    || __get_user(ss_size, &uss->ss_size))
			goto out;

		error = -EPERM;
		if (on_sig_stack (sp))
			goto out;

		error = -EINVAL;
		if (ss_flags & ~SS_DISABLE)
			goto out;

		if (ss_flags & SS_DISABLE) {
			ss_size = 0;
			ss_sp = NULL;
		} else {
			error = -ENOMEM;
			if (ss_size < MINSIGSTKSZ)
				goto out;
		}

		current->sas_ss_sp = (unsigned long) ss_sp;
		current->sas_ss_size = ss_size;
	}

	if (uoss) {
		error = -EFAULT;
		if (copy_to_user(uoss, &oss, sizeof(oss)))
			goto out;
	}

	error = 0;
out:
	return error;
}

#if !defined(__alpha__)
/* Alpha has its own versions with special arguments.  */

asmlinkage int
sys_sigprocmask(int how, old_sigset_t *set, old_sigset_t *oset)
{
	int error;
	old_sigset_t old_set, new_set;

	if (set) {
		error = -EFAULT;
		if (copy_from_user(&new_set, set, sizeof(*set)))
			goto out;
		new_set &= ~(sigmask(SIGKILL)|sigmask(SIGSTOP));

		spin_lock_irq(&current->sigmask_lock);
		old_set = current->blocked.sig[0];

		error = 0;
		switch (how) {
		default:
			error = -EINVAL;
			break;
		case SIG_BLOCK:
			sigaddsetmask(&current->blocked, new_set);
			break;
		case SIG_UNBLOCK:
			sigdelsetmask(&current->blocked, new_set);
			break;
		case SIG_SETMASK:
			current->blocked.sig[0] = new_set;
			break;
		}

		recalc_sigpending(current);
		spin_unlock_irq(&current->sigmask_lock);
		if (error)
			goto out;
		if (oset)
			goto set_old;
	} else if (oset) {
		old_set = current->blocked.sig[0];
	set_old:
		error = -EFAULT;
		if (copy_to_user(oset, &old_set, sizeof(*oset)))
			goto out;
	}
	error = 0;
out:
	return error;
}

asmlinkage int
sys_sigpending(old_sigset_t *set)
{
	int error;
	old_sigset_t pending;

	spin_lock_irq(&current->sigmask_lock);
	pending = current->blocked.sig[0] & current->signal.sig[0];
	spin_unlock_irq(&current->sigmask_lock);

	error = -EFAULT;
	if (!copy_to_user(set, &pending, sizeof(*set)))
		error = 0;
	return error;
}

#ifndef __sparc__
asmlinkage int
sys_rt_sigaction(int sig, const struct sigaction *act, struct sigaction *oact,
		 size_t sigsetsize)
{
	struct k_sigaction new_sa, old_sa;
	int ret = -EINVAL;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		goto out;

	if (act) {
		if (copy_from_user(&new_sa.sa, act, sizeof(new_sa.sa)))
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_sa : NULL, oact ? &old_sa : NULL);

	if (!ret && oact) {
		if (copy_to_user(oact, &old_sa.sa, sizeof(old_sa.sa)))
			return -EFAULT;
	}
out:
	return ret;
}
#endif /* __sparc__ */
#endif

#if !defined(__alpha__)
/*
 * For backwards compatibility.  Functionality superseded by sigprocmask.
 */
asmlinkage int
sys_sgetmask(void)
{
	/* SMP safe */
	return current->blocked.sig[0];
}

asmlinkage int
sys_ssetmask(int newmask)
{
	int old;

	spin_lock_irq(&current->sigmask_lock);
	old = current->blocked.sig[0];

	siginitset(&current->blocked, newmask & ~(sigmask(SIGKILL)|
						  sigmask(SIGSTOP)));
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	return old;
}

/*
 * For backwards compatibility.  Functionality superseded by sigaction.
 */
asmlinkage unsigned long
sys_signal(int sig, __sighandler_t handler)
{
	struct k_sigaction new_sa, old_sa;
	int ret;

	new_sa.sa.sa_handler = handler;
	new_sa.sa.sa_flags = SA_ONESHOT | SA_NOMASK;

	ret = do_sigaction(sig, &new_sa, &old_sa);

	return ret ? ret : (unsigned long)old_sa.sa.sa_handler;
}
#endif /* !alpha */

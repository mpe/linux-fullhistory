/*
 *  linux/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-02  Modified for POSIX.1b signals by Richard Henderson
 */

#define __KERNEL_SYSCALLS__

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/unistd.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/binfmts.h>
#include <linux/security.h>
#include <asm/param.h>
#include <asm/uaccess.h>
#include <asm/siginfo.h>

/*
 * SLAB caches for signal bits.
 */

static kmem_cache_t *sigqueue_cachep;

atomic_t nr_queued_signals;
int max_queued_signals = 1024;

/*********************************************************

    POSIX thread group signal behavior:

----------------------------------------------------------
|                    |  userspace       |  kernel        |
----------------------------------------------------------
|  SIGHUP            |  load-balance    |  kill-all      |
|  SIGINT            |  load-balance    |  kill-all      |
|  SIGQUIT           |  load-balance    |  kill-all+core |
|  SIGILL            |  specific        |  kill-all+core |
|  SIGTRAP           |  specific        |  kill-all+core |
|  SIGABRT/SIGIOT    |  specific        |  kill-all+core |
|  SIGBUS            |  specific        |  kill-all+core |
|  SIGFPE            |  specific        |  kill-all+core |
|  SIGKILL           |  n/a             |  kill-all      |
|  SIGUSR1           |  load-balance    |  kill-all      |
|  SIGSEGV           |  specific        |  kill-all+core |
|  SIGUSR2           |  load-balance    |  kill-all      |
|  SIGPIPE           |  specific        |  kill-all      |
|  SIGALRM           |  load-balance    |  kill-all      |
|  SIGTERM           |  load-balance    |  kill-all      |
|  SIGCHLD           |  load-balance    |  ignore        |
|  SIGCONT           |  load-balance    |  ignore        |
|  SIGSTOP           |  n/a             |  stop-all      |
|  SIGTSTP           |  load-balance    |  stop-all      |
|  SIGTTIN           |  load-balance    |  stop-all      |
|  SIGTTOU           |  load-balance    |  stop-all      |
|  SIGURG            |  load-balance    |  ignore        |
|  SIGXCPU           |  specific        |  kill-all+core |
|  SIGXFSZ           |  specific        |  kill-all+core |
|  SIGVTALRM         |  load-balance    |  kill-all      |
|  SIGPROF           |  specific        |  kill-all      |
|  SIGPOLL/SIGIO     |  load-balance    |  kill-all      |
|  SIGSYS/SIGUNUSED  |  specific        |  kill-all+core |
|  SIGSTKFLT         |  specific        |  kill-all      |
|  SIGWINCH          |  load-balance    |  ignore        |
|  SIGPWR            |  load-balance    |  kill-all      |
|  SIGRTMIN-SIGRTMAX |  load-balance    |  kill-all      |
----------------------------------------------------------

    non-POSIX signal thread group behavior:

----------------------------------------------------------
|                    |  userspace       |  kernel        |
----------------------------------------------------------
|  SIGEMT            |  specific        |  kill-all+core |
----------------------------------------------------------
*/

/* Some systems do not have a SIGSTKFLT and the kernel never
 * generates such signals anyways.
 */
#ifdef SIGSTKFLT
#define M_SIGSTKFLT	M(SIGSTKFLT)
#else
#define M_SIGSTKFLT	0
#endif

#ifdef SIGEMT
#define M_SIGEMT	M(SIGEMT)
#else
#define M_SIGEMT	0
#endif

#if SIGRTMIN > BITS_PER_LONG
#define M(sig) (1ULL << ((sig)-1))
#else
#define M(sig) (1UL << ((sig)-1))
#endif
#define T(sig, mask) (M(sig) & (mask))

#define SIG_KERNEL_BROADCAST_MASK (\
	M(SIGHUP)    |  M(SIGINT)    |  M(SIGQUIT)   |  M(SIGILL)    | \
	M(SIGTRAP)   |  M(SIGABRT)   |  M(SIGBUS)    |  M(SIGFPE)    | \
	M(SIGKILL)   |  M(SIGUSR1)   |  M(SIGSEGV)   |  M(SIGUSR2)   | \
	M(SIGPIPE)   |  M(SIGALRM)   |  M(SIGTERM)   |  M(SIGXCPU)   | \
	M(SIGXFSZ)   |  M(SIGVTALRM) |  M(SIGPROF)   |  M(SIGPOLL)   | \
	M(SIGSYS)    |  M_SIGSTKFLT  |  M(SIGPWR)    |  M(SIGCONT)   | \
        M(SIGSTOP)   |  M(SIGTSTP)   |  M(SIGTTIN)   |  M(SIGTTOU)   | \
        M_SIGEMT )

#define SIG_KERNEL_ONLY_MASK (\
	M(SIGKILL)   |  M(SIGSTOP)                                   )

#define SIG_KERNEL_STOP_MASK (\
	M(SIGSTOP)   |  M(SIGTSTP)   |  M(SIGTTIN)   |  M(SIGTTOU)   )

#define SIG_KERNEL_COREDUMP_MASK (\
        M(SIGQUIT)   |  M(SIGILL)    |  M(SIGTRAP)   |  M(SIGABRT)   | \
        M(SIGFPE)    |  M(SIGSEGV)   |  M(SIGBUS)    |  M(SIGSYS)    | \
        M(SIGXCPU)   |  M(SIGXFSZ)   |  M_SIGEMT                     )

#define SIG_KERNEL_IGNORE_MASK (\
        M(SIGCONT)   |  M(SIGCHLD)   |  M(SIGWINCH)  |  M(SIGURG)    )

#define sig_kernel_only(sig) \
		(((sig) < SIGRTMIN)  && T(sig, SIG_KERNEL_ONLY_MASK))
#define sig_kernel_coredump(sig) \
		(((sig) < SIGRTMIN)  && T(sig, SIG_KERNEL_COREDUMP_MASK))
#define sig_kernel_ignore(sig) \
		(((sig) < SIGRTMIN)  && T(sig, SIG_KERNEL_IGNORE_MASK))
#define sig_kernel_stop(sig) \
		(((sig) < SIGRTMIN)  && T(sig, SIG_KERNEL_STOP_MASK))

#define sig_user_defined(t, signr) \
	(((t)->sighand->action[(signr)-1].sa.sa_handler != SIG_DFL) &&	\
	 ((t)->sighand->action[(signr)-1].sa.sa_handler != SIG_IGN))

#define sig_ignored(t, signr) \
	(!((t)->ptrace & PT_PTRACED) && \
	 (t)->sighand->action[(signr)-1].sa.sa_handler == SIG_IGN)

#define sig_fatal(t, signr) \
	(!T(signr, SIG_KERNEL_IGNORE_MASK|SIG_KERNEL_STOP_MASK) && \
	 (t)->sighand->action[(signr)-1].sa.sa_handler == SIG_DFL)

/*
 * Re-calculate pending state from the set of locally pending
 * signals, globally pending signals, and blocked signals.
 */
static inline int has_pending_signals(sigset_t *signal, sigset_t *blocked)
{
	unsigned long ready;
	long i;

	switch (_NSIG_WORDS) {
	default:
		for (i = _NSIG_WORDS, ready = 0; --i >= 0 ;)
			ready |= signal->sig[i] &~ blocked->sig[i];
		break;

	case 4: ready  = signal->sig[3] &~ blocked->sig[3];
		ready |= signal->sig[2] &~ blocked->sig[2];
		ready |= signal->sig[1] &~ blocked->sig[1];
		ready |= signal->sig[0] &~ blocked->sig[0];
		break;

	case 2: ready  = signal->sig[1] &~ blocked->sig[1];
		ready |= signal->sig[0] &~ blocked->sig[0];
		break;

	case 1: ready  = signal->sig[0] &~ blocked->sig[0];
	}
	return ready !=	0;
}

#define PENDING(p,b) has_pending_signals(&(p)->signal, (b))

inline void recalc_sigpending_tsk(struct task_struct *t)
{
	if (t->signal->group_stop_count > 0 ||
	    PENDING(&t->pending, &t->blocked) ||
	    PENDING(&t->signal->shared_pending, &t->blocked))
		set_tsk_thread_flag(t, TIF_SIGPENDING);
	else
		clear_tsk_thread_flag(t, TIF_SIGPENDING);
}

void recalc_sigpending(void)
{
	recalc_sigpending_tsk(current);
}

/* Given the mask, find the first available signal that should be serviced. */

static int
next_signal(struct sigpending *pending, sigset_t *mask)
{
	unsigned long i, *s, *m, x;
	int sig = 0;
	
	s = pending->signal.sig;
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
	
	return sig;
}

static void flush_sigqueue(struct sigpending *queue)
{
	struct sigqueue *q, *n;

	sigemptyset(&queue->signal);
	q = queue->head;
	queue->head = NULL;
	queue->tail = &queue->head;

	while (q) {
		n = q->next;
		kmem_cache_free(sigqueue_cachep, q);
		atomic_dec(&nr_queued_signals);
		q = n;
	}
}

/*
 * Flush all pending signals for a task.
 */

void
flush_signals(struct task_struct *t)
{
	clear_tsk_thread_flag(t,TIF_SIGPENDING);
	flush_sigqueue(&t->pending);
}

/*
 * This function expects the tasklist_lock write-locked.
 */
void __exit_sighand(struct task_struct *tsk)
{
	struct sighand_struct * sighand = tsk->sighand;

	/* Ok, we're done with the signal handlers */
	tsk->sighand = NULL;
	if (atomic_dec_and_test(&sighand->count))
		kmem_cache_free(sighand_cachep, sighand);
}

void exit_sighand(struct task_struct *tsk)
{
	write_lock_irq(&tasklist_lock);
	__exit_sighand(tsk);
	write_unlock_irq(&tasklist_lock);
}

/*
 * This function expects the tasklist_lock write-locked.
 */
void __exit_signal(struct task_struct *tsk)
{
	struct signal_struct * sig = tsk->signal;
	struct sighand_struct * sighand = tsk->sighand;

	if (!sig)
		BUG();
	if (!atomic_read(&sig->count))
		BUG();
	spin_lock(&sighand->siglock);
	if (atomic_dec_and_test(&sig->count)) {
		if (tsk == sig->curr_target)
			sig->curr_target = next_thread(tsk);
		tsk->signal = NULL;
		spin_unlock(&sighand->siglock);
		flush_sigqueue(&sig->shared_pending);
		kmem_cache_free(signal_cachep, sig);
	} else {
		/*
		 * If there is any task waiting for the group exit
		 * then notify it:
		 */
		if (sig->group_exit_task && atomic_read(&sig->count) <= 2) {
			wake_up_process(sig->group_exit_task);
			sig->group_exit_task = NULL;
		}
		if (tsk == sig->curr_target)
			sig->curr_target = next_thread(tsk);
		tsk->signal = NULL;
		spin_unlock(&sighand->siglock);
	}
	clear_tsk_thread_flag(tsk,TIF_SIGPENDING);
	flush_sigqueue(&tsk->pending);
}

void exit_signal(struct task_struct *tsk)
{
	write_lock_irq(&tasklist_lock);
	__exit_signal(tsk);
	write_unlock_irq(&tasklist_lock);
}

/*
 * Flush all handlers for a task.
 */

void
flush_signal_handlers(struct task_struct *t)
{
	int i;
	struct k_sigaction *ka = &t->sighand->action[0];
	for (i = _NSIG ; i != 0 ; i--) {
		if (ka->sa.sa_handler != SIG_IGN)
			ka->sa.sa_handler = SIG_DFL;
		ka->sa.sa_flags = 0;
		sigemptyset(&ka->sa.sa_mask);
		ka++;
	}
}


/* Notify the system that a driver wants to block all signals for this
 * process, and wants to be notified if any signals at all were to be
 * sent/acted upon.  If the notifier routine returns non-zero, then the
 * signal will be acted upon after all.  If the notifier routine returns 0,
 * then then signal will be blocked.  Only one block per process is
 * allowed.  priv is a pointer to private data that the notifier routine
 * can use to determine if the signal should be blocked or not.  */

void
block_all_signals(int (*notifier)(void *priv), void *priv, sigset_t *mask)
{
	unsigned long flags;

	spin_lock_irqsave(&current->sighand->siglock, flags);
	current->notifier_mask = mask;
	current->notifier_data = priv;
	current->notifier = notifier;
	spin_unlock_irqrestore(&current->sighand->siglock, flags);
}

/* Notify the system that blocking has ended. */

void
unblock_all_signals(void)
{
	unsigned long flags;

	spin_lock_irqsave(&current->sighand->siglock, flags);
	current->notifier = NULL;
	current->notifier_data = NULL;
	recalc_sigpending();
	spin_unlock_irqrestore(&current->sighand->siglock, flags);
}

static inline int collect_signal(int sig, struct sigpending *list, siginfo_t *info)
{
	if (sigismember(&list->signal, sig)) {
		/* Collect the siginfo appropriate to this signal.  */
		struct sigqueue *q, **pp;
		pp = &list->head;
		while ((q = *pp) != NULL) {
			if (q->info.si_signo == sig)
				goto found_it;
			pp = &q->next;
		}

		/* Ok, it wasn't in the queue.  This must be
		   a fast-pathed signal or we must have been
		   out of queue space.  So zero out the info.
		 */
		sigdelset(&list->signal, sig);
		info->si_signo = sig;
		info->si_errno = 0;
		info->si_code = 0;
		info->si_pid = 0;
		info->si_uid = 0;
		return 1;

found_it:
		if ((*pp = q->next) == NULL)
			list->tail = pp;

		/* Copy the sigqueue information and free the queue entry */
		copy_siginfo(info, &q->info);
		kmem_cache_free(sigqueue_cachep,q);
		atomic_dec(&nr_queued_signals);

		/* Non-RT signals can exist multiple times.. */
		if (sig >= SIGRTMIN) {
			while ((q = *pp) != NULL) {
				if (q->info.si_signo == sig)
					goto found_another;
				pp = &q->next;
			}
		}

		sigdelset(&list->signal, sig);
found_another:
		return 1;
	}
	return 0;
}

static int __dequeue_signal(struct sigpending *pending, sigset_t *mask,
			siginfo_t *info)
{
	int sig = 0;

	sig = next_signal(pending, mask);
	if (sig) {
		if (current->notifier) {
			if (sigismember(current->notifier_mask, sig)) {
				if (!(current->notifier)(current->notifier_data)) {
					clear_thread_flag(TIF_SIGPENDING);
					return 0;
				}
			}
		}

		if (!collect_signal(sig, pending, info))
			sig = 0;
				
		/* XXX: Once POSIX.1b timers are in, if si_code == SI_TIMER,
		   we need to xchg out the timer overrun values.  */
	}
	recalc_sigpending();

	return sig;
}

/*
 * Dequeue a signal and return the element to the caller, which is 
 * expected to free it.
 *
 * All callers have to hold the siglock.
 */
int dequeue_signal(sigset_t *mask, siginfo_t *info)
{
	int signr = __dequeue_signal(&current->pending, mask, info);
	if (!signr)
		signr = __dequeue_signal(&current->signal->shared_pending,
					 mask, info);
	return signr;
}

/*
 * Tell a process that it has a new active signal..
 *
 * NOTE! we rely on the previous spin_lock to
 * lock interrupts for us! We can only be called with
 * "siglock" held, and the local interrupt must
 * have been disabled when that got acquired!
 *
 * No need to set need_resched since signal event passing
 * goes through ->blocked
 */
inline void signal_wake_up(struct task_struct *t, int resume)
{
	unsigned int mask;

	set_tsk_thread_flag(t,TIF_SIGPENDING);

	/*
	 * If the task is running on a different CPU
	 * force a reschedule on the other CPU to make
	 * it notice the new signal quickly.
	 *
	 * The code below is a tad loose and might occasionally
	 * kick the wrong CPU if we catch the process in the
	 * process of changing - but no harm is done by that
	 * other than doing an extra (lightweight) IPI interrupt.
	 */
	if (t->state == TASK_RUNNING)
		kick_if_running(t);
	/*
	 * If resume is set, we want to wake it up in the TASK_STOPPED case.
	 * We don't check for TASK_STOPPED because there is a race with it
	 * executing another processor and just now entering stopped state.
	 * By calling wake_up_process any time resume is set, we ensure
	 * the process will wake up and handle its stop or death signal.
	 */
	mask = TASK_INTERRUPTIBLE;
	if (resume)
		mask |= TASK_STOPPED;
	if (t->state & mask) {
		wake_up_process(t);
		return;
	}
}

/*
 * Remove signals in mask from the pending set and queue.
 * Returns 1 if any signals were found.
 *
 * All callers must be holding the siglock.
 */
static int rm_from_queue(unsigned long mask, struct sigpending *s)
{
	struct sigqueue *q, **pp;

	if (!sigtestsetmask(&s->signal, mask))
		return 0;

	sigdelsetmask(&s->signal, mask);

	pp = &s->head;

	while ((q = *pp) != NULL) {
		if (q->info.si_signo < SIGRTMIN &&
		    (mask & sigmask (q->info.si_signo))) {
			if ((*pp = q->next) == NULL)
				s->tail = pp;
			kmem_cache_free(sigqueue_cachep,q);
			atomic_dec(&nr_queued_signals);
			continue;
		}
		pp = &q->next;
	}
	return 1;
}

/*
 * Bad permissions for sending the signal
 */
static inline int check_kill_permission(int sig, struct siginfo *info,
					struct task_struct *t)
{
	int error = -EINVAL;
	if (sig < 0 || sig > _NSIG)
		return error;
	error = -EPERM;
	if ((!info || ((unsigned long)info != 1 &&
			(unsigned long)info != 2 && SI_FROMUSER(info)))
	    && ((sig != SIGCONT) || (current->session != t->session))
	    && (current->euid ^ t->suid) && (current->euid ^ t->uid)
	    && (current->uid ^ t->suid) && (current->uid ^ t->uid)
	    && !capable(CAP_KILL))
		return error;
	return security_task_kill(t, info, sig);
}

/* forward decl */
static void do_notify_parent_cldstop(struct task_struct *tsk,
				     struct task_struct *parent);

/*
 * Handle magic process-wide effects of stop/continue signals, and SIGKILL.
 * Unlike the signal actions, these happen immediately at signal-generation
 * time regardless of blocking, ignoring, or handling.  This does the
 * actual continuing for SIGCONT, but not the actual stopping for stop
 * signals.  The process stop is done as a signal action for SIG_DFL.
 */
static void handle_stop_signal(int sig, struct task_struct *p)
{
	struct task_struct *t;

	if (sig_kernel_stop(sig)) {
		/*
		 * This is a stop signal.  Remove SIGCONT from all queues.
		 */
		rm_from_queue(sigmask(SIGCONT), &p->signal->shared_pending);
		t = p;
		do {
			rm_from_queue(sigmask(SIGCONT), &t->pending);
			t = next_thread(t);
		} while (t != p);
	} else if (sig == SIGCONT) {
		/*
		 * Remove all stop signals from all queues,
		 * and wake all threads.
		 */
		if (unlikely(p->signal->group_stop_count > 0)) {
			/*
			 * There was a group stop in progress.  We'll
			 * pretend it finished before we got here.  We are
			 * obliged to report it to the parent: if the
			 * SIGSTOP happened "after" this SIGCONT, then it
			 * would have cleared this pending SIGCONT.  If it
			 * happened "before" this SIGCONT, then the parent
			 * got the SIGCHLD about the stop finishing before
			 * the continue happened.  We do the notification
			 * now, and it's as if the stop had finished and
			 * the SIGCHLD was pending on entry to this kill.
			 */
			p->signal->group_stop_count = 0;
			if (p->ptrace & PT_PTRACED)
				do_notify_parent_cldstop(p, p->parent);
			else
				do_notify_parent_cldstop(
					p->group_leader,
					p->group_leader->real_parent);
		}
		rm_from_queue(SIG_KERNEL_STOP_MASK, &p->signal->shared_pending);
		t = p;
		do {
			rm_from_queue(SIG_KERNEL_STOP_MASK, &t->pending);
			/*
			 * This wakeup is only need if in TASK_STOPPED,
			 * but there can be SMP races with testing for that.
			 * In the normal SIGCONT case, all will be stopped.
			 * A spuriously sent SIGCONT will interrupt all running
			 * threads to check signals even if it's ignored.
			 *
			 * If there is a handler for SIGCONT, we must make
			 * sure that no thread returns to user mode before
			 * we post the signal, in case it was the only
			 * thread eligible to run the signal handler--then
			 * it must not do anything between resuming and
			 * running the handler.  With the TIF_SIGPENDING
			 * flag set, the thread will pause and acquire the
			 * siglock that we hold now and until we've queued
			 * the pending signal. 
			 */
			if (!(t->flags & PF_EXITING)) {
				if (!sigismember(&t->blocked, SIGCONT))
					set_tsk_thread_flag(t, TIF_SIGPENDING);
				wake_up_process(t);
			}
			t = next_thread(t);
		} while (t != p);
	}
}

static int send_signal(int sig, struct siginfo *info, struct sigpending *signals)
{
	struct sigqueue * q = NULL;

	/*
	 * fast-pathed signals for kernel-internal things like SIGSTOP
	 * or SIGKILL.
	 */
	if ((unsigned long)info == 2)
		goto out_set;

	/* Real-time signals must be queued if sent by sigqueue, or
	   some other real-time mechanism.  It is implementation
	   defined whether kill() does so.  We attempt to do so, on
	   the principle of least surprise, but since kill is not
	   allowed to fail with EAGAIN when low on memory we just
	   make sure at least one signal gets delivered and don't
	   pass on the info struct.  */

	if (atomic_read(&nr_queued_signals) < max_queued_signals)
		q = kmem_cache_alloc(sigqueue_cachep, GFP_ATOMIC);

	if (q) {
		atomic_inc(&nr_queued_signals);
		q->next = NULL;
		*signals->tail = q;
		signals->tail = &q->next;
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
			copy_siginfo(&q->info, info);
			break;
		}
	} else if (sig >= SIGRTMIN && info && (unsigned long)info != 1
		   && info->si_code != SI_USER)
		/*
		 * Queue overflow, abort.  We may abort if the signal was rt
		 * and sent by user using something other than kill().
		 */
		return -EAGAIN;

out_set:
	sigaddset(&signals->signal, sig);
	return 0;
}

#define LEGACY_QUEUE(sigptr, sig) \
	(((sig) < SIGRTMIN) && sigismember(&(sigptr)->signal, (sig)))


static int
specific_send_sig_info(int sig, struct siginfo *info, struct task_struct *t)
{
	int ret;

	if (!irqs_disabled())
		BUG();
#if CONFIG_SMP
	if (!spin_is_locked(&t->sighand->siglock))
		BUG();
#endif

	/* Short-circuit ignored signals.  */
	if (sig_ignored(t, sig))
		return 0;

	/* Support queueing exactly one non-rt signal, so that we
	   can get more detailed information about the cause of
	   the signal. */
	if (LEGACY_QUEUE(&t->pending, sig))
		return 0;

	ret = send_signal(sig, info, &t->pending);
	if (!ret && !sigismember(&t->blocked, sig))
		signal_wake_up(t, sig == SIGKILL);

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
	int ret;

	spin_lock_irqsave(&t->sighand->siglock, flags);
	if (t->sighand->action[sig-1].sa.sa_handler == SIG_IGN)
		t->sighand->action[sig-1].sa.sa_handler = SIG_DFL;
	sigdelset(&t->blocked, sig);
	recalc_sigpending_tsk(t);
	ret = specific_send_sig_info(sig, info, t);
	spin_unlock_irqrestore(&t->sighand->siglock, flags);

	return ret;
}

void
force_sig_specific(int sig, struct task_struct *t)
{
	unsigned long int flags;

	spin_lock_irqsave(&t->sighand->siglock, flags);
	if (t->sighand->action[sig-1].sa.sa_handler == SIG_IGN)
		t->sighand->action[sig-1].sa.sa_handler = SIG_DFL;
	sigdelset(&t->blocked, sig);
	recalc_sigpending_tsk(t);
	specific_send_sig_info(sig, (void *)2, t);
	spin_unlock_irqrestore(&t->sighand->siglock, flags);
}

/*
 * Test if P wants to take SIG.  After we've checked all threads with this,
 * it's equivalent to finding no threads not blocking SIG.  Any threads not
 * blocking SIG were ruled out because they are not running and already
 * have pending signals.  Such threads will dequeue from the shared queue
 * as soon as they're available, so putting the signal on the shared queue
 * will be equivalent to sending it to one such thread.
 */
#define wants_signal(sig, p, mask) 			\
	(!sigismember(&(p)->blocked, sig)		\
	 && !((p)->state & mask)			\
	 && !((p)->flags & PF_EXITING)			\
	 && (task_curr(p) || !signal_pending(p)))

static inline int
__group_send_sig_info(int sig, struct siginfo *info, struct task_struct *p)
{
	struct task_struct *t;
	unsigned int mask;
	int ret;

#if CONFIG_SMP
	if (!spin_is_locked(&p->sighand->siglock))
		BUG();
#endif
	handle_stop_signal(sig, p);

	/* Short-circuit ignored signals.  */
	if (sig_ignored(p, sig))
		return 0;

	if (LEGACY_QUEUE(&p->signal->shared_pending, sig))
		/* This is a non-RT signal and we already have one queued.  */
		return 0;

	/*
	 * Don't bother zombies and stopped tasks (but
	 * SIGKILL will punch through stopped state)
	 */
	mask = TASK_DEAD | TASK_ZOMBIE;
	if (sig != SIGKILL)
		mask |= TASK_STOPPED;

	/*
	 * Put this signal on the shared-pending queue, or fail with EAGAIN.
	 * We always use the shared queue for process-wide signals,
	 * to avoid several races.
	 */
	ret = send_signal(sig, info, &p->signal->shared_pending);
	if (unlikely(ret))
		return ret;

	/*
	 * Now find a thread we can wake up to take the signal off the queue.
	 *
	 * If the main thread wants the signal, it gets first crack.
	 * Probably the least surprising to the average bear.
	 */
	if (wants_signal(sig, p, mask))
		t = p;
	else if (thread_group_empty(p))
		/*
		 * There is just one thread and it does not need to be woken.
		 * It will dequeue unblocked signals before it runs again.
		 */
		return 0;
	else {
		/*
		 * Otherwise try to find a suitable thread.
		 */
		t = p->signal->curr_target;
		if (t == NULL)
			/* restart balancing at this thread */
			t = p->signal->curr_target = p;
		BUG_ON(t->tgid != p->tgid);

		while (!wants_signal(sig, t, mask)) {
			t = next_thread(t);
			if (t == p->signal->curr_target)
				/*
				 * No thread needs to be woken.
				 * Any eligible threads will see
				 * the signal in the queue soon.
				 */
				return 0;
		}
		p->signal->curr_target = t;
	}

	/*
	 * Found a killable thread.  If the signal will be fatal,
	 * then start taking the whole group down immediately.
	 */
	if (sig_fatal(p, sig) && !p->signal->group_exit &&
	    !sigismember(&t->real_blocked, sig) &&
	    (sig == SIGKILL || !(t->ptrace & PT_PTRACED))) {
		/*
		 * This signal will be fatal to the whole group.
		 */
		if (!sig_kernel_coredump(sig)) {
			/*
			 * Start a group exit and wake everybody up.
			 * This way we don't have other threads
			 * running and doing things after a slower
			 * thread has the fatal signal pending.
			 */
			p->signal->group_exit = 1;
			p->signal->group_exit_code = sig;
			p->signal->group_stop_count = 0;
			t = p;
			do {
				sigaddset(&t->pending.signal, SIGKILL);
				signal_wake_up(t, 1);
				t = next_thread(t);
			} while (t != p);
			return 0;
		}

		/*
		 * There will be a core dump.  We make all threads other
		 * than the chosen one go into a group stop so that nothing
		 * happens until it gets scheduled, takes the signal off
		 * the shared queue, and does the core dump.  This is a
		 * little more complicated than strictly necessary, but it
		 * keeps the signal state that winds up in the core dump
		 * unchanged from the death state, e.g. which thread had
		 * the core-dump signal unblocked.
		 */
		rm_from_queue(SIG_KERNEL_STOP_MASK, &t->pending);
		rm_from_queue(SIG_KERNEL_STOP_MASK, &p->signal->shared_pending);
		p->signal->group_stop_count = 0;
		p->signal->group_exit_task = t;
		t = p;
		do {
			p->signal->group_stop_count++;
			signal_wake_up(t, 0);
			t = next_thread(t);
		} while (t != p);
		wake_up_process(p->signal->group_exit_task);
		return 0;
	}

	/*
	 * The signal is already in the shared-pending queue.
	 * Tell the chosen thread to wake up and dequeue it.
	 */
	signal_wake_up(t, sig == SIGKILL);
	return 0;
}

/*
 * Nuke all other threads in the group.
 */
void zap_other_threads(struct task_struct *p)
{
	struct task_struct *t;

	p->signal->group_stop_count = 0;

	if (thread_group_empty(p))
		return;

	for (t = next_thread(p); t != p; t = next_thread(t)) {
		sigaddset(&t->pending.signal, SIGKILL);
		rm_from_queue(SIG_KERNEL_STOP_MASK, &t->pending);
		signal_wake_up(t, 1);
	}
}

int
group_send_sig_info(int sig, struct siginfo *info, struct task_struct *p)
{
	unsigned long flags;
	int ret;

	ret = check_kill_permission(sig, info, p);
	if (!ret && sig && p->sighand) {
		spin_lock_irqsave(&p->sighand->siglock, flags);
		ret = __group_send_sig_info(sig, info, p);
		spin_unlock_irqrestore(&p->sighand->siglock, flags);
	}

	return ret;
}

/*
 * kill_pg_info() sends a signal to a process group: this is what the tty
 * control characters do (^C, ^Z etc)
 */

int __kill_pg_info(int sig, struct siginfo *info, pid_t pgrp)
{
	struct task_struct *p;
	struct list_head *l;
	struct pid *pid;
	int err, retval = -ESRCH;

	if (pgrp <= 0)
		return -EINVAL;

	for_each_task_pid(pgrp, PIDTYPE_PGID, p, l, pid) {
		err = group_send_sig_info(sig, info, p);
		if (retval)
			retval = err;
	}
	return retval;
}

int
kill_pg_info(int sig, struct siginfo *info, pid_t pgrp)
{
	int retval;

	read_lock(&tasklist_lock);
	retval = __kill_pg_info(sig, info, pgrp);
	read_unlock(&tasklist_lock);

	return retval;
}

/*
 * kill_sl_info() sends a signal to the session leader: this is used
 * to send SIGHUP to the controlling process of a terminal when
 * the connection is lost.
 */


int
kill_sl_info(int sig, struct siginfo *info, pid_t sid)
{
	int err, retval = -EINVAL;
	struct pid *pid;
	struct list_head *l;
	struct task_struct *p;

	if (sid <= 0)
		goto out;

	retval = -ESRCH;
	read_lock(&tasklist_lock);
	for_each_task_pid(sid, PIDTYPE_SID, p, l, pid) {
		if (!p->leader)
			continue;
		err = group_send_sig_info(sig, info, p);
		if (retval)
			retval = err;
	}
	read_unlock(&tasklist_lock);
out:
	return retval;
}

int
kill_proc_info(int sig, struct siginfo *info, pid_t pid)
{
	int error;
	struct task_struct *p;

	read_lock(&tasklist_lock);
	p = find_task_by_pid(pid);
	error = -ESRCH;
	if (p)
		error = group_send_sig_info(sig, info, p);
	read_unlock(&tasklist_lock);
	return error;
}


/*
 * kill_something_info() interprets pid in interesting ways just like kill(2).
 *
 * POSIX specifies that kill(-1,sig) is unspecified, but what we have
 * is probably wrong.  Should make it like BSD or SYSV.
 */

static int kill_something_info(int sig, struct siginfo *info, int pid)
{
	if (!pid) {
		return kill_pg_info(sig, info, current->pgrp);
	} else if (pid == -1) {
		int retval = 0, count = 0;
		struct task_struct * p;

		read_lock(&tasklist_lock);
		for_each_process(p) {
			if (p->pid > 1 && p->tgid != current->tgid) {
				int err = group_send_sig_info(sig, info, p);
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
send_sig_info(int sig, struct siginfo *info, struct task_struct *p)
{
	/* XXX should nix these interfaces and update the kernel */
	if (T(sig, SIG_KERNEL_BROADCAST_MASK))
		/* XXX do callers really always hold the tasklist_lock?? */
		return group_send_sig_info(sig, info, p);
	else {
		int error;
		spin_lock_irq(&p->sighand->siglock);
		error = specific_send_sig_info(sig, info, p);
		spin_unlock_irq(&p->sighand->siglock);
		return error;
	}
}

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
 * Joy. Or not. Pthread wants us to wake up every thread
 * in our parent group.
 */
static inline void __wake_up_parent(struct task_struct *p,
				    struct task_struct *parent)
{
	struct task_struct *tsk = parent;

	/*
	 * Fortunately this is not necessary for thread groups:
	 */
	if (p->tgid == tsk->tgid) {
		wake_up_interruptible(&tsk->wait_chldexit);
		return;
	}

	do {
		wake_up_interruptible(&tsk->wait_chldexit);
		tsk = next_thread(tsk);
		if (tsk->signal != parent->signal)
			BUG();
	} while (tsk != parent);
}

/*
 * Let a parent know about a status change of a child.
 */

void do_notify_parent(struct task_struct *tsk, int sig)
{
	struct siginfo info;
	unsigned long flags;
	int why, status;
	struct sighand_struct *psig;

	if (sig == -1)
		BUG();

	info.si_signo = sig;
	info.si_errno = 0;
	info.si_pid = tsk->pid;
	info.si_uid = tsk->uid;

	/* FIXME: find out whether or not this is supposed to be c*time. */
	info.si_utime = tsk->utime;
	info.si_stime = tsk->stime;

	status = tsk->exit_code & 0x7f;
	why = SI_KERNEL;	/* shouldn't happen */
	switch (tsk->state) {
	case TASK_STOPPED:
		/* FIXME -- can we deduce CLD_TRAPPED or CLD_CONTINUED? */
		if (tsk->ptrace & PT_PTRACED)
			why = CLD_TRAPPED;
		else
			why = CLD_STOPPED;
		break;

	default:
		if (tsk->exit_code & 0x80)
			why = CLD_DUMPED;
		else if (tsk->exit_code & 0x7f)
			why = CLD_KILLED;
		else {
			why = CLD_EXITED;
			status = tsk->exit_code >> 8;
		}
		break;
	}
	info.si_code = why;
	info.si_status = status;

	psig = tsk->parent->sighand;
	spin_lock_irqsave(&psig->siglock, flags);
	if (sig == SIGCHLD && tsk->state != TASK_STOPPED &&
	    (psig->action[SIGCHLD-1].sa.sa_handler == SIG_IGN ||
	     (psig->action[SIGCHLD-1].sa.sa_flags & SA_NOCLDWAIT))) {
		/*
		 * We are exiting and our parent doesn't care.  POSIX.1
		 * defines special semantics for setting SIGCHLD to SIG_IGN
		 * or setting the SA_NOCLDWAIT flag: we should be reaped
		 * automatically and not left for our parent's wait4 call.
		 * Rather than having the parent do it as a magic kind of
		 * signal handler, we just set this to tell do_exit that we
		 * can be cleaned up without becoming a zombie.  Note that
		 * we still call __wake_up_parent in this case, because a
		 * blocked sys_wait4 might now return -ECHILD.
		 *
		 * Whether we send SIGCHLD or not for SA_NOCLDWAIT
		 * is implementation-defined: we do (if you don't want
		 * it, just use SIG_IGN instead).
		 */
		tsk->exit_signal = -1;
		if (psig->action[SIGCHLD-1].sa.sa_handler == SIG_IGN)
			sig = 0;
	}
	if (sig > 0 && sig <= _NSIG)
		__group_send_sig_info(sig, &info, tsk->parent);
	__wake_up_parent(tsk, tsk->parent);
	spin_unlock_irqrestore(&psig->siglock, flags);
}


/*
 * We need the tasklist lock because it's the only
 * thing that protects out "parent" pointer.
 *
 * exit.c calls "do_notify_parent()" directly, because
 * it already has the tasklist lock.
 */
void
notify_parent(struct task_struct *tsk, int sig)
{
	if (sig != -1) {
		read_lock(&tasklist_lock);
		do_notify_parent(tsk, sig);
		read_unlock(&tasklist_lock);
	}
}

static void
do_notify_parent_cldstop(struct task_struct *tsk, struct task_struct *parent)
{
	struct siginfo info;
	unsigned long flags;
	struct sighand_struct *sighand;

	info.si_signo = SIGCHLD;
	info.si_errno = 0;
	info.si_pid = tsk->pid;
	info.si_uid = tsk->uid;

	/* FIXME: find out whether or not this is supposed to be c*time. */
	info.si_utime = tsk->utime;
	info.si_stime = tsk->stime;

	info.si_status = tsk->exit_code & 0x7f;
	info.si_code = CLD_STOPPED;

	sighand = parent->sighand;
	spin_lock_irqsave(&sighand->siglock, flags);
	if (sighand->action[SIGCHLD-1].sa.sa_handler != SIG_IGN &&
	    !(sighand->action[SIGCHLD-1].sa.sa_flags & SA_NOCLDSTOP))
		__group_send_sig_info(SIGCHLD, &info, parent);
	/*
	 * Even if SIGCHLD is not generated, we must wake up wait4 calls.
	 */
	__wake_up_parent(tsk, parent);
	spin_unlock_irqrestore(&sighand->siglock, flags);
}

static void
finish_stop(int stop_count)
{
	/*
	 * If there are no other threads in the group, or if there is
	 * a group stop in progress and we are the last to stop,
	 * report to the parent.  When ptraced, every thread reports itself.
	 */
	if (stop_count < 0 || (current->ptrace & PT_PTRACED)) {
		read_lock(&tasklist_lock);
		do_notify_parent_cldstop(current, current->parent);
		read_unlock(&tasklist_lock);
	}
	else if (stop_count == 0) {
		read_lock(&tasklist_lock);
		do_notify_parent_cldstop(current->group_leader,
					 current->group_leader->real_parent);
		read_unlock(&tasklist_lock);
	}

	schedule();
	/*
	 * Now we don't run again until continued.
	 */
	current->exit_code = 0;
}

/*
 * This performs the stopping for SIGSTOP and other stop signals.
 * We have to stop all threads in the thread group.
 */
static void
do_signal_stop(int signr)
{
	struct signal_struct *sig = current->signal;
	struct sighand_struct *sighand = current->sighand;
	int stop_count = -1;

	if (sig->group_stop_count > 0) {
		/*
		 * There is a group stop in progress.  We don't need to
		 * start another one.
		 */
		spin_lock_irq(&sighand->siglock);
		if (unlikely(sig->group_stop_count == 0)) {
			BUG_ON(!sig->group_exit);
			spin_unlock_irq(&sighand->siglock);
			return;
		}
		signr = sig->group_exit_code;
		stop_count = --sig->group_stop_count;
		current->exit_code = signr;
		set_current_state(TASK_STOPPED);
		spin_unlock_irq(&sighand->siglock);
	}
	else if (thread_group_empty(current)) {
		/*
		 * No locks needed in this case.
		 */
		current->exit_code = signr;
		set_current_state(TASK_STOPPED);
	}
	else {
		/*
		 * There is no group stop already in progress.
		 * We must initiate one now.
		 */
		struct task_struct *t;
		read_lock(&tasklist_lock);
		spin_lock_irq(&sighand->siglock);

		if (unlikely(sig->group_exit)) {
			/*
			 * There is a group exit in progress now.
			 * We'll just ignore the stop and process the
			 * associated fatal signal.
			 */
			spin_unlock_irq(&sighand->siglock);
			read_unlock(&tasklist_lock);
			return;
		}

		if (sig->group_stop_count == 0) {
			sig->group_exit_code = signr;
			stop_count = 0;
			for (t = next_thread(current); t != current;
			     t = next_thread(t))
				/*
				 * Setting state to TASK_STOPPED for a group
				 * stop is always done with the siglock held,
				 * so this check has no races.
				 */
				if (t->state < TASK_STOPPED) {
					stop_count++;
					signal_wake_up(t, 0);
				}
			sig->group_stop_count = stop_count;
		}
		else {
			/* A race with another thread while unlocked.  */
			signr = sig->group_exit_code;
			stop_count = --sig->group_stop_count;
		}

		current->exit_code = signr;
		set_current_state(TASK_STOPPED);

		spin_unlock_irq(&sighand->siglock);
		read_unlock(&tasklist_lock);
	}

	finish_stop(stop_count);
}


#ifndef HAVE_ARCH_GET_SIGNAL_TO_DELIVER

int get_signal_to_deliver(siginfo_t *info, struct pt_regs *regs)
{
	sigset_t *mask = &current->blocked;

	for (;;) {
		unsigned long signr = 0;
		struct k_sigaction *ka;

		spin_lock_irq(&current->sighand->siglock);
		if (unlikely(current->signal->group_stop_count > 0)) {
			int stop_count;
			if (current->signal->group_exit_task == current) {
				/*
				 * Group stop is so we can do a core dump.
				 */
				current->signal->group_exit_task = NULL;
				goto dequeue;
			}
			/*
			 * There is a group stop in progress.  We stop
			 * without any associated signal being in our queue.
			 */
			stop_count = --current->signal->group_stop_count;
			signr = current->signal->group_exit_code;
			current->exit_code = signr;
			set_current_state(TASK_STOPPED);
			spin_unlock_irq(&current->sighand->siglock);
			finish_stop(stop_count);
			continue;
		}
	dequeue:
		signr = dequeue_signal(mask, info);
		spin_unlock_irq(&current->sighand->siglock);

		if (!signr)
			break;

		if ((current->ptrace & PT_PTRACED) && signr != SIGKILL) {
			/*
			 * If there is a group stop in progress,
			 * we must participate in the bookkeeping.
			 */
			if (current->signal->group_stop_count > 0) {
				spin_lock_irq(&current->sighand->siglock);
				--current->signal->group_stop_count;
				spin_unlock_irq(&current->sighand->siglock);
			}

			/* Let the debugger run.  */
			current->exit_code = signr;
			current->last_siginfo = info;
			set_current_state(TASK_STOPPED);
			notify_parent(current, SIGCHLD);
			schedule();

			current->last_siginfo = NULL;

			/* We're back.  Did the debugger cancel the sig?  */
			signr = current->exit_code;
			if (signr == 0)
				continue;
			current->exit_code = 0;

			/* Update the siginfo structure if the signal has
			   changed.  If the debugger wanted something
			   specific in the siginfo structure then it should
			   have updated *info via PTRACE_SETSIGINFO.  */
			if (signr != info->si_signo) {
				info->si_signo = signr;
				info->si_errno = 0;
				info->si_code = SI_USER;
				info->si_pid = current->parent->pid;
				info->si_uid = current->parent->uid;
			}

			/* If the (new) signal is now blocked, requeue it.  */
			if (sigismember(&current->blocked, signr)) {
				spin_lock_irq(&current->sighand->siglock);
				specific_send_sig_info(signr, info, current);
				spin_unlock_irq(&current->sighand->siglock);
				continue;
			}
		}

		ka = &current->sighand->action[signr-1];
		if (ka->sa.sa_handler == SIG_IGN) /* Do nothing.  */
			continue;
		if (ka->sa.sa_handler != SIG_DFL) /* Run the handler.  */
			return signr;

		/*
		 * Now we are doing the default action for this signal.
		 */
		if (sig_kernel_ignore(signr)) /* Default is nothing. */
			continue;

		/* Init gets no signals it doesn't want.  */
		if (current->pid == 1)
			continue;

		if (sig_kernel_stop(signr)) {
			/*
			 * The default action is to stop all threads in
			 * the thread group.  The job control signals
			 * do nothing in an orphaned pgrp, but SIGSTOP
			 * always works.
			 */
			if (signr == SIGSTOP ||
			    !is_orphaned_pgrp(current->pgrp))
				do_signal_stop(signr);
			continue;
		}

		/*
		 * Anything else is fatal, maybe with a core dump.
		 */
		current->flags |= PF_SIGNALED;
		if (sig_kernel_coredump(signr) &&
		    do_coredump(signr, signr, regs)) {
			/*
			 * That killed all other threads in the group and
			 * synchronized with their demise, so there can't
			 * be any more left to kill now.  The group_exit
			 * flags are set by do_coredump.  Note that
			 * thread_group_empty won't always be true yet,
			 * because those threads were blocked in __exit_mm
			 * and we just let them go to finish dying.
			 */
			const int code = signr | 0x80;
			BUG_ON(!current->signal->group_exit);
			BUG_ON(current->signal->group_exit_code != code);
			do_exit(code);
				/* NOTREACHED */
			}

		/*
		 * Death signals, no core dump.
		 */
		do_group_exit(signr);
		/* NOTREACHED */
	}
	return 0;
}

#endif

EXPORT_SYMBOL(recalc_sigpending);
EXPORT_SYMBOL_GPL(dequeue_signal);
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
EXPORT_SYMBOL(send_sig);
EXPORT_SYMBOL(send_sig_info);
EXPORT_SYMBOL(block_all_signals);
EXPORT_SYMBOL(unblock_all_signals);


/*
 * System call entry points.
 */

asmlinkage long sys_restart_syscall(void)
{
	struct restart_block *restart = &current_thread_info()->restart_block;
	return restart->fn(restart);
}

long do_no_restart_syscall(struct restart_block *param)
{
	return -EINTR;
}


/*
 * We don't need to get the kernel lock - this is all local to this
 * particular thread.. (and that's good, because this is _heavily_
 * used by various programs)
 */

asmlinkage long
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

		spin_lock_irq(&current->sighand->siglock);
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
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
		if (error)
			goto out;
		if (oset)
			goto set_old;
	} else if (oset) {
		spin_lock_irq(&current->sighand->siglock);
		old_set = current->blocked;
		spin_unlock_irq(&current->sighand->siglock);

	set_old:
		error = -EFAULT;
		if (copy_to_user(oset, &old_set, sizeof(*oset)))
			goto out;
	}
	error = 0;
out:
	return error;
}

long do_sigpending(void *set, unsigned long sigsetsize)
{
	long error = -EINVAL;
	sigset_t pending;

	if (sigsetsize > sizeof(sigset_t))
		goto out;

	spin_lock_irq(&current->sighand->siglock);
	sigorsets(&pending, &current->pending.signal,
		  &current->signal->shared_pending.signal);
	spin_unlock_irq(&current->sighand->siglock);

	/* Outside the lock because only this thread touches it.  */
	sigandsets(&pending, &current->blocked, &pending);

	error = -EFAULT;
	if (!copy_to_user(set, &pending, sigsetsize))
		error = 0;

out:
	return error;
}	

asmlinkage long
sys_rt_sigpending(sigset_t *set, size_t sigsetsize)
{
	return do_sigpending(set, sigsetsize);
}

#ifndef HAVE_ARCH_COPY_SIGINFO_TO_USER

int copy_siginfo_to_user(siginfo_t *to, siginfo_t *from)
{
	int err;

	if (!access_ok (VERIFY_WRITE, to, sizeof(siginfo_t)))
		return -EFAULT;
	if (from->si_code < 0)
		return __copy_to_user(to, from, sizeof(siginfo_t))
			? -EFAULT : 0;
	/*
	 * If you change siginfo_t structure, please be sure
	 * this code is fixed accordingly.
	 * It should never copy any pad contained in the structure
	 * to avoid security leaks, but must copy the generic
	 * 3 ints plus the relevant union member.
	 */
	err = __put_user(from->si_signo, &to->si_signo);
	err |= __put_user(from->si_errno, &to->si_errno);
	err |= __put_user((short)from->si_code, &to->si_code);
	switch (from->si_code & __SI_MASK) {
	case __SI_KILL:
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		break;
	case __SI_TIMER:
		err |= __put_user(from->si_timer1, &to->si_timer1);
		err |= __put_user(from->si_timer2, &to->si_timer2);
		break;
	case __SI_POLL:
		err |= __put_user(from->si_band, &to->si_band);
		err |= __put_user(from->si_fd, &to->si_fd);
		break;
	case __SI_FAULT:
		err |= __put_user(from->si_addr, &to->si_addr);
#ifdef __ARCH_SI_TRAPNO
		err |= __put_user(from->si_trapno, &to->si_trapno);
#endif
		break;
	case __SI_CHLD:
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		err |= __put_user(from->si_status, &to->si_status);
		err |= __put_user(from->si_utime, &to->si_utime);
		err |= __put_user(from->si_stime, &to->si_stime);
		break;
	case __SI_RT: /* This is not generated by the kernel as of now. */
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		err |= __put_user(from->si_int, &to->si_int);
		err |= __put_user(from->si_ptr, &to->si_ptr);
		break;
	default: /* this is just in case for now ... */
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		break;
	}
	return err;
}

#endif

asmlinkage long
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
		
	/*
	 * Invert the set of allowed signals to get those we
	 * want to block.
	 */
	sigdelsetmask(&these, sigmask(SIGKILL)|sigmask(SIGSTOP));
	signotset(&these);

	if (uts) {
		if (copy_from_user(&ts, uts, sizeof(ts)))
			return -EFAULT;
		if (ts.tv_nsec >= 1000000000L || ts.tv_nsec < 0
		    || ts.tv_sec < 0)
			return -EINVAL;
	}

	spin_lock_irq(&current->sighand->siglock);
	sig = dequeue_signal(&these, &info);
	if (!sig) {
		timeout = MAX_SCHEDULE_TIMEOUT;
		if (uts)
			timeout = (timespec_to_jiffies(&ts)
				   + (ts.tv_sec || ts.tv_nsec));

		if (timeout) {
			/* None ready -- temporarily unblock those we're
			 * interested while we are sleeping in so that we'll
			 * be awakened when they arrive.  */
			current->real_blocked = current->blocked;
			sigandsets(&current->blocked, &current->blocked, &these);
			recalc_sigpending();
			spin_unlock_irq(&current->sighand->siglock);

			current->state = TASK_INTERRUPTIBLE;
			timeout = schedule_timeout(timeout);

			spin_lock_irq(&current->sighand->siglock);
			sig = dequeue_signal(&these, &info);
			current->blocked = current->real_blocked;
			siginitset(&current->real_blocked, 0);
			recalc_sigpending();
		}
	}
	spin_unlock_irq(&current->sighand->siglock);

	if (sig) {
		ret = sig;
		if (uinfo) {
			if (copy_siginfo_to_user(uinfo, &info))
				ret = -EFAULT;
		}
	} else {
		ret = -EAGAIN;
		if (timeout)
			ret = -EINTR;
	}

	return ret;
}

asmlinkage long
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

/*
 *  Send a signal to only one task, even if it's a CLONE_THREAD task.
 */
asmlinkage long
sys_tkill(int pid, int sig)
{
	struct siginfo info;
	int error;
	struct task_struct *p;

	/* This is only valid for single tasks */
	if (pid <= 0)
		return -EINVAL;

	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = SI_TKILL;
	info.si_pid = current->pid;
	info.si_uid = current->uid;

	read_lock(&tasklist_lock);
	p = find_task_by_pid(pid);
	error = -ESRCH;
	if (p) {
		error = check_kill_permission(sig, &info, p);
		/*
		 * The null signal is a permissions and process existence
		 * probe.  No signal is actually delivered.
		 */
		if (!error && sig && p->sighand) {
			spin_lock_irq(&p->sighand->siglock);
			handle_stop_signal(sig, p);
			error = specific_send_sig_info(sig, &info, p);
			spin_unlock_irq(&p->sighand->siglock);
		}
	}
	read_unlock(&tasklist_lock);
	return error;
}

asmlinkage long
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

	if (sig < 1 || sig > _NSIG || (act && sig_kernel_only(sig)))
		return -EINVAL;

	k = &current->sighand->action[sig-1];

	spin_lock_irq(&current->sighand->siglock);
	if (signal_pending(current)) {
		/*
		 * If there might be a fatal signal pending on multiple
		 * threads, make sure we take it before changing the action.
		 */
		spin_unlock_irq(&current->sighand->siglock);
		return -ERESTARTNOINTR;
	}

	if (oact)
		*oact = *k;

	if (act) {
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
		 */
		if (act->sa.sa_handler == SIG_IGN ||
		    (act->sa.sa_handler == SIG_DFL &&
		     sig_kernel_ignore(sig))) {
			/*
			 * This is a fairly rare case, so we only take the
			 * tasklist_lock once we're sure we'll need it.
			 * Now we must do this little unlock and relock
			 * dance to maintain the lock hierarchy.
			 */
			struct task_struct *t = current;
			spin_unlock_irq(&t->sighand->siglock);
			read_lock(&tasklist_lock);
			spin_lock_irq(&t->sighand->siglock);
			*k = *act;
			sigdelsetmask(&k->sa.sa_mask,
				      sigmask(SIGKILL) | sigmask(SIGSTOP));
			rm_from_queue(sigmask(sig), &t->signal->shared_pending);
			do {
				rm_from_queue(sigmask(sig), &t->pending);
				recalc_sigpending_tsk(t);
				t = next_thread(t);
			} while (t != current);
			spin_unlock_irq(&current->sighand->siglock);
			read_unlock(&tasklist_lock);
			return 0;
		}

		*k = *act;
		sigdelsetmask(&k->sa.sa_mask,
			      sigmask(SIGKILL) | sigmask(SIGSTOP));
	}

	spin_unlock_irq(&current->sighand->siglock);
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
		if (on_sig_stack(sp))
			goto out;

		error = -EINVAL;
		/*
		 *
		 * Note - this code used to test ss_flags incorrectly
		 *  	  old code may have been written using ss_flags==0
		 *	  to mean ss_flags==SS_ONSTACK (as this was the only
		 *	  way that worked) - this fix preserves that older
		 *	  mechanism
		 */
		if (ss_flags != SS_DISABLE && ss_flags != SS_ONSTACK && ss_flags != 0)
			goto out;

		if (ss_flags == SS_DISABLE) {
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

asmlinkage long
sys_sigpending(old_sigset_t *set)
{
	return do_sigpending(set, sizeof(*set));
}

#if !defined(__alpha__)
/* Alpha has its own versions with special arguments.  */

asmlinkage long
sys_sigprocmask(int how, old_sigset_t *set, old_sigset_t *oset)
{
	int error;
	old_sigset_t old_set, new_set;

	if (set) {
		error = -EFAULT;
		if (copy_from_user(&new_set, set, sizeof(*set)))
			goto out;
		new_set &= ~(sigmask(SIGKILL) | sigmask(SIGSTOP));

		spin_lock_irq(&current->sighand->siglock);
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

		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
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

#ifndef __sparc__
asmlinkage long
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

#if !defined(__alpha__) && !defined(__ia64__) && !defined(__arm__)
/*
 * For backwards compatibility.  Functionality superseded by sigprocmask.
 */
asmlinkage long
sys_sgetmask(void)
{
	/* SMP safe */
	return current->blocked.sig[0];
}

asmlinkage long
sys_ssetmask(int newmask)
{
	int old;

	spin_lock_irq(&current->sighand->siglock);
	old = current->blocked.sig[0];

	siginitset(&current->blocked, newmask & ~(sigmask(SIGKILL)|
						  sigmask(SIGSTOP)));
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	return old;
}
#endif /* !defined(__alpha__) */

#if !defined(__alpha__) && !defined(__ia64__) && !defined(__mips__) && \
    !defined(__arm__)
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
#endif /* !alpha && !__ia64__ && !defined(__mips__) && !defined(__arm__) */

#ifndef HAVE_ARCH_SYS_PAUSE

asmlinkage int
sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

#endif /* HAVE_ARCH_SYS_PAUSE */

void __init signals_init(void)
{
	sigqueue_cachep =
		kmem_cache_create("sigqueue",
				  sizeof(struct sigqueue),
				  __alignof__(struct sigqueue),
				  0, NULL, NULL);
	if (!sigqueue_cachep)
		panic("signals_init(): cannot create sigqueue SLAB cache");
}


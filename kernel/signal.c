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

#include <asm/segment.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

extern int core_dump(long signr,struct pt_regs * regs);

int sys_sgetmask(void)
{
	return current->blocked;
}

int sys_ssetmask(int newmask)
{
	int old=current->blocked;

	current->blocked = newmask & _BLOCKABLE;
	return old;
}

int sys_sigpending(sigset_t *set)
{
	int error;
	/* fill in "set" with signals pending but blocked. */
	error = verify_area(VERIFY_WRITE, set, 4);
	if (!error)
		put_fs_long(current->blocked & current->signal, (unsigned long *)set);
	return error;
}

/* atomically swap in the new signal mask, and wait for a signal.
 *
 * we need to play some games with syscall restarting.  We get help
 * from the syscall library interface.  Note that we need to coordinate
 * the calling convention with the libc routine.
 *
 * "set" is just the sigmask as described in 1003.1-1988, 3.3.7.
 * 	It is assumed that sigset_t can be passed as a 32 bit quantity.
 *
 * "restart" holds a restart indication.  If it's 1, then we 
 * 	install the old mask, and return normally.  If it's zero, we store 
 * 	the current mask in old_mask and block until a signal comes in.
 *	If it's 2, then it's a signal we must handle but not return from.
 *
 * We are careful to prevent a rouge restart from user space from fooling
 * us into blocking SIGKILL or SIGSTOP.
 */
int sys_sigsuspend(volatile int restart, volatile unsigned long old_mask, unsigned long set)
{
	extern int sys_pause(void);

	switch (restart) {
	case 0:
		/* we're not restarting.  do the work */
		restart = 1;
		old_mask = current->blocked;
		current->blocked = set & _BLOCKABLE;
		break;
	case 1:
		/* we're restarting to restore and exit */
		current->blocked = old_mask & _BLOCKABLE;
		return -EINTR;
	case 2:
		/* we're restarting but staying paused */
		restart = 1;
		break;
	}
	/* pause returns after a signal arrives */
	if (sys_pause() == -ERESTARTSYS)
		restart = 2;
	return -ERESTARTNOINTR;		/* handle the signal, and come back */
}

/*
 * POSIX 3.3.1.3:
 *  "Setting a signal action to SIG_IGN for a signal that is pending
 *   shall cause the pending signal to be discarded, whether or not
 *   it is blocked" (but SIGCHLD is unspecified: linux leaves it alone).
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
static void check_pending(int signum)
{
	struct sigaction *p;

	p = signum - 1 + current->sigaction;
	if (p->sa_handler == SIG_IGN) {
		if (signum == SIGCHLD)
			return;
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

int sys_signal(int signum, long handler, long restorer)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL || signum==SIGSTOP)
		return -EINVAL;
	tmp.sa_handler = (void (*)(int)) handler;
	tmp.sa_mask = 0;
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK | SA_INTERRUPT;
	tmp.sa_restorer = (void (*)(void)) restorer;
	handler = (long) current->sigaction[signum-1].sa_handler;
	current->sigaction[signum-1] = tmp;
	check_pending(signum);
	return handler;
}

int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction new, *p;

	if (signum<1 || signum>32 || signum==SIGKILL || signum==SIGSTOP)
		return -EINVAL;
	p = signum - 1 + current->sigaction;
	if (action) {
		memcpy_fromfs(&new, action, sizeof(struct sigaction));
		if (new.sa_flags & SA_NOMASK)
			new.sa_mask = 0;
		else {
			new.sa_mask |= _S(signum);
			new.sa_mask &= _BLOCKABLE;
		}
	}
	if (oldaction) {
		if (!verify_area(VERIFY_WRITE,oldaction, sizeof(struct sigaction)))
			memcpy_tofs(oldaction, p, sizeof(struct sigaction));
	}
	if (action) {
		*p = new;
		check_pending(signum);
	}
	return 0;
}

extern int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options);

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
int do_signal(long signr,struct pt_regs * regs)
{
	unsigned long sa_handler;
	long old_eip = regs->eip;
	struct sigaction * sa = current->sigaction + signr - 1;
	int longs;
	unsigned long * tmp_esp;

	sa_handler = (unsigned long) sa->sa_handler;
	if ((regs->orig_eax >= 0) &&
	    ((regs->eax == -ERESTARTSYS) || (regs->eax == -ERESTARTNOINTR))) {
		if ((sa_handler > 1) && (regs->eax == -ERESTARTSYS) &&
		    (sa->sa_flags & SA_INTERRUPT))
			regs->eax = -EINTR;
		else {
			regs->eax = regs->orig_eax;
			regs->eip = old_eip -= 2;
		}
	}
	if (sa_handler==1) {
/* check for SIGCHLD: it's special */
		if (signr == SIGCHLD)
			while (sys_waitpid(-1,NULL,WNOHANG) > 0)
				/* nothing */;
		return(1);   /* Ignore, see if there are more signals... */
	}
	if (!sa_handler) {
		if (current->pid == 1)
			return 1;
		switch (signr) {
		case SIGCONT:
		case SIGCHLD:
		case SIGWINCH:
			return(1);  /* Ignore, ... */

		case SIGSTOP:
		case SIGTSTP:
		case SIGTTIN:
		case SIGTTOU:
			current->state = TASK_STOPPED;
			current->exit_code = signr;
			if (!(current->p_pptr->sigaction[SIGCHLD-1].sa_flags & 
					SA_NOCLDSTOP))
				send_sig(SIGCHLD, current->p_pptr, 1);
			return(1);  /* Reschedule another event */

		case SIGQUIT:
		case SIGILL:
		case SIGTRAP:
		case SIGIOT:
		case SIGFPE:
		case SIGSEGV:
			if (core_dump(signr,regs))
				signr |= 0x80;
			/* fall through */
		default:
			current->signal |= _S(signr & 0x7f);
			do_exit(signr);
		}
	}
	/*
	 * OK, we're invoking a handler 
	 */
	if (sa->sa_flags & SA_ONESHOT)
		sa->sa_handler = NULL;
	regs->eip = sa_handler;
	longs = (sa->sa_flags & SA_NOMASK)?(7*4):(8*4);
	regs->esp -= longs;
	tmp_esp = (unsigned long *) regs->esp;
	verify_area(VERIFY_WRITE,tmp_esp,longs);
	put_fs_long((long) sa->sa_restorer,tmp_esp++);
	put_fs_long(signr,tmp_esp++);
	if (!(sa->sa_flags & SA_NOMASK))
		put_fs_long(current->blocked,tmp_esp++);
	put_fs_long(regs->eax,tmp_esp++);
	put_fs_long(regs->ecx,tmp_esp++);
	put_fs_long(regs->edx,tmp_esp++);
	put_fs_long(regs->eflags,tmp_esp++);
	put_fs_long(old_eip,tmp_esp++);
	current->blocked |= sa->sa_mask;
/* force a supervisor-mode page-in of the signal handler to reduce races */
	__asm__("testb $0,%%fs:%0"::"m" (*(char *) sa_handler));
	return(0);		/* Continue, execute handler */
}

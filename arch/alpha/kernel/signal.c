/*
 *  linux/arch/alpha/kernel/signal.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/mm.h>

#include <asm/bitops.h>
#include <asm/segment.h>

#define _S(nr) (1<<((nr)-1))
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

asmlinkage int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options);
asmlinkage void ret_from_sys_call(void);
asmlinkage int do_signal(unsigned long, struct pt_regs *, struct switch_stack *,
	unsigned long, unsigned long);
asmlinkage void imb(void);

/*
 * The OSF/1 sigprocmask calling sequence is different from the
 * C sigprocmask() sequence..
 */
asmlinkage unsigned long osf_sigprocmask(int how, unsigned long newmask)
{
	unsigned long oldmask = current->blocked;

	newmask &= _BLOCKABLE;
	switch (how) {
		case SIG_BLOCK:
			current->blocked |= newmask;
			return oldmask;
		case SIG_UNBLOCK:
			current->blocked &= ~newmask;
			return oldmask;
		case SIG_SETMASK:
			current->blocked = newmask;
			return oldmask;
	}
	return -EINVAL;
}

/*
 * atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int do_sigsuspend(unsigned long mask, struct pt_regs * regs, struct switch_stack * sw)
{
	unsigned long oldmask = current->blocked;
	current->blocked = mask & _BLOCKABLE;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(oldmask,regs, sw, 0, 0))
			return -EINTR;
	}
}

/*
 * Do a signal return; undo the signal stack.
 */
asmlinkage void do_sigreturn(struct sigcontext_struct * sc, 
	struct pt_regs * regs, struct switch_stack * sw)
{
	unsigned long mask;
	int i;

	/* verify that it's a good sigcontext before using it */
	if (verify_area(VERIFY_READ, sc, sizeof(*sc)))
		do_exit(SIGSEGV);
	if (get_fs_quad(&sc->sc_ps) != 8)
		do_exit(SIGSEGV);
	mask = get_fs_quad(&sc->sc_mask);
	if (mask & ~_BLOCKABLE)
		do_exit(SIGSEGV);

	/* ok, looks fine, start restoring */
	wrusp(get_fs_quad(sc->sc_regs+30));
	regs->pc = get_fs_quad(&sc->sc_pc);
	sw->r26 = (unsigned long) ret_from_sys_call;
	current->blocked = mask;

	regs->r0  = get_fs_quad(sc->sc_regs+0);
	regs->r1  = get_fs_quad(sc->sc_regs+1);
	regs->r2  = get_fs_quad(sc->sc_regs+2);
	regs->r3  = get_fs_quad(sc->sc_regs+3);
	regs->r4  = get_fs_quad(sc->sc_regs+4);
	regs->r5  = get_fs_quad(sc->sc_regs+5);
	regs->r6  = get_fs_quad(sc->sc_regs+6);
	regs->r7  = get_fs_quad(sc->sc_regs+7);
	regs->r8  = get_fs_quad(sc->sc_regs+8);
	sw->r9    = get_fs_quad(sc->sc_regs+9);
	sw->r10   = get_fs_quad(sc->sc_regs+10);
	sw->r11   = get_fs_quad(sc->sc_regs+11);
	sw->r12   = get_fs_quad(sc->sc_regs+12);
	sw->r13   = get_fs_quad(sc->sc_regs+13);
	sw->r14   = get_fs_quad(sc->sc_regs+14);
	sw->r15   = get_fs_quad(sc->sc_regs+15);
	regs->r16 = get_fs_quad(sc->sc_regs+16);
	regs->r17 = get_fs_quad(sc->sc_regs+17);
	regs->r18 = get_fs_quad(sc->sc_regs+18);
	regs->r19 = get_fs_quad(sc->sc_regs+19);
	regs->r20 = get_fs_quad(sc->sc_regs+20);
	regs->r21 = get_fs_quad(sc->sc_regs+21);
	regs->r22 = get_fs_quad(sc->sc_regs+22);
	regs->r23 = get_fs_quad(sc->sc_regs+23);
	regs->r24 = get_fs_quad(sc->sc_regs+24);
	regs->r25 = get_fs_quad(sc->sc_regs+25);
	regs->r26 = get_fs_quad(sc->sc_regs+26);
	regs->r27 = get_fs_quad(sc->sc_regs+27);
	regs->r28 = get_fs_quad(sc->sc_regs+28);
	regs->gp  = get_fs_quad(sc->sc_regs+29);
	for (i = 0; i < 31; i++)
		sw->fp[i] = get_fs_quad(sc->sc_fpregs+i);
}

/*
 * Set up a signal frame...
 */
static void setup_frame(struct sigaction * sa, struct sigcontext_struct ** fp, unsigned long pc,
	struct pt_regs * regs, struct switch_stack * sw, int signr, unsigned long oldmask)
{
	int i;
	struct sigcontext_struct * sc;

	sc = *fp;
	/* check here if we would need to switch stacks.. */
	sc--;
	if (verify_area(VERIFY_WRITE, sc, sizeof(*sc)))
		do_exit(SIGSEGV);

	put_fs_quad(oldmask, &sc->sc_mask);
	put_fs_quad(8, &sc->sc_ps);
	put_fs_quad(pc, &sc->sc_pc);
	put_fs_quad((unsigned long)*fp, sc->sc_regs+30);

	put_fs_quad(regs->r0 , sc->sc_regs+0);
	put_fs_quad(regs->r1 , sc->sc_regs+1);
	put_fs_quad(regs->r2 , sc->sc_regs+2);
	put_fs_quad(regs->r3 , sc->sc_regs+3);
	put_fs_quad(regs->r4 , sc->sc_regs+4);
	put_fs_quad(regs->r5 , sc->sc_regs+5);
	put_fs_quad(regs->r6 , sc->sc_regs+6);
	put_fs_quad(regs->r7 , sc->sc_regs+7);
	put_fs_quad(regs->r8 , sc->sc_regs+8);
	put_fs_quad(sw->r9   , sc->sc_regs+9);
	put_fs_quad(sw->r10  , sc->sc_regs+10);
	put_fs_quad(sw->r11  , sc->sc_regs+11);
	put_fs_quad(sw->r12  , sc->sc_regs+12);
	put_fs_quad(sw->r13  , sc->sc_regs+13);
	put_fs_quad(sw->r14  , sc->sc_regs+14);
	put_fs_quad(sw->r15  , sc->sc_regs+15);
	put_fs_quad(regs->r16, sc->sc_regs+16);
	put_fs_quad(regs->r17, sc->sc_regs+17);
	put_fs_quad(regs->r18, sc->sc_regs+18);
	put_fs_quad(regs->r19, sc->sc_regs+19);
	put_fs_quad(regs->r20, sc->sc_regs+20);
	put_fs_quad(regs->r21, sc->sc_regs+21);
	put_fs_quad(regs->r22, sc->sc_regs+22);
	put_fs_quad(regs->r23, sc->sc_regs+23);
	put_fs_quad(regs->r24, sc->sc_regs+24);
	put_fs_quad(regs->r25, sc->sc_regs+25);
	put_fs_quad(regs->r26, sc->sc_regs+26);
	put_fs_quad(regs->r27, sc->sc_regs+27);
	put_fs_quad(regs->r28, sc->sc_regs+28);
	put_fs_quad(regs->gp , sc->sc_regs+29);
	for (i = 0; i < 31; i++)
		put_fs_quad(sw->fp[i], sc->sc_fpregs+i);

	/*
	 * The following is:
	 *
	 * bis $30,$30,$16
	 * addq $31,0x67,$0
	 * call_pal callsys
	 *
	 * ie, "sigreturn(stack-pointer)"
	 */
	put_fs_quad(0x43ecf40047de0410, sc->sc_retcode+0);
	put_fs_quad(0x0000000000000083, sc->sc_retcode+1);
	regs->r26 = (unsigned long) sc->sc_retcode;
	regs->r16 = signr;
	*fp = sc;
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
asmlinkage int do_signal(unsigned long oldmask,
	struct pt_regs * regs,
	struct switch_stack * sw,
	unsigned long r0, unsigned long r19)
{
	unsigned long mask = ~current->blocked;
	unsigned long handler_signal = 0;
	struct sigcontext_struct *frame = NULL;
	unsigned long pc = 0;
	unsigned long signr;
	struct sigaction * sa;
	extern ptrace_cancel_bpt (struct task_struct *child);

	ptrace_cancel_bpt(current);	/* make sure single-step bpt is gone */

	while ((signr = current->signal & mask) != 0) {
		signr = ffz(~signr);
		clear_bit(signr, &current->signal);
		sa = current->sigaction + signr;
		signr++;
		if ((current->flags & PF_PTRACED) && signr != SIGKILL) {
			current->exit_code = signr;
			current->state = TASK_STOPPED;
			notify_parent(current);
			schedule();
			if (!(signr = current->exit_code))
				continue;
			current->exit_code = 0;
			if (signr == SIGSTOP)
				continue;
			if (_S(signr) & current->blocked) {
				current->signal |= _S(signr);
				continue;
			}
			sa = current->sigaction + signr - 1;
		}
		if (sa->sa_handler == SIG_IGN) {
			if (signr != SIGCHLD)
				continue;
			/* check for SIGCHLD: it's special */
			while (sys_waitpid(-1,NULL,WNOHANG) > 0)
				/* nothing */;
			continue;
		}
		if (sa->sa_handler == SIG_DFL) {
			if (current->pid == 1)
				continue;
			switch (signr) {
			case SIGCONT: case SIGCHLD: case SIGWINCH:
				continue;

			case SIGSTOP: case SIGTSTP: case SIGTTIN: case SIGTTOU:
				if (current->flags & PF_PTRACED)
					continue;
				current->state = TASK_STOPPED;
				current->exit_code = signr;
				if (!(current->p_pptr->sigaction[SIGCHLD-1].sa_flags & 
						SA_NOCLDSTOP))
					notify_parent(current);
				schedule();
				continue;

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGABRT: case SIGFPE: case SIGSEGV:
				if (current->binfmt && current->binfmt->core_dump) {
					if (current->binfmt->core_dump(signr, regs))
						signr |= 0x80;
				}
				/* fall through */
			default:
				current->signal |= _S(signr & 0x7f);
				do_exit(signr);
			}
		}
		/*
		 * OK, we're invoking a handler
		 */
		if (r0) {
			if (regs->r0 == ERESTARTNOHAND ||
			   (regs->r0 == ERESTARTSYS && !(sa->sa_flags & SA_RESTART)))
				regs->r0 = EINTR;
		}
		handler_signal |= 1 << (signr-1);
		mask &= ~sa->sa_mask;
	}
	if (r0 &&
	    (regs->r0 == ERESTARTNOHAND ||
	     regs->r0 == ERESTARTSYS ||
	     regs->r0 == ERESTARTNOINTR)) {
		regs->r0 = r0;
		regs->r19 = r19;
		regs->pc -= 4;
	}
	if (!handler_signal)		/* no handler will be called - return 0 */
		return 0;
	pc = regs->pc;
	frame = (struct sigcontext_struct *) rdusp();
	signr = 1;
	sa = current->sigaction;
	for (mask = 1 ; mask ; sa++,signr++,mask += mask) {
		if (mask > handler_signal)
			break;
		if (!(mask & handler_signal))
			continue;
		setup_frame(sa,&frame,pc,regs,sw,signr,oldmask);
		pc = (unsigned long) sa->sa_handler;
		regs->r27 = pc;
		if (sa->sa_flags & SA_ONESHOT)
			sa->sa_handler = NULL;
		current->blocked |= sa->sa_mask;
		oldmask |= sa->sa_mask;
	}
	imb();
	wrusp((unsigned long) frame);
	regs->pc = pc;			/* "return" to the first handler */
	return 1;
}

/*  $Id: signal.c,v 1.19 1995/11/25 00:58:24 davem Exp $
 *  linux/arch/sparc/kernel/signal.c
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/mm.h>

#include <asm/segment.h>
#include <asm/bitops.h>
#include <asm/ptrace.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

asmlinkage int sys_waitpid(pid_t pid, unsigned long *stat_addr, int options);
asmlinkage int do_signal(unsigned long oldmask, struct pt_regs * regs);

/*
 * atomically swap in the new signal mask, and wait for a signal.
 * This is really tricky on the Sparc, watch out...
 */
asmlinkage inline int _sigpause_common(unsigned int set, struct pt_regs *regs)
{
	unsigned long mask;

	mask = current->blocked;
	current->blocked = set & _BLOCKABLE;
	/* Advance over the syscall instruction for when
	 * we return.  We want setup_frame to save the proper
	 * state, but then below we set ourselves backwards
	 * to compensate for what ret_sys_call does. Ick!
	 */
	regs->pc += 4;
	regs->npc += 4;

	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(mask, regs)) {
			/* If we actually post the signal then we
			 * will get our PCs advanced in entry.S, to
			 * compensate we throw the PCs back one insn.
			 */
			regs->pc -= 4;
			regs->npc -= 4;
			return -EINTR;
		}
	}
}

asmlinkage int sys_sigpause(unsigned int set)
{
	struct pt_regs *regs;
	unsigned long fp;

	__asm__ __volatile__("mov %%fp, %0\n\t" :
			     "=r" (fp));
	regs = ((struct pt_regs *) (fp + STACKFRAME_SZ));
	return _sigpause_common(set, regs);
}

asmlinkage int sys_sigsuspend(unsigned int *sigmaskp)
{
	struct pt_regs *regs;
	unsigned long fp;
	unsigned int set;

	__asm__ __volatile__("mov %%fp, %0\n\t" :
			     "=r" (fp));
	regs = ((struct pt_regs *) (fp + STACKFRAME_SZ));
	if(verify_area(VERIFY_READ, sigmaskp, sizeof(unsigned int)))
		return -EFAULT;
	set = *sigmaskp;
	return _sigpause_common(set, regs);
}

extern unsigned long nwindows;

asmlinkage void do_sigreturn(struct pt_regs *regs)
{
	int wsvd;
	struct sigcontext_struct *scptr =
		(struct sigcontext_struct *) regs->u_regs[UREG_I0];

	/* Make the stack consistant. */
	flush_user_windows();

	/* Check sanity of the user arg. */
	if(verify_area(VERIFY_READ, scptr, sizeof(struct sigcontext_struct)) ||
	   ((((unsigned long) scptr)) & 0x3))
		do_exit(SIGSEGV);

	if((scptr->sigc_pc | scptr->sigc_npc) & 3)
		return; /* Nice try. */

	current->blocked = scptr->sigc_mask & _BLOCKABLE;
	current->tss.sstk_info.cur_status = (scptr->sigc_onstack & 0x1);
	regs->pc = scptr->sigc_pc;
	regs->npc = scptr->sigc_npc;
	regs->u_regs[UREG_FP] = scptr->sigc_sp;
	regs->u_regs[UREG_I0] = scptr->sigc_o0;
	regs->u_regs[UREG_G1] = scptr->sigc_g1;

	/* User can only change condition codes in %psr. */
	regs->psr &= (~PSR_ICC);
	regs->psr |= (scptr->sigc_psr & PSR_ICC);
}

/* I love register windows...
 * Basically, this tries to get as many of the current user
 * windows on the stack as possible, even if unaligned.  Failing
 * that the windows end up in the per-task window save buffer
 * in the thread struct, these will be copied onto the signal
 * stack for possible inspection by the process.
 */
static inline void
do_magic_sparc_stuff(void)
{
	unsigned long sp;
	int this_win;

	flush_user_windows();
	this_win = (current->tss.w_saved - 1);
	while(this_win >= 0) {
		sp = current->tss.rwbuf_stkptrs[this_win];
		if(!(sp & 7) && !verify_area(VERIFY_WRITE, (char *)sp,
		                             sizeof(struct reg_window))) {
			memcpy((char *)sp,
			       (char *)&current->tss.reg_window[this_win],
			       sizeof(struct reg_window));
			current->tss.w_saved--;
			memcpy((char *)&current->tss.reg_window[this_win],
			       (char *)&current->tss.reg_window[this_win+1],
			       (sizeof(struct reg_window) *
			       (current->tss.w_saved-this_win)));
		}
		this_win -= 1;
	}
}

/*
 * Set up a signal frame... Make the stack look the way SunOS
 * expects it to look which is basically:
 *
 * ---------------------------------- <-- %sp at signal time
 * Struct sigcontext
 * Signal address
 * Ptr to sigcontext area above
 * Signal code
 * The signal number itself
 * One register window
 * ---------------------------------- <-- New %sp
 */
struct signal_sframe {
	struct reg_window sig_window;
	int sig_num;
	int sig_code;
	struct sigcontext_struct *sig_scptr;
	int sig_address;
	struct sigcontext_struct sig_context;
};
/* To align the structure properly. */
#define SF_ALIGNEDSZ  (((sizeof(struct signal_sframe) + 7) & (~7)))

static inline void
setup_frame(struct sigaction *sa, struct sigcontext_struct **fp,
	    unsigned long pc, struct pt_regs *regs, int signr,
	    unsigned long oldmask)
{
	struct signal_sframe *sframep;
	struct sigcontext_struct *sc;
	int window = 0;
	int old_status = current->tss.sstk_info.cur_status;

	sframep = (struct signal_sframe *) *fp;
	sframep = (struct signal_sframe *) (((unsigned long) sframep)-SF_ALIGNEDSZ);
	sc = &sframep->sig_context;
	if(verify_area(VERIFY_WRITE, sframep, sizeof(*sframep)) ||
	   (((unsigned long) sframep) & 7) ||
	   (((unsigned long) sframep) >= KERNBASE)) {
		printk("%s [%d]: User has trashed signal stack\n",
		       current->comm, current->pid);
		printk("Sigstack ptr %p handler at pc<%08lx> for sig<%d>\n",
		       sframep, pc, signr);
		show_regs(regs);
		/* Don't change signal code and address, so that
		 * post mortem debuggers can have a look.
		 */
		current->sig->action[SIGILL-1].sa_handler = SIG_DFL;
		current->blocked &= ~(1<<(SIGILL-1));
		send_sig(SIGILL,current,1);
		return;
	}
	*fp = (struct sigcontext_struct *) sframep;
	do_magic_sparc_stuff();

	sc->sigc_onstack = old_status;
	sc->sigc_mask = oldmask;
	sc->sigc_sp = regs->u_regs[UREG_FP];
	sc->sigc_pc = regs->pc;
	sc->sigc_npc = regs->npc;
	sc->sigc_psr = regs->psr;
	sc->sigc_g1 = regs->u_regs[UREG_G1];
	sc->sigc_o0 = regs->u_regs[UREG_I0];
	sc->sigc_oswins = current->tss.w_saved;
	if(current->tss.w_saved)
		for(window = 0; window < current->tss.w_saved; window++) {
			sc->sigc_spbuf[window] =
				(char *)current->tss.rwbuf_stkptrs[window];
			memcpy(&sc->sigc_wbuf[window],
			       &current->tss.reg_window[window],
			       sizeof(struct reg_window));
		}
	else
		/* This is so that gdb and friends can trace back
		 * through the stack properly.  An alternative
		 * is to set the fp in this new window to the
		 * sp of the frame at the time of the signal and
		 * I see some problems with that maneuver.
		 */
		memcpy(sframep, (char *)regs->u_regs[UREG_FP],
		       sizeof(struct reg_window));

	current->tss.w_saved = 0; /* So process is allowed to execute. */
	sframep->sig_num = signr;
	if(signr == SIGSEGV ||
	   signr == SIGILL ||
	   signr == SIGFPE ||
	   signr == SIGBUS ||
	   signr == SIGEMT) {
		sframep->sig_code = current->tss.sig_desc;
		sframep->sig_address = current->tss.sig_address;
	} else {
		sframep->sig_code = 0;
		sframep->sig_address = 0;
	}
	sframep->sig_scptr = sc;
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

asmlinkage int do_signal(unsigned long oldmask, struct pt_regs * regs)
{
	unsigned long mask = ~current->blocked;
	unsigned long handler_signal = 0;
	struct sigcontext_struct *frame = NULL;
	unsigned long pc = 0;
	unsigned long signr;
	struct sigaction *sa;

	while ((signr = current->signal & mask) != 0) {
		signr = ffz(~signr);
		clear_bit(signr, &current->signal);
		sa = current->sig->action + signr;
		signr++;
		if((current->flags & PF_PTRACED) && signr != SIGKILL) {
			current->exit_code = signr;
			current->state = TASK_STOPPED;
			notify_parent(current);
			schedule();
			if(!(signr = current->exit_code))
				continue;
			current->exit_code = 0;
			if(signr == SIGSTOP)
				continue;
			if(_S(signr) & current->blocked) {
				current->signal |= _S(signr);
				continue;
			}
			sa = current->sig->action + signr - 1;
		}
		if(sa->sa_handler == SIG_IGN) {
			if(signr != SIGCHLD)
				continue;
			while(sys_waitpid(-1,NULL,WNOHANG) > 0);
			continue;
		}
		if(sa->sa_handler == SIG_DFL) {
			if(current->pid == 1)
				continue;
			switch(signr) {
			case SIGCONT: case SIGCHLD: case SIGWINCH:
				continue;

			case SIGSTOP: case SIGTSTP: case SIGTTIN: case SIGTTOU:
				if(current->flags & PF_PTRACED)
					continue;
				current->state = TASK_STOPPED;
				current->exit_code = signr;
				if(!(current->p_pptr->sig->action[SIGCHLD-1].sa_flags &
				     SA_NOCLDSTOP))
					notify_parent(current);
				schedule();
				continue;

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGABRT: case SIGFPE: case SIGSEGV:
				if(current->binfmt && current->binfmt->core_dump) {
					if(current->binfmt->core_dump(signr, regs))
						signr |= 0x80;
				}
				/* fall through */
			default:
				current->signal |= _S(signr & 0x7f);
				do_exit(signr);
			}
		}
		/* OK, we're invoking a handler. */
		handler_signal |= 1 << (signr - 1);
		mask &= ~sa->sa_mask;
	}
	if(!handler_signal)
		return 0;
	pc = regs->pc;
	frame = (struct sigcontext_struct *) regs->u_regs[UREG_FP];
	signr = 1;
	sa = current->sig->action;
	for(mask = 1; mask; sa++, signr++, mask += mask) {
		if(mask > handler_signal)
			break;
		if(!(mask & handler_signal))
			continue;
		setup_frame(sa, &frame, pc, regs, signr, oldmask);
		pc = (unsigned long) sa->sa_handler;
		if(sa->sa_flags & SA_ONESHOT)
			sa->sa_handler = NULL;
		current->blocked |= sa->sa_mask;
		oldmask |= sa->sa_mask;
	}
	regs->u_regs[UREG_FP] = (unsigned long) frame;
	regs->npc = (regs->pc = pc) + 4;
	return 1;
}

asmlinkage int
sys_sigstack(struct sigstack *ssptr, struct sigstack *ossptr)
{
	/* First see if old state is wanted. */
	if(ossptr) {
		if(verify_area(VERIFY_WRITE, ossptr, sizeof(struct sigstack)))
			return -EFAULT;
		memcpy(ossptr, &current->tss.sstk_info, sizeof(struct sigstack));
	}

	/* Now see if we want to update the new state. */
	if(ssptr) {
		if(verify_area(VERIFY_READ, ssptr, sizeof(struct sigstack)))
			return -EFAULT;
		memcpy(&current->tss.sstk_info, ssptr, sizeof(struct sigstack));
	}
	return 0;
}

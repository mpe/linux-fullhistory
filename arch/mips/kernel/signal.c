/*
 *  linux/arch/mips/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1994, 1995, 1996  Ralf Baechle
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

#include <asm/asm.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

asmlinkage int sys_wait4(pid_t pid, unsigned long *stat_addr,
                         int options, unsigned long *ru);
asmlinkage int do_signal(unsigned long oldmask, struct pt_regs *regs);
extern asmlinkage void (*save_fp_context)(struct sigcontext *sc);
extern asmlinkage void (*restore_fp_context)(struct sigcontext *sc);

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 * Unlike on Intel we pass a sigset_t *, not sigset_t.
 */
asmlinkage int sys_sigsuspend(struct pt_regs *regs)
{
	unsigned long mask;
	sigset_t *uset, set;

	uset = (sigset_t *)(long) regs->regs[4];
	if (get_user(set, uset))
		return -EFAULT;

	spin_lock_irq(&current->sigmask_lock);
	mask = current->blocked;
	current->blocked = set & _BLOCKABLE;
	spin_unlock_irq(&current->sigmask_lock);

	regs->regs[2] = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(mask, regs))
			return -EINTR;
	}
	return -EINTR;
}

asmlinkage int sys_sigreturn(struct pt_regs *regs)
{
	struct sigcontext *context;
	int i;

	context = (struct sigcontext *)(long) regs->regs[29];
	if (!access_ok(VERIFY_READ, context, sizeof(struct sigcontext)) ||
	    (regs->regs[29] & (SZREG - 1)))
		goto badframe;

	current->blocked = context->sc_sigset & _BLOCKABLE;
	regs->cp0_epc = context->sc_pc;

	/*
	 * We only allow user processes in 64bit mode (n32, 64 bit ABI) to
	 * restore the upper half of registers.
	 */
	if (read_32bit_cp0_register(CP0_STATUS) & ST0_UX)
		for(i = 31;i >= 0;i--)
			__get_user(regs->regs[i], &context->sc_regs[i]);
	else
		for(i = 31;i >= 0;i--) {
			__get_user(regs->regs[i], &context->sc_regs[i]);
			regs->regs[i] = (int) regs->regs[i];
		}

	__get_user(regs->hi, &context->sc_mdhi);
	__get_user(regs->lo, &context->sc_mdlo);
	restore_fp_context(context);

	/*
	 * Disable syscall checks
	 */
	regs->orig_reg2 = -1;

	/*
	 * Don't let your children do this ...
	 */
	__asm__ __volatile__(
		"move\t$29,%0\n\t"
		"j\tret_from_sys_call"
		:/* no outputs */
		:"r" (regs));
	/* Unreached */

badframe:
	lock_kernel();
	do_exit(SIGSEGV);
	unlock_kernel();
}

/*
 * Set up a signal frame...
 *
 * This routine is somewhat complicated by the fact that if the kernel may be
 * entered by an exception other than a system call; e.g. a bus error or other
 * "bad" exception.  If this is the case, then *all* the context on the kernel
 * stack frame must be saved.
 *
 * For a large number of exceptions, the stack frame format is the same
 * as that which will be created when the process traps back to the kernel
 * when finished executing the signal handler.	In this case, nothing
 * must be done. This information is saved on the user stack and restored
 * when the signal handler is returned.
 *
 * The signal handler will be called with ra pointing to code1 (see below) and
 * one parameters in a0 (signum).
 *
 *     usp ->  [unused]                         ; first free word on stack
 *             arg save space                   ; 16/32 bytes arg. save space
 *	       code1   (addiu sp,#1-offset)	; pop stackframe
 *	       code2   (li v0,__NR_sigreturn)	; syscall number
 *	       code3   (syscall)		; do sigreturn(2)
 *     #1|     $0, at, v0, v1, a0, a1, a2, a3   ; All integer registers
 *       |     t0, t1, t2, t3, t4, t5, t6, t7   ; $0, k0 and k1 are placeholders
 *       |     s0, s1, s2, s3, s4, s5, s6, s7
 *       |     k0, k1, t8, t9, gp, sp, fp, ra;
 *       |     epc                              ; old program counter
 *       |     cause                            ; CP0 cause register
 *       |     oldmask
 */

struct sc {
	unsigned long	ass[4];
	unsigned int	code[4];
	struct sigcontext scc;
};
#define scc_offset ((size_t)&((struct sc *)0)->scc)

static void setup_frame(struct sigaction * sa, struct pt_regs *regs,
                        int signr, unsigned long oldmask)
{
	struct sc *frame;
	struct sigcontext *sc;
	int i;

	/* Align the stackframe to an adequate boundary for the architecture. */
	frame = (struct sc *) (long) regs->regs[29];
	frame--;
	frame = (struct sc *)((unsigned long)frame & ALMASK);

	if (verify_area(VERIFY_WRITE, frame, sizeof (*frame)))
		goto segv_and_exit;
	sc = &frame->scc;

	/*
	 * Set up the return code ...
	 *
	 *         .set    noreorder
	 *         addiu   sp,24
	 *         li      v0,__NR_sigreturn
	 *         syscall
	 *         .set    reorder
	 */
	__put_user(0x27bd0000 + scc_offset, &frame->code[0]);
	__put_user(0x24020000 + __NR_sigreturn, &frame->code[1]);
	__put_user(0x0000000c, &frame->code[2]);

	/*
	 * Flush caches so that the instructions will be correctly executed.
	 */
	flush_cache_sigtramp((unsigned long) frame->code);

	/*
	 * Set up the "normal" sigcontext
	 */
	__put_user(regs->cp0_epc, &sc->sc_pc);
	__put_user(regs->cp0_status, &sc->sc_status);	/* Status register */
	for(i = 31;i >= 0;i--)
		__put_user(regs->regs[i], &sc->sc_regs[i]);
	save_fp_context(sc);
	__put_user(regs->hi, &sc->sc_mdhi);
	__put_user(regs->lo, &sc->sc_mdlo);
	__put_user(regs->cp0_cause, &sc->sc_cause);
	__put_user((regs->cp0_status & ST0_CU1) != 0, &sc->sc_ownedfp);
	__put_user(oldmask, &sc->sc_sigset);
	__put_user(0, &sc->__pad0[0]);
	__put_user(0, &sc->__pad0[1]);
	__put_user(0, &sc->__pad0[2]);

	regs->regs[4] = signr;				/* Arguments for handler */
	regs->regs[5] = 0;				/* For now. */
	regs->regs[6] = (long) frame;			/* Pointer to sigcontext */
	regs->regs[29] = (unsigned long) frame;		/* Stack pointer */
	regs->regs[31] = (unsigned long) frame->code;	/* Return address */
	regs->cp0_epc = (unsigned long) sa->sa_handler;	/* "return" to the first handler */
	regs->regs[25] = regs->cp0_epc;			/* PIC shit... */
	return;

segv_and_exit:
	lock_kernel();
	do_exit(SIGSEGV);
	unlock_kernel();
}

static inline void handle_signal(unsigned long signr, struct sigaction *sa,
	unsigned long oldmask, struct pt_regs * regs)
{
	setup_frame(sa, regs, signr, oldmask);

	if (sa->sa_flags & SA_ONESHOT)
		sa->sa_handler = NULL;
	if (!(sa->sa_flags & SA_NOMASK)) {
		spin_lock_irq(&current->sigmask_lock);
		current->blocked |= (sa->sa_mask | _S(signr)) & _BLOCKABLE;
		spin_unlock_irq(&current->sigmask_lock);
	}
}

static inline void syscall_restart(unsigned long r0, unsigned long or2,
				   unsigned long or7, struct pt_regs *regs,
				   struct sigaction *sa)
{
	switch(r0) {
	case ERESTARTNOHAND:
	no_system_call_restart:
		regs->regs[0] = regs->regs[2] = EINTR;
		break;
	case ERESTARTSYS:
		if(!(sa->sa_flags & SA_RESTART))
			goto no_system_call_restart;
	/* fallthrough */
	case ERESTARTNOINTR:
		regs->regs[0] = regs->regs[2] = or2;
		regs->regs[7] = or7;
		regs->cp0_epc -= 8;
	}
}

extern int do_irix_signal(unsigned long oldmask, struct pt_regs *regs);

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
	unsigned long signr, r0 = regs->regs[0];
	unsigned long r7 = regs->orig_reg7;
	struct sigaction * sa;

#ifdef CONFIG_BINFMT_IRIX
	if(current->personality != PER_LINUX)
		return do_irix_signal(oldmask, regs);
#endif
	while ((signr = current->signal & mask)) {
		signr = ffz(~signr);
		clear_bit(signr, &current->signal);
		sa = current->sig->action + signr;
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
				spin_lock_irq(&current->sigmask_lock);
				current->signal |= _S(signr);
				spin_unlock_irq(&current->sigmask_lock);
				continue;
			}
			sa = current->sig->action + signr - 1;
		}
		if (sa->sa_handler == SIG_IGN) {
			if (signr != SIGCHLD)
				continue;
			/* check for SIGCHLD: it's special */
			while (sys_wait4(-1,NULL,WNOHANG, NULL) > 0)
				/* nothing */;
			continue;
		}
		if (sa->sa_handler == SIG_DFL) {
			if (current->pid == 1)
				continue;
			switch (signr) {
			case SIGCONT: case SIGCHLD: case SIGWINCH:
				continue;

			case SIGTSTP: case SIGTTIN: case SIGTTOU:
				if (is_orphaned_pgrp(current->pgrp))
					continue;
			case SIGSTOP:
				if (current->flags & PF_PTRACED)
					continue;
				current->state = TASK_STOPPED;
				current->exit_code = signr;
				if (!(current->p_pptr->sig->action[SIGCHLD-1].sa_flags & 
						SA_NOCLDSTOP))
					notify_parent(current);
				schedule();
				continue;

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGABRT: case SIGFPE: case SIGSEGV:
			case SIGBUS:
				lock_kernel();
				if (current->binfmt && current->binfmt->core_dump) {
					if (current->binfmt->core_dump(signr, regs))
						signr |= 0x80;
				}
				unlock_kernel();
				/* fall through */
			default:
				spin_lock_irq(&current->sigmask_lock);
				current->signal |= _S(signr & 0x7f);
				spin_unlock_irq(&current->sigmask_lock);

				current->flags |= PF_SIGNALED;

				lock_kernel(); /* 8-( */
				do_exit(signr);
				unlock_kernel();
			}
		}
		/*
		 * OK, we're invoking a handler
		 */
		if(r0)
			syscall_restart(r0, regs->orig_reg2,
					r7, regs, sa);
		handle_signal(signr, sa, oldmask, regs);
		return 1;
	}
	/*
	 * Who's code doesn't conform to the restartable syscall convention
	 * dies here!!!  The li instruction, a single machine instruction,
	 * must directly be followed by the syscall instruction.
	 */
	if (r0 &&
	    (regs->regs[2] == ERESTARTNOHAND ||
	     regs->regs[2] == ERESTARTSYS ||
	     regs->regs[2] == ERESTARTNOINTR)) {
		regs->regs[0] = regs->regs[2] = regs->orig_reg2;
		regs->regs[7] = r7;
		regs->cp0_epc -= 8;
	}
	return 0;
}

/*
 * The signal(2) syscall is no longer available in the kernel
 * because GNU libc doesn't use it.  Maybe I'll add it back to the
 * kernel for the binary compatibility stuff.
 */
asmlinkage unsigned long sys_signal(int signum, __sighandler_t handler)
{
	return -ENOSYS;
}

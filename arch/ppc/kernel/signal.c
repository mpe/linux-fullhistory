/*
 *  linux/arch/ppc/kernel/signal.c
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/i386/kernel/signal.c"
 *    Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

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
#include <linux/elf.h>
#include <asm/uaccess.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

#define DEBUG_SIGNALS
#undef  DEBUG_SIGNALS

#define PAUSE_AFTER_SIGNAL
#undef  PAUSE_AFTER_SIGNAL

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

asmlinkage int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options);

/*
 * atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int sys_sigsuspend(unsigned long set, int p2, int p3, int p4, int p6, int p7, struct pt_regs *regs)
{
	unsigned long mask;

	spin_lock_irq(&current->sigmask_lock);
	mask = current->blocked;
	current->blocked = set & _BLOCKABLE;
	spin_unlock_irq(&current->sigmask_lock);

	regs->gpr[3] = -EINTR;
#ifdef DEBUG_SIGNALS
printk("Task: %x[%d] - SIGSUSPEND at %x, Mask: %x\n", current, current->pid, regs->nip, set);	
#endif
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(mask,regs)) {
			/*
			 * If a signal handler needs to be called,
			 * do_signal() has set R3 to the signal number (the
			 * first argument of the signal handler), so don't
			 * overwrite that with EINTR !
			 * In the other cases, do_signal() doesn't touch 
			 * R3, so it's still set to -EINTR (see above).
			 */
			return regs->gpr[3];
		}
	}
}

/* 
 * These are the flags in the MSR that the user is allowed to change
 * by modifying the saved value of the MSR on the stack.  SE and BE
 * should not be in this list since gdb may want to change these.  I.e,
 * you should be able to step out of a signal handler to see what
 * instruction executes next after the signal handler completes.
 * Alternately, if you stepped into a signal handler, you should be
 * able to continue 'til the next breakpoint from within the signal
 * handler, even if the handler returns.
 */
#define MSR_USERCHANGE	(MSR_FE0 | MSR_FE1)

/*
 * This sets regs->esp even though we don't actually use sigstacks yet..
 */
asmlinkage int sys_sigreturn(struct pt_regs *regs)
{
	struct sigcontext_struct *sc, sigctx;
	int ret;
	elf_gregset_t saved_regs;  /* an array of ELF_NGREG unsigned longs */

	sc = (struct sigcontext_struct *)(regs->gpr[1] + __SIGNAL_FRAMESIZE);
	if (copy_from_user(&sigctx, sc, sizeof(sigctx)))
		goto badframe;
	current->blocked = sigctx.oldmask & _BLOCKABLE;
	sc++;			/* Pop signal 'context' */
#ifdef DEBUG_SIGNALS
	printk("Sig return - Regs: %p, sc: %p, sig: %d\n", sigctx.regs, sc,
	       sigctx.signal);
#endif
	if (sc == (struct sigcontext_struct *)(sigctx.regs)) {
		/* Last stacked signal - restore registers */
		if (last_task_used_math == current)
			giveup_fpu();
		if (copy_from_user(saved_regs, sigctx.regs, sizeof(saved_regs)))
			goto badframe;
		saved_regs[PT_MSR] = (regs->msr & ~MSR_USERCHANGE)
			| (saved_regs[PT_MSR] & MSR_USERCHANGE);
		memcpy(regs, saved_regs, 
		       MIN(sizeof(elf_gregset_t),sizeof(struct pt_regs)));

		if (copy_from_user(current->tss.fpr,
				   (unsigned long *)sigctx.regs + ELF_NGREG,
				   ELF_NFPREG * sizeof(double)))
			goto badframe;

		if (regs->trap == 0x0C00 /* System Call! */ &&
		    ((int)regs->result == -ERESTARTNOHAND ||
		     (int)regs->result == -ERESTARTSYS ||
		     (int)regs->result == -ERESTARTNOINTR)) {
			regs->gpr[3] = regs->orig_gpr3;
			regs->nip -= 4; /* Back up & retry system call */
			regs->result = 0;
		}
		ret = regs->result;

	} else {
		/* More signals to go */
		regs->gpr[1] = (unsigned long)sc - __SIGNAL_FRAMESIZE;
		if (copy_from_user(&sigctx, sc, sizeof(sigctx)))
			goto badframe;
		regs->gpr[3] = ret = sigctx.signal;
		regs->gpr[4] = (unsigned long) sigctx.regs;
		regs->link = regs->gpr[4] + ELF_NGREG * sizeof(unsigned long)
			+ ELF_NFPREG * sizeof(double);
		regs->nip = sigctx.handler;
	}
	return ret;

badframe:
	lock_kernel();
	do_exit(SIGSEGV);
	unlock_kernel();
	return -EFAULT;
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
	unsigned long mask;
	unsigned long handler_signal = 0;
	unsigned long *frame = NULL;
	unsigned long *trampoline;
	unsigned long *regs_ptr;
	double *fpregs_ptr;
	unsigned long nip = 0;
	unsigned long signr;
	struct sigcontext_struct *sc;
	struct sigaction * sa;
	int bitno;

	mask = ~current->blocked;
	while ((signr = current->signal & mask)) {
#if 0
		signr = ffz(~signr);  /* Compute bit # */
#else
		for (bitno = 0;  bitno < 32;  bitno++)
			if (signr & (1<<bitno))
				break;
		signr = bitno;
#endif
		current->signal &= ~(1<<signr);  /* Clear bit */
		sa = current->sig->action + signr;
		signr++;
		if ((current->flags & PF_PTRACED) && signr != SIGKILL) {
			current->exit_code = signr;
			current->state = TASK_STOPPED;
			notify_parent(current, SIGCHLD);
			schedule();
			if (!(signr = current->exit_code))
				continue;
			current->exit_code = 0;
			if (signr == SIGSTOP)
				continue;
			if (_S(signr) & current->blocked) {
				current->signal |= _S(signr);
				spin_lock_irq(&current->sigmask_lock);
				spin_unlock_irq(&current->sigmask_lock);
				continue;
			}
			sa = current->sig->action + signr - 1;
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
					notify_parent(current, SIGCHLD);
				schedule();
				continue;

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGIOT: case SIGFPE: case SIGSEGV:
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
		if (regs->trap == 0x0C00 /* System Call! */) {
			if ((int)regs->result == -ERESTARTNOHAND ||
			    ((int)regs->result == -ERESTARTSYS &&
			     !(sa->sa_flags & SA_RESTART)))
				(int)regs->result = -EINTR;
		}
		handler_signal |= 1 << (signr-1);
		mask &= ~sa->sa_mask;
	}

	if (regs->trap == 0x0C00 /* System Call! */ &&
	    ((int)regs->result == -ERESTARTNOHAND ||
	     (int)regs->result == -ERESTARTSYS ||
	     (int)regs->result == -ERESTARTNOINTR)) {
		regs->gpr[3] = regs->orig_gpr3;
		regs->nip -= 4; /* Back up & retry system call */
		regs->result = 0;
	}

	if (!handler_signal)	/* no handler will be called - return 0 */
		return 0;

	nip = regs->nip;
	frame = (unsigned long *) regs->gpr[1];

	/*
	 * Build trampoline code on stack, and save gp and fp regs.
	 * The 56 word hole is because programs using the rs6000/xcoff
	 * style calling sequence can save up to 19 gp regs and 18 fp regs
	 * on the stack before decrementing sp.
	 */
	frame -= 2 + 56;
	trampoline = frame;
	frame -= ELF_NFPREG * sizeof(double) / sizeof(unsigned long);
	fpregs_ptr = (double *) frame;
	frame -= ELF_NGREG;
	regs_ptr = frame;
	/* verify stack is valid for writing to */
	if (verify_area(VERIFY_WRITE, frame,
			(ELF_NGREG + 2) * sizeof(long)
			+ ELF_NFPREG * sizeof(double)))
		goto badframe;
	if (last_task_used_math == current)
		giveup_fpu();
	if (__copy_to_user(regs_ptr, regs, 
	                   MIN(sizeof(elf_gregset_t),sizeof(struct pt_regs)))
	    || __copy_to_user(fpregs_ptr, current->tss.fpr,
			      ELF_NFPREG * sizeof(double))
	    || __put_user(0x38007777UL, trampoline)	/* li r0,0x7777 */
	    || __put_user(0x44000002UL, trampoline+1))	/* sc           */
		goto badframe;

	signr = 1;
	sa = current->sig->action;

	for (mask = 1 ; mask ; sa++,signr++,mask += mask) {
		if (mask > handler_signal)
			break;
		if (!(mask & handler_signal))
			continue;

		frame -= sizeof(struct sigcontext_struct) / sizeof(long);
		if (verify_area(VERIFY_WRITE, frame,
				sizeof(struct sigcontext_struct)))
			goto badframe;
		sc = (struct sigcontext_struct *)frame;
		nip = (unsigned long) sa->sa_handler;
		if (sa->sa_flags & SA_ONESHOT)
			sa->sa_handler = NULL;
		if (__put_user(nip, &sc->handler)
		    || __put_user(oldmask, &sc->oldmask)
		    || __put_user(regs_ptr, &sc->regs)
		    || __put_user(signr, &sc->signal))
			goto badframe;
		current->blocked |= sa->sa_mask;
		regs->gpr[3] = signr;
		regs->gpr[4] = (unsigned long) regs_ptr;
	}

	frame -= __SIGNAL_FRAMESIZE / sizeof(unsigned long);
	if (put_user(regs->gpr[1], frame))
		goto badframe;
	regs->link = (unsigned long)trampoline;
	regs->nip = nip;
	regs->gpr[1] = (unsigned long) frame;

	/* The DATA cache must be flushed here to insure coherency */
	/* between the DATA & INSTRUCTION caches.  Since we just */
	/* created an instruction stream using the DATA [cache] space */
	/* and since the instruction cache will not look in the DATA */
	/* cache for new data, we have to force the data to go on to */
	/* memory and flush the instruction cache to force it to look */
	/* there.  The following function performs this magic */
	flush_icache_range((unsigned long) trampoline,
			   (unsigned long) (trampoline + 2));
	return 1;

badframe:
	lock_kernel();
	do_exit(SIGSEGV);
	unlock_kernel();
	return 0;
}

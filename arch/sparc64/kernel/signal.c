/*  $Id: signal.c,v 1.20 1997/07/14 03:10:28 davem Exp $
 *  arch/sparc64/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1997 Eddie C. Dost   (ecd@skynet.be)
 *  Copyright (C) 1997 Jakub Jelinek   (jj@sunsite.mff.cuni.cz)
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/mm.h>

#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/ptrace.h>
#include <asm/svr4.h>
#include <asm/pgtable.h>
#include <asm/fpumacro.h>
#include <asm/uctx.h>
#include <asm/smp_lock.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

asmlinkage int sys_wait4(pid_t pid, unsigned long *stat_addr,
			 int options, unsigned long *ru);

asmlinkage int do_signal(unsigned long oldmask, struct pt_regs * regs,
			 unsigned long orig_o0, int ret_from_syscall);

/* This turned off for production... */
/* #define DEBUG_SIGNALS 1 */

/* {set, get}context() needed for 64-bit SparcLinux userland. */
asmlinkage void sparc64_set_context(struct pt_regs *regs)
{
	struct ucontext *ucp = (struct ucontext *) regs->u_regs[UREG_I0];
	struct thread_struct *tp = &current->tss;
	mc_gregset_t *grp;
	unsigned long pc, npc, tstate;
	unsigned long fp, i7;
	unsigned char fenab;

	__asm__ __volatile__("flushw");
	if(tp->w_saved						||
	   (((unsigned long)ucp) & (sizeof(unsigned long)-1))	||
	   (!__access_ok((unsigned long)ucp, sizeof(*ucp))))
		do_exit(SIGSEGV);
	grp = &ucp->uc_mcontext.mc_gregs;
	__get_user(pc, &((*grp)[MC_PC]));
	__get_user(npc, &((*grp)[MC_NPC]));
	if((pc | npc) & 3)
		do_exit(SIGSEGV);
	if(regs->u_regs[UREG_I1]) {
		__get_user(current->blocked, &ucp->uc_sigmask);
		current->blocked &= _BLOCKABLE;
	}
	regs->tpc = pc;
	regs->tnpc = npc;
	__get_user(regs->y, &((*grp)[MC_Y]));
	__get_user(tstate, &((*grp)[MC_Y]));
	regs->tstate &= ~(TSTATE_ICC | TSTATE_XCC);
	regs->tstate |= (tstate & (TSTATE_ICC | TSTATE_XCC));
	__get_user(regs->u_regs[UREG_G1], (&(*grp)[MC_G1]));
	__get_user(regs->u_regs[UREG_G2], (&(*grp)[MC_G2]));
	__get_user(regs->u_regs[UREG_G3], (&(*grp)[MC_G3]));
	__get_user(regs->u_regs[UREG_G4], (&(*grp)[MC_G4]));
	__get_user(regs->u_regs[UREG_G5], (&(*grp)[MC_G5]));
	__get_user(regs->u_regs[UREG_G6], (&(*grp)[MC_G6]));
	__get_user(regs->u_regs[UREG_G7], (&(*grp)[MC_G7]));
	__get_user(regs->u_regs[UREG_I0], (&(*grp)[MC_O0]));
	__get_user(regs->u_regs[UREG_I1], (&(*grp)[MC_O1]));
	__get_user(regs->u_regs[UREG_I2], (&(*grp)[MC_O2]));
	__get_user(regs->u_regs[UREG_I3], (&(*grp)[MC_O3]));
	__get_user(regs->u_regs[UREG_I4], (&(*grp)[MC_O4]));
	__get_user(regs->u_regs[UREG_I5], (&(*grp)[MC_O5]));
	__get_user(regs->u_regs[UREG_I6], (&(*grp)[MC_O6]));
	__get_user(regs->u_regs[UREG_I7], (&(*grp)[MC_O7]));

	__get_user(fp, &(ucp->uc_mcontext.mc_fp));
	__get_user(i7, &(ucp->uc_mcontext.mc_i7));
	__put_user(fp, (&(((struct reg_window *)(STACK_BIAS+regs->u_regs[UREG_I6]))->ins[6])));
	__put_user(i7, (&(((struct reg_window *)(STACK_BIAS+regs->u_regs[UREG_I6]))->ins[7])));

	__get_user(fenab, &(ucp->uc_mcontext.mc_fpregs.mcfpu_enab));
	if(fenab) {
		unsigned long *fpregs = (unsigned long *)(regs+1);
		copy_from_user(fpregs, &(ucp->uc_mcontext.mc_fpregs.mcfpu_fregs),
			       (sizeof(unsigned long) * 32));
		__get_user(fpregs[32], &(ucp->uc_mcontext.mc_fpregs.mcfpu_fsr));
		__get_user(fpregs[33], &(ucp->uc_mcontext.mc_fpregs.mcfpu_gsr));
		regs->fprs = FPRS_FEF;
	}
}

asmlinkage void sparc64_get_context(struct pt_regs *regs)
{
	struct ucontext *ucp = (struct ucontext *) regs->u_regs[UREG_I0];
	struct thread_struct *tp = &current->tss;
	mc_gregset_t *grp;
	mcontext_t *mcp;
	unsigned long fp, i7;
	unsigned char fenab = (current->flags & PF_USEDFPU);

	synchronize_user_stack();
	if(tp->w_saved || clear_user(ucp, sizeof(*ucp)))
		do_exit(SIGSEGV);
	mcp = &ucp->uc_mcontext;
	grp = &mcp->mc_gregs;

	/* Skip over the trap instruction, first. */
	regs->tpc   = regs->tnpc;
	regs->tnpc += 4;

	__put_user(current->blocked, &ucp->uc_sigmask);
	__put_user(regs->tstate, &((*grp)[MC_TSTATE]));
	__put_user(regs->tpc, &((*grp)[MC_PC]));
	__put_user(regs->tnpc, &((*grp)[MC_NPC]));
	__put_user(regs->y, &((*grp)[MC_Y]));
	__put_user(regs->u_regs[UREG_G1], &((*grp)[MC_G1]));
	__put_user(regs->u_regs[UREG_G2], &((*grp)[MC_G2]));
	__put_user(regs->u_regs[UREG_G3], &((*grp)[MC_G3]));
	__put_user(regs->u_regs[UREG_G4], &((*grp)[MC_G4]));
	__put_user(regs->u_regs[UREG_G5], &((*grp)[MC_G5]));
	__put_user(regs->u_regs[UREG_G6], &((*grp)[MC_G6]));
	__put_user(regs->u_regs[UREG_G6], &((*grp)[MC_G7]));
	__put_user(regs->u_regs[UREG_I0], &((*grp)[MC_O0]));
	__put_user(regs->u_regs[UREG_I1], &((*grp)[MC_O1]));
	__put_user(regs->u_regs[UREG_I2], &((*grp)[MC_O2]));
	__put_user(regs->u_regs[UREG_I3], &((*grp)[MC_O3]));
	__put_user(regs->u_regs[UREG_I4], &((*grp)[MC_O4]));
	__put_user(regs->u_regs[UREG_I5], &((*grp)[MC_O5]));
	__put_user(regs->u_regs[UREG_I6], &((*grp)[MC_O6]));
	__put_user(regs->u_regs[UREG_I7], &((*grp)[MC_O7]));

	__get_user(fp, (&(((struct reg_window *)(STACK_BIAS+regs->u_regs[UREG_I6]))->ins[6])));
	__get_user(i7, (&(((struct reg_window *)(STACK_BIAS+regs->u_regs[UREG_I6]))->ins[7])));
	__put_user(fp, &(mcp->mc_fp));
	__put_user(i7, &(mcp->mc_i7));

	__put_user(fenab, &(mcp->mc_fpregs.mcfpu_enab));
	if(fenab) {
		unsigned long *fpregs = (unsigned long *)(regs+1);
		copy_to_user(&(mcp->mc_fpregs.mcfpu_fregs), fpregs,
			     (sizeof(unsigned long) * 32));
		__put_user(fpregs[32], &(mcp->mc_fpregs.mcfpu_fsr));
		__put_user(fpregs[33], &(mcp->mc_fpregs.mcfpu_gsr));
		__put_user(FPRS_FEF, &(mcp->mc_fpregs.mcfpu_fprs));
	}
}

/* 
 * The new signal frame, intended to be used for Linux applications only
 * (we have enough in there to work with clone).
 * All the interesting bits are in the info field.
 */

struct new_signal_frame {
	struct sparc_stackf	ss;
	__siginfo_t		info;
	__siginfo_fpu_t *	fpu_save;
	unsigned int		insns [2];
	__siginfo_fpu_t		fpu_state;
};

/* Align macros */
#define NF_ALIGNEDSZ  (((sizeof(struct new_signal_frame) + 7) & (~7)))

/*
 * atomically swap in the new signal mask, and wait for a signal.
 * This is really tricky on the Sparc, watch out...
 */
asmlinkage void _sigpause_common(unsigned int set, struct pt_regs *regs)
{
	unsigned long mask;

#ifdef CONFIG_SPARC32_COMPAT
	if (current->tss.flags & SPARC_FLAG_32BIT) {
		extern asmlinkage void _sigpause32_common(unsigned int,
							  struct pt_regs *);
		_sigpause32_common(set, regs);
		return;
	}
#endif
	spin_lock_irq(&current->sigmask_lock);
	mask = current->blocked;
	current->blocked = set & _BLOCKABLE;
	spin_unlock_irq(&current->sigmask_lock);
	
	regs->tpc = regs->tnpc;
	regs->tnpc += 4;

	/* Condition codes and return value where set here for sigpause,
	 * and so got used by setup_frame, which again causes sigreturn()
	 * to return -EINTR.
	 */
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		/*
		 * Return -EINTR and set condition code here,
		 * so the interrupted system call actually returns
		 * these.
		 */
		regs->tstate |= (TSTATE_ICARRY|TSTATE_XCARRY);
		regs->u_regs[UREG_I0] = EINTR;
		if (do_signal(mask, regs, 0, 0))
			return;
	}
}

asmlinkage void do_sigpause(unsigned int set, struct pt_regs *regs)
{
	_sigpause_common(set, regs);
}

asmlinkage void do_sigsuspend(struct pt_regs *regs)
{
	_sigpause_common(regs->u_regs[UREG_I0], regs);
}


static inline void
restore_fpu_state(struct pt_regs *regs, __siginfo_fpu_t *fpu)
{
	unsigned long *fpregs = (unsigned long *)(regs+1);
	copy_from_user(fpregs, &fpu->si_float_regs[0],
		       (sizeof(unsigned int) * 64));
	__get_user(fpregs[32], &fpu->si_fsr);
	__get_user(fpregs[33], &fpu->si_gsr);
	regs->fprs = FPRS_FEF;
}

void do_sigreturn(struct pt_regs *regs)
{
	struct new_signal_frame *sf;
	unsigned long tpc, tnpc, tstate;
	__siginfo_fpu_t *fpu_save;
	unsigned long mask;

#ifdef CONFIG_SPARC32_COMPAT
	if (current->tss.flags & SPARC_FLAG_32BIT) {
		extern asmlinkage void do_sigreturn32(struct pt_regs *);
		return do_sigreturn32(regs);
	}
#endif	
	synchronize_user_stack ();
	sf = (struct new_signal_frame *)
		(regs->u_regs [UREG_FP] + STACK_BIAS);

	/* 1. Make sure we are not getting garbage from the user */
	if (verify_area (VERIFY_READ, sf, sizeof (*sf)))
		goto segv;

	if (((unsigned long) sf) & 3)
		goto segv;

	get_user(tpc, &sf->info.si_regs.tpc);
	__get_user(tnpc, &sf->info.si_regs.tnpc);
	if ((tpc | tnpc) & 3)
		goto segv;

	regs->tpc = tpc;
	regs->tnpc = tnpc;

	/* 2. Restore the state */
	__get_user(regs->y, &sf->info.si_regs.y);
	__get_user(tstate, &sf->info.si_regs.tstate);
	copy_from_user(regs->u_regs, sf->info.si_regs.u_regs, sizeof(regs->u_regs));

	/* User can only change condition codes in %tstate. */
	regs->tstate &= ~(TSTATE_ICC);
	regs->tstate |= (tstate & TSTATE_ICC);

	__get_user(fpu_save, &sf->fpu_save);
	if (fpu_save)
		restore_fpu_state(regs, &sf->fpu_state);
	__get_user(mask, &sf->info.si_mask);
	current->blocked = mask & _BLOCKABLE;
	return;
segv:
	lock_kernel();
	do_exit(SIGSEGV);
}

/* Checks if the fp is valid */
static int invalid_frame_pointer(void *fp, int fplen)
{
	if ((((unsigned long) fp) & 7) || ((unsigned long)fp) > 0x80000000000ULL - fplen)
		return 1;
	return 0;
}

static inline void
save_fpu_state(struct pt_regs *regs, __siginfo_fpu_t *fpu)
{
	unsigned long *fpregs = (unsigned long *)(regs+1);
	copy_to_user(&fpu->si_float_regs[0], fpregs,
		     (sizeof(unsigned int) * 64));
	__put_user(fpregs[32], &fpu->si_fsr);
	__put_user(fpregs[33], &fpu->si_gsr);
	regs->fprs = 0;
}

static inline void
new_setup_frame(struct sigaction *sa, struct pt_regs *regs,
		  int signo, unsigned long oldmask)
{
	struct new_signal_frame *sf;
	int sigframe_size;

	/* 1. Make sure everything is clean */
	synchronize_user_stack();
	sigframe_size = NF_ALIGNEDSZ;
	if (!(current->flags & PF_USEDFPU))
		sigframe_size -= sizeof(__siginfo_fpu_t);

	sf = (struct new_signal_frame *)
		(regs->u_regs[UREG_FP] + STACK_BIAS - sigframe_size);
	
	if (invalid_frame_pointer (sf, sigframe_size))
		goto sigill;

	if (current->tss.w_saved != 0) {
		printk ("%s[%d]: Invalid user stack frame for "
			"signal delivery.\n", current->comm, current->pid);
		goto sigill;
	}

	/* 2. Save the current process state */
	copy_to_user(&sf->info.si_regs, regs, sizeof (*regs));

	if (current->flags & PF_USEDFPU) {
		save_fpu_state(regs, &sf->fpu_state);
		__put_user((u64)&sf->fpu_state, &sf->fpu_save);
	} else {
		__put_user(0, &sf->fpu_save);
	}

	__put_user(oldmask, &sf->info.si_mask);

	copy_in_user((u64 *)sf,
		     (u64 *)(regs->u_regs[UREG_FP]+STACK_BIAS),
		     sizeof(struct reg_window));
	
	/* 3. return to kernel instructions */
	__put_user(0x821020d8, &sf->insns[0]); /* mov __NR_sigreturn, %g1 */
	__put_user(0x91d02011, &sf->insns[1]); /* t 0x11 */

	/* 4. signal handler back-trampoline and parameters */
	regs->u_regs[UREG_FP] = ((unsigned long) sf) - STACK_BIAS;
	regs->u_regs[UREG_I0] = signo;
	regs->u_regs[UREG_I1] = (unsigned long) &sf->info;
	regs->u_regs[UREG_I7] = (unsigned long) (&(sf->insns[0]) - 2);

	/* 5. signal handler */
	regs->tpc = (unsigned long) sa->sa_handler;
	regs->tnpc = (regs->tpc + 4);

	/* Flush instruction space. */
	{
		unsigned long address = ((unsigned long)&(sf->insns[0]));
		pgd_t *pgdp = pgd_offset(current->mm, address);
		pmd_t *pmdp = pmd_offset(pgdp, address);
		pte_t *ptep = pte_offset(pmdp, address);

		if(pte_present(*ptep)) {
			unsigned long page = pte_page(*ptep);

			__asm__ __volatile__("
			membar	#StoreStore
			flush	%0 + %1"
			: : "r" (page), "r" (address & (PAGE_SIZE - 1))
			: "memory");
		}
	}
	return;

sigill:
	lock_kernel();
	do_exit(SIGILL);
}

static inline void handle_signal(unsigned long signr, struct sigaction *sa,
				 unsigned long oldmask, struct pt_regs *regs)
{
	new_setup_frame(sa, regs, signr, oldmask);
	if(sa->sa_flags & SA_ONESHOT)
		sa->sa_handler = NULL;
	if(!(sa->sa_flags & SA_NOMASK)) {
		spin_lock_irq(&current->sigmask_lock);
		current->blocked |= (sa->sa_mask | _S(signr)) & _BLOCKABLE;
		spin_unlock_irq(&current->sigmask_lock);
	}
}

static inline void syscall_restart(unsigned long orig_i0, struct pt_regs *regs,
				     struct sigaction *sa)
{
	switch(regs->u_regs[UREG_I0]) {
		case ERESTARTNOHAND:
		no_system_call_restart:
			regs->u_regs[UREG_I0] = EINTR;
			regs->tstate |= (TSTATE_ICARRY|TSTATE_XCARRY);
			break;
		case ERESTARTSYS:
			if(!(sa->sa_flags & SA_RESTART))
				goto no_system_call_restart;
		/* fallthrough */
		case ERESTARTNOINTR:
			regs->u_regs[UREG_I0] = orig_i0;
			regs->tpc -= 4;
			regs->tnpc -= 4;
	}
}

/* Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
asmlinkage int do_signal(unsigned long oldmask, struct pt_regs * regs,
			   unsigned long orig_i0, int restart_syscall)
{
	unsigned long signr, mask = ~current->blocked;
	struct sigaction *sa;

#ifdef CONFIG_SPARC32_COMPAT
	if (current->tss.flags & SPARC_FLAG_32BIT) {
		extern asmlinkage int do_signal32(unsigned long, struct pt_regs *,
						  unsigned long, int);
		return do_signal32(oldmask, regs, orig_i0, restart_syscall);
	}
#endif	
	while ((signr = current->signal & mask) != 0) {
		signr = ffz(~signr);

		spin_lock_irq(&current->sigmask_lock);
		current->signal &= ~(1 << signr);
		spin_unlock_irq(&current->sigmask_lock);

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
		if(sa->sa_handler == SIG_IGN) {
			if(signr != SIGCHLD)
				continue;

                        /* sys_wait4() grabs the master kernel lock, so
                         * we need not do so, that sucker should be
                         * threaded and would not be that difficult to
                         * do anyways.
                         */
                        while(sys_wait4(-1, NULL, WNOHANG, NULL) > 0)
                                ;
			continue;
		}
		if(sa->sa_handler == SIG_DFL) {
			if(current->pid == 1)
				continue;
			switch(signr) {
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
				if(!(current->p_pptr->sig->action[SIGCHLD-1].sa_flags &
				     SA_NOCLDSTOP))
					notify_parent(current);
				schedule();
				continue;

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGABRT: case SIGFPE: case SIGSEGV: case SIGBUS:
				if(current->binfmt && current->binfmt->core_dump) {
					lock_kernel();
					if(current->binfmt->core_dump(signr, regs))
						signr |= 0x80;
					unlock_kernel();
				}
#ifdef DEBUG_SIGNALS
				/* Very useful to debug dynamic linker problems */
				printk ("Sig ILL going...\n");
				show_regs (regs);
#endif
				/* fall through */
			default:
				spin_lock_irq(&current->sigmask_lock);
				current->signal |= _S(signr & 0x7f);
				spin_unlock_irq(&current->sigmask_lock);

				current->flags |= PF_SIGNALED;

				lock_kernel();
				do_exit(signr);
				unlock_kernel();
			}
		}
		if(restart_syscall)
			syscall_restart(orig_i0, regs, sa);
		handle_signal(signr, sa, oldmask, regs);
		return 1;
	}
	if(restart_syscall &&
	   (regs->u_regs[UREG_I0] == ERESTARTNOHAND ||
	    regs->u_regs[UREG_I0] == ERESTARTSYS ||
	    regs->u_regs[UREG_I0] == ERESTARTNOINTR)) {
		/* replay the system call when we are done */
		regs->u_regs[UREG_I0] = orig_i0;
		regs->tpc -= 4;
		regs->tnpc -= 4;
	}
	return 0;
}

asmlinkage int
sys_sigstack(struct sigstack *ssptr, struct sigstack *ossptr)
{
	int ret = -EFAULT;

	lock_kernel();
	/* First see if old state is wanted. */
	if(ossptr) {
		if (put_user ((u64)current->tss.sstk_info.the_stack, &ossptr->the_stack) ||
		    __put_user (current->tss.sstk_info.cur_status, &ossptr->cur_status))
			goto out;
	}

	/* Now see if we want to update the new state. */
	if(ssptr) {
		if (get_user ((u64)current->tss.sstk_info.the_stack, &ssptr->the_stack) ||
		    __put_user (current->tss.sstk_info.cur_status, &ssptr->cur_status))
			goto out;
	}
	ret = 0;
out:
	unlock_kernel();
	return ret;
}

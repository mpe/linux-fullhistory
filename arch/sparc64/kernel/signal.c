/*  $Id: signal.c,v 1.27 1997/12/15 15:04:44 jj Exp $
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
#include <asm/siginfo.h>

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

asmlinkage int sys_wait4(pid_t pid, unsigned long *stat_addr,
			 int options, unsigned long *ru);

asmlinkage int do_signal(sigset_t *oldset, struct pt_regs * regs,
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
		goto do_sigsegv;
	grp = &ucp->uc_mcontext.mc_gregs;
	__get_user(pc, &((*grp)[MC_PC]));
	__get_user(npc, &((*grp)[MC_NPC]));
	if((pc | npc) & 3)
		goto do_sigsegv;
	if(regs->u_regs[UREG_I1]) {
		sigset_t set;

		if (_NSIG_WORDS == 1) {
			if (__get_user(set.sig[0], &ucp->uc_sigmask.sig[0]))
				goto do_sigsegv;
		} else {
			if (__copy_from_user(&set, &ucp->uc_sigmask, sizeof(sigset_t)))
				goto do_sigsegv;
		}
		sigdelsetmask(&set, ~_BLOCKABLE);
		spin_lock_irq(&current->sigmask_lock);
		current->blocked = set;
		recalc_sigpending(current);
		spin_unlock_irq(&current->sigmask_lock);
	}
	regs->tpc = pc;
	regs->tnpc = npc;
	__get_user(regs->y, &((*grp)[MC_Y]));
	__get_user(tstate, &((*grp)[MC_TSTATE]));
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
		unsigned long fprs;
		__get_user(fprs, &(ucp->uc_mcontext.mc_fpregs.mcfpu_fprs));
		if (fprs & FPRS_DL)
			copy_from_user(fpregs, &(ucp->uc_mcontext.mc_fpregs.mcfpu_fregs),
			       	       (sizeof(unsigned int) * 32));
		if (fprs & FPRS_DU)
			copy_from_user(fpregs+16, ((unsigned long *)&(ucp->uc_mcontext.mc_fpregs.mcfpu_fregs))+16,
			       	       (sizeof(unsigned int) * 32));
		__get_user(fpregs[32], &(ucp->uc_mcontext.mc_fpregs.mcfpu_fsr));
		__get_user(fpregs[33], &(ucp->uc_mcontext.mc_fpregs.mcfpu_gsr));
		regs->fprs = fprs;
		regs->tstate |= TSTATE_PEF;
	}
	return;
do_sigsegv:
	lock_kernel();
	do_exit(SIGSEGV);
}

asmlinkage void sparc64_get_context(struct pt_regs *regs)
{
	struct ucontext *ucp = (struct ucontext *) regs->u_regs[UREG_I0];
	struct thread_struct *tp = &current->tss;
	mc_gregset_t *grp;
	mcontext_t *mcp;
	unsigned long fp, i7;
	unsigned char fenab = (current->tss.flags & SPARC_FLAG_USEDFPU);

	synchronize_user_stack();
	if(tp->w_saved || clear_user(ucp, sizeof(*ucp)))
		goto do_sigsegv;
	mcp = &ucp->uc_mcontext;
	grp = &mcp->mc_gregs;

	/* Skip over the trap instruction, first. */
	regs->tpc   = regs->tnpc;
	regs->tnpc += 4;

	if (_NSIG_WORDS == 1)
		__put_user(current->blocked.sig[0], (unsigned long *)&ucp->uc_sigmask);
	else
		__copy_to_user(&ucp->uc_sigmask, &current->blocked, sizeof(sigset_t));

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
		unsigned long fprs;
		
		fprs = (regs->fprs & FPRS_FEF) | 
		       (current->tss.flags & (SPARC_FLAG_USEDFPUL | SPARC_FLAG_USEDFPUU));
		if (fprs & FPRS_DL)
			copy_to_user(&(mcp->mc_fpregs.mcfpu_fregs), fpregs,
				     (sizeof(unsigned int) * 32));
		else
			clear_user(&(mcp->mc_fpregs.mcfpu_fregs),
				     (sizeof(unsigned int) * 32));
		if (fprs & FPRS_DU)
			copy_to_user(((unsigned long *)&(mcp->mc_fpregs.mcfpu_fregs))+16, fpregs+16,
				     (sizeof(unsigned int) * 32));
		else
			clear_user(((unsigned long *)&(mcp->mc_fpregs.mcfpu_fregs))+16,
				     (sizeof(unsigned int) * 32));
		__put_user(fpregs[32], &(mcp->mc_fpregs.mcfpu_fsr));
		__put_user(fpregs[33], &(mcp->mc_fpregs.mcfpu_gsr));
		__put_user(fprs, &(mcp->mc_fpregs.mcfpu_fprs));
	}
	return;
do_sigsegv:
	lock_kernel();
	do_exit(SIGSEGV);
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
	unsigned long		extramask[_NSIG_WORDS-1];
	__siginfo_fpu_t		fpu_state;
};

struct rt_signal_frame {
	struct sparc_stackf	ss;
	siginfo_t		info;
	struct pt_regs		regs;
	sigset_t		mask;
	__siginfo_fpu_t *	fpu_save;
	unsigned int		insns [2];
	__siginfo_fpu_t		fpu_state;
};

/* Align macros */
#define NF_ALIGNEDSZ  (((sizeof(struct new_signal_frame) + 7) & (~7)))
#define RT_ALIGNEDSZ  (((sizeof(struct rt_signal_frame) + 7) & (~7)))

/*
 * atomically swap in the new signal mask, and wait for a signal.
 * This is really tricky on the Sparc, watch out...
 */
asmlinkage void _sigpause_common(old_sigset_t set, struct pt_regs *regs)
{
	sigset_t saveset;

#ifdef CONFIG_SPARC32_COMPAT
	if (current->tss.flags & SPARC_FLAG_32BIT) {
		extern asmlinkage void _sigpause32_common(old_sigset_t32,
							  struct pt_regs *);
		_sigpause32_common(set, regs);
		return;
	}
#endif
	set &= _BLOCKABLE;
	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	siginitset(&current->blocked, set);
	recalc_sigpending(current);
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
		if (do_signal(&saveset, regs, 0, 0))
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

asmlinkage void do_rt_sigsuspend(sigset_t *uset, size_t sigsetsize, struct pt_regs *regs)
{
	sigset_t oldset, set;
        
	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t)) {
		regs->tstate |= (TSTATE_ICARRY|TSTATE_XCARRY);
		regs->u_regs[UREG_I0] = EINVAL;
		return;
	}
	if (copy_from_user(&set, uset, sizeof(set))) {
		regs->tstate |= (TSTATE_ICARRY|TSTATE_XCARRY);
		regs->u_regs[UREG_I0] = EFAULT;
		return;
	}
                                                                
	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	oldset = current->blocked;
	current->blocked = set;
	recalc_sigpending(current);
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
		if (do_signal(&oldset, regs, 0, 0))
			return;
	}
}

static inline void
restore_fpu_state(struct pt_regs *regs, __siginfo_fpu_t *fpu)
{
	unsigned long *fpregs = (unsigned long *)(regs+1);
	unsigned long fprs;
	
	__get_user(fprs, &fpu->si_fprs);
	if (fprs & FPRS_DL)
		copy_from_user(fpregs, &fpu->si_float_regs[0],
		       	       (sizeof(unsigned int) * 32));
	if (fprs & FPRS_DU)
		copy_from_user(fpregs+16, &fpu->si_float_regs[32],
		       	       (sizeof(unsigned int) * 32));
	__get_user(fpregs[32], &fpu->si_fsr);
	__get_user(fpregs[33], &fpu->si_gsr);
	regs->fprs = fprs;
	regs->tstate |= TSTATE_PEF;
}

void do_sigreturn(struct pt_regs *regs)
{
	struct new_signal_frame *sf;
	unsigned long tpc, tnpc, tstate;
	__siginfo_fpu_t *fpu_save;
	sigset_t set;

	synchronize_user_stack ();
	sf = (struct new_signal_frame *)
		(regs->u_regs [UREG_FP] + STACK_BIAS);

	/* 1. Make sure we are not getting garbage from the user */
	if (((unsigned long) sf) & 3)
		goto segv;

	if (get_user(tpc, &sf->info.si_regs.tpc) ||
	    __get_user(tnpc, &sf->info.si_regs.tnpc) ||
	    ((tpc | tnpc) & 3))
		goto segv;

	regs->tpc = tpc;
	regs->tnpc = tnpc;

	/* 2. Restore the state */
	if (__get_user(regs->y, &sf->info.si_regs.y) ||
	    __get_user(tstate, &sf->info.si_regs.tstate) ||
	    copy_from_user(regs->u_regs, sf->info.si_regs.u_regs, sizeof(regs->u_regs)))
		goto segv;

	/* User can only change condition codes in %tstate. */
	regs->tstate &= ~(TSTATE_ICC);
	regs->tstate |= (tstate & TSTATE_ICC);

	if (__get_user(fpu_save, &sf->fpu_save))
		goto segv;
	if (fpu_save)
		restore_fpu_state(regs, &sf->fpu_state);
	if (__get_user(set.sig[0], &sf->info.si_mask) ||
	    (_NSIG_WORDS > 1 &&
	     __copy_from_user(&set.sig[1], &sf->extramask,
	      sizeof(sf->extramask))))
		goto segv;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	return;
segv:
	lock_kernel();
	do_exit(SIGSEGV);
}

void do_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_signal_frame *sf;
	unsigned long tpc, tnpc, tstate;
	__siginfo_fpu_t *fpu_save;
	sigset_t set;

	synchronize_user_stack ();
	sf = (struct rt_signal_frame *)
		(regs->u_regs [UREG_FP] + STACK_BIAS);

	/* 1. Make sure we are not getting garbage from the user */
	if (((unsigned long) sf) & 3)
		goto segv;

	if (get_user(tpc, &sf->regs.tpc) ||
	    __get_user(tnpc, &sf->regs.tnpc) ||
	    ((tpc | tnpc) & 3))
		goto segv;

	regs->tpc = tpc;
	regs->tnpc = tnpc;

	/* 2. Restore the state */
	if (__get_user(regs->y, &sf->regs.y) ||
	    __get_user(tstate, &sf->regs.tstate) ||
	    copy_from_user(regs->u_regs, sf->regs.u_regs, sizeof(regs->u_regs)))
		goto segv;

	/* User can only change condition codes in %tstate. */
	regs->tstate &= ~(TSTATE_ICC);
	regs->tstate |= (tstate & TSTATE_ICC);

	if (__get_user(fpu_save, &sf->fpu_save))
		goto segv;
	if (fpu_save)
		restore_fpu_state(regs, &sf->fpu_state);

	if (__copy_from_user(&set, &sf->mask, sizeof(sigset_t)))
		goto segv;
	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
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
	unsigned long fprs;
	
	fprs = (regs->fprs & FPRS_FEF) | 
		(current->tss.flags & (SPARC_FLAG_USEDFPUL | SPARC_FLAG_USEDFPUU));
	if (fprs & FPRS_DL)
		copy_to_user(&fpu->si_float_regs[0], fpregs,
		     	     (sizeof(unsigned int) * 32));
	else
		clear_user(&fpu->si_float_regs[0],
		     	     (sizeof(unsigned int) * 32));
	if (fprs & FPRS_DU)
		copy_to_user(&fpu->si_float_regs[32], fpregs+16,
		     	     (sizeof(unsigned int) * 32));
	else
		clear_user(&fpu->si_float_regs[32],
		     	     (sizeof(unsigned int) * 32));
	__put_user(fpregs[32], &fpu->si_fsr);
	__put_user(fpregs[33], &fpu->si_gsr);
	__put_user(fprs, &fpu->si_fprs);
	regs->tstate &= ~TSTATE_PEF;
}

static inline void
new_setup_frame(struct k_sigaction *ka, struct pt_regs *regs,
		  int signo, sigset_t *oldset)
{
	struct new_signal_frame *sf;
	int sigframe_size;

	/* 1. Make sure everything is clean */
	synchronize_user_stack();
	sigframe_size = NF_ALIGNEDSZ;
	if (!(current->tss.flags & SPARC_FLAG_USEDFPU))
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

	if (current->tss.flags & SPARC_FLAG_USEDFPU) {
		save_fpu_state(regs, &sf->fpu_state);
		__put_user((u64)&sf->fpu_state, &sf->fpu_save);
	} else {
		__put_user(0, &sf->fpu_save);
	}

	__put_user(oldset->sig[0], &sf->info.si_mask);
	if (_NSIG_WORDS > 1)
		__copy_to_user(sf->extramask, &oldset->sig[1], sizeof(sf->extramask));

	copy_in_user((u64 *)sf,
		     (u64 *)(regs->u_regs[UREG_FP]+STACK_BIAS),
		     sizeof(struct reg_window));
	
	/* 3. signal handler back-trampoline and parameters */
	regs->u_regs[UREG_FP] = ((unsigned long) sf) - STACK_BIAS;
	regs->u_regs[UREG_I0] = signo;
	regs->u_regs[UREG_I1] = (unsigned long) &sf->info;

	/* 5. signal handler */
	regs->tpc = (unsigned long) ka->sa.sa_handler;
	regs->tnpc = (regs->tpc + 4);

	/* 4. return to kernel instructions */
	if (ka->ka_restorer)
		regs->u_regs[UREG_I7] = (unsigned long)ka->ka_restorer;
	else {
		/* Flush instruction space. */
		unsigned long address = ((unsigned long)&(sf->insns[0]));
		pgd_t *pgdp = pgd_offset(current->mm, address);
		pmd_t *pmdp = pmd_offset(pgdp, address);
		pte_t *ptep = pte_offset(pmdp, address);

		regs->u_regs[UREG_I7] = (unsigned long) (&(sf->insns[0]) - 2);
		
		__put_user(0x821020d8, &sf->insns[0]); /* mov __NR_sigreturn, %g1 */
		__put_user(0x91d0206d, &sf->insns[1]); /* t 0x6d */

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

static inline void
setup_rt_frame(struct k_sigaction *ka, struct pt_regs *regs,
	       int signo, sigset_t *oldset, siginfo_t *info)
{
	struct rt_signal_frame *sf;
	int sigframe_size;

	/* 1. Make sure everything is clean */
	synchronize_user_stack();
	sigframe_size = RT_ALIGNEDSZ;
	if (!(current->tss.flags & SPARC_FLAG_USEDFPU))
		sigframe_size -= sizeof(__siginfo_fpu_t);

	sf = (struct rt_signal_frame *)
		(regs->u_regs[UREG_FP] + STACK_BIAS - sigframe_size);
	
	if (invalid_frame_pointer (sf, sigframe_size))
		goto sigill;

	if (current->tss.w_saved != 0) {
		printk ("%s[%d]: Invalid user stack frame for "
			"signal delivery.\n", current->comm, current->pid);
		goto sigill;
	}

	/* 2. Save the current process state */
	copy_to_user(&sf->regs, regs, sizeof (*regs));

	if (current->tss.flags & SPARC_FLAG_USEDFPU) {
		save_fpu_state(regs, &sf->fpu_state);
		__put_user((u64)&sf->fpu_state, &sf->fpu_save);
	} else {
		__put_user(0, &sf->fpu_save);
	}

	copy_to_user(&sf->mask, oldset, sizeof(sigset_t));

	copy_in_user((u64 *)sf,
		     (u64 *)(regs->u_regs[UREG_FP]+STACK_BIAS),
		     sizeof(struct reg_window));

	copy_to_user(&sf->info, info, sizeof(siginfo_t));
	
	/* 3. signal handler back-trampoline and parameters */
	regs->u_regs[UREG_FP] = ((unsigned long) sf) - STACK_BIAS;
	regs->u_regs[UREG_I0] = signo;
	regs->u_regs[UREG_I1] = (unsigned long) &sf->info;

	/* 5. signal handler */
	regs->tpc = (unsigned long) ka->sa.sa_handler;
	regs->tnpc = (regs->tpc + 4);

	/* 4. return to kernel instructions */
	if (ka->ka_restorer)
		regs->u_regs[UREG_I7] = (unsigned long)ka->ka_restorer;
	else {
		/* Flush instruction space. */
		unsigned long address = ((unsigned long)&(sf->insns[0]));
		pgd_t *pgdp = pgd_offset(current->mm, address);
		pmd_t *pmdp = pmd_offset(pgdp, address);
		pte_t *ptep = pte_offset(pmdp, address);

		regs->u_regs[UREG_I7] = (unsigned long) (&(sf->insns[0]) - 2);
		
		__put_user(0x82102065, &sf->insns[0]); /* mov __NR_rt_sigreturn, %g1 */
		__put_user(0x91d0206d, &sf->insns[1]); /* t 0x6d */

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

static inline void handle_signal(unsigned long signr, struct k_sigaction *ka,
				 siginfo_t *info,
				 sigset_t *oldset, struct pt_regs *regs)
{
	if(ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame(ka, regs, signr, oldset, info);
	else
		new_setup_frame(ka, regs, signr, oldset);
	if(ka->sa.sa_flags & SA_ONESHOT)
		ka->sa.sa_handler = SIG_DFL;
	if(!(ka->sa.sa_flags & SA_NOMASK)) {
		spin_lock_irq(&current->sigmask_lock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,signr);
		recalc_sigpending(current);
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
asmlinkage int do_signal(sigset_t *oldset, struct pt_regs * regs,
			 unsigned long orig_i0, int restart_syscall)
{
	unsigned long signr;
	siginfo_t info;
	struct k_sigaction *ka;
	
	if (!oldset)
		oldset = &current->blocked;

#ifdef CONFIG_SPARC32_COMPAT
	if (current->tss.flags & SPARC_FLAG_32BIT) {
		extern asmlinkage int do_signal32(sigset_t *, struct pt_regs *,
						  unsigned long, int);
		return do_signal32(oldset, regs, orig_i0, restart_syscall);
	}
#endif	
	for (;;) {
		spin_lock_irq(&current->sigmask_lock);
		signr = dequeue_signal(&current->blocked, &info);
		spin_unlock_irq(&current->sigmask_lock);
		
		if (!signr) break;

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

			/* Update the siginfo structure.  Is this good?  */
			if (signr != info.si_signo) {
				info.si_signo = signr;
				info.si_errno = 0;
				info.si_code = SI_USER;
				info.si_pid = current->p_pptr->pid;
				info.si_uid = current->p_pptr->uid;
			}

			/* If the (new) signal is now blocked, requeue it.  */
			if (sigismember(&current->blocked, signr)) {
				send_sig_info(signr, &info, current);
				continue;
			}
		}
		
		ka = &current->sig->action[signr-1];
		
		if(ka->sa.sa_handler == SIG_IGN) {
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
		if(ka->sa.sa_handler == SIG_DFL) {
			unsigned long exit_code = signr;
			
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
				if(!(current->p_pptr->sig->action[SIGCHLD-1].sa.sa_flags &
				     SA_NOCLDSTOP))
					notify_parent(current, SIGCHLD);
				schedule();
				continue;

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGABRT: case SIGFPE: case SIGSEGV: case SIGBUS:
				if(current->binfmt && current->binfmt->core_dump) {
					lock_kernel();
					if(current->binfmt->core_dump(signr, regs))
						exit_code |= 0x80;
					unlock_kernel();
				}
#ifdef DEBUG_SIGNALS
				/* Very useful to debug dynamic linker problems */
				printk ("Sig ILL going...\n");
				show_regs (regs);
#endif
				/* fall through */
			default:
				lock_kernel();
				sigaddset(&current->signal, signr);
				current->flags |= PF_SIGNALED;
				do_exit(exit_code);
				/* NOT REACHED */
			}
		}
		if(restart_syscall)
			syscall_restart(orig_i0, regs, &ka->sa);
		handle_signal(signr, ka, &info, oldset, regs);
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

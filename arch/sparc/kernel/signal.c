/*  $Id: signal.c,v 1.74 1997/05/15 19:57:09 davem Exp $
 *  linux/arch/sparc/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1997 Eddie C. Dost   (ecd@skynet.be)
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
#include <asm/bitops.h>
#include <asm/ptrace.h>
#include <asm/svr4.h>
#include <asm/pgtable.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

asmlinkage int sys_wait4(pid_t pid, unsigned long *stat_addr,
			 int options, unsigned long *ru);

extern void fpsave(unsigned long *fpregs, unsigned long *fsr,
		   void *fpqueue, unsigned long *fpqdepth);
extern void fpload(unsigned long *fpregs, unsigned long *fsr);

asmlinkage int do_signal(unsigned long oldmask, struct pt_regs * regs,
			 unsigned long orig_o0, int ret_from_syscall);

/* This turned off for production... */
/* #define DEBUG_SIGNALS 1 */

/* Signal frames: the original one (compatible with SunOS):
 *
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
	struct sigcontext *sig_scptr;
	int sig_address;
	struct sigcontext sig_context;
};

/* 
 * And the new one, intended to be used for Linux applications only
 * (we have enough in there to work with clone).
 * All the interesting bits are in the info field.
 */

struct new_signal_frame {
	struct sparc_stackf	ss;
	__siginfo_t		info;
	__siginfo_fpu_t		*fpu_save;
	unsigned long		insns [2] __attribute__ ((aligned (8)));
	__siginfo_fpu_t		fpu_state;
};

/* Align macros */
#define SF_ALIGNEDSZ  (((sizeof(struct signal_sframe) + 7) & (~7)))
#define NF_ALIGNEDSZ  (((sizeof(struct new_signal_frame) + 7) & (~7)))

/*
 * atomically swap in the new signal mask, and wait for a signal.
 * This is really tricky on the Sparc, watch out...
 */
asmlinkage void _sigpause_common(unsigned int set, struct pt_regs *regs)
{
	unsigned long mask;

	spin_lock_irq(&current->sigmask_lock);
	mask = current->blocked;
	current->blocked = set & _BLOCKABLE;
	spin_unlock_irq(&current->sigmask_lock);

	regs->pc = regs->npc;
	regs->npc += 4;

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
		regs->psr |= PSR_C;
		regs->u_regs[UREG_I0] = EINTR;
		if (do_signal(mask, regs, 0, 0))
			return;
	}
}

asmlinkage void do_sigpause(unsigned int set, struct pt_regs *regs)
{
	_sigpause_common(set, regs);
}

asmlinkage void do_sigsuspend (struct pt_regs *regs)
{
	_sigpause_common(regs->u_regs[UREG_I0], regs);
}


static inline void
restore_fpu_state(struct pt_regs *regs, __siginfo_fpu_t *fpu)
{
#ifdef __SMP__
	if (current->flags & PF_USEDFPU)
		regs->psr &= ~PSR_EF;
#else
	if (current == last_task_used_math) {
		last_task_used_math = 0;
		regs->psr &= ~PSR_EF;
	}
#endif
	current->used_math = 1;
	current->flags &= ~PF_USEDFPU;

	copy_from_user(&current->tss.float_regs[0], &fpu->si_float_regs[0],
		       (sizeof(unsigned long) * 32));
	__get_user(current->tss.fsr, &fpu->si_fsr);
	__get_user(current->tss.fpqdepth, &fpu->si_fpqdepth);
	if (current->tss.fpqdepth != 0)
		copy_from_user(&current->tss.fpqueue[0],
			       &fpu->si_fpqueue[0],
			       ((sizeof(unsigned long) +
			       (sizeof(unsigned long *)))*16));
}

void do_new_sigreturn (struct pt_regs *regs)
{
	struct new_signal_frame *sf;
	unsigned long up_psr, pc, npc, mask;
	
	sf = (struct new_signal_frame *) regs->u_regs [UREG_FP];

	/* 1. Make sure we are not getting garbage from the user */
	if (verify_area (VERIFY_READ, sf, sizeof (*sf)))
		goto segv_and_exit;

	if (((uint) sf) & 3)
		goto segv_and_exit;

	__get_user(pc,  &sf->info.si_regs.pc);
	__get_user(npc, &sf->info.si_regs.npc);

	if ((pc | npc) & 3)
		goto segv_and_exit;
	
	/* 2. Restore the state */
	up_psr = regs->psr;
	copy_from_user(regs, &sf->info.si_regs, sizeof (struct pt_regs));

	/* User can only change condition codes and FPU enabling in %psr. */
	regs->psr = (up_psr & ~(PSR_ICC | PSR_EF))
		  | (regs->psr & (PSR_ICC | PSR_EF));
	
	if (sf->fpu_save)
		restore_fpu_state(regs, sf->fpu_save);

	/* This is pretty much atomic, no amount locking would prevent
	 * the races which exist anyways.
	 */
	__get_user(mask, &sf->info.si_mask);
	current->blocked = (mask & _BLOCKABLE);
	return;

segv_and_exit:
	/* Ugh, we need to grab master lock in these rare cases ;-( */
	lock_kernel();
	do_exit(SIGSEGV);
	unlock_kernel();
}

asmlinkage void do_sigreturn(struct pt_regs *regs)
{
	struct sigcontext *scptr;
	unsigned long pc, npc, psr, mask;

	synchronize_user_stack();

	if (current->tss.new_signal)
		return do_new_sigreturn (regs);

	scptr = (struct sigcontext *) regs->u_regs[UREG_I0];

	/* Check sanity of the user arg. */
	if(verify_area(VERIFY_READ, scptr, sizeof(struct sigcontext)) ||
	   (((unsigned long) scptr) & 3))
		goto segv_and_exit;

	__get_user(pc, &scptr->sigc_pc);
	__get_user(npc, &scptr->sigc_npc);

	if((pc | npc) & 3)
		goto segv_and_exit;

	/* This is pretty much atomic, no amount locking would prevent
	 * the races which exist anyways.
	 */
	__get_user(mask, &scptr->sigc_mask);
	current->blocked = (mask & _BLOCKABLE);

	__get_user(current->tss.sstk_info.cur_status, &scptr->sigc_onstack);
	current->tss.sstk_info.cur_status &= 1;

	regs->pc = pc;
	regs->npc = npc;

	__get_user(regs->u_regs[UREG_FP], &scptr->sigc_sp);
	__get_user(regs->u_regs[UREG_I0], &scptr->sigc_o0);
	__get_user(regs->u_regs[UREG_G1], &scptr->sigc_g1);

	/* User can only change condition codes in %psr. */
	__get_user(psr, &scptr->sigc_psr);
	regs->psr &= ~(PSR_ICC);
	regs->psr |= (psr & PSR_ICC);
	return;

segv_and_exit:
	/* Ugh, we need to grab master lock in these rare cases ;-( */
	lock_kernel();
	do_exit(SIGSEGV);
	unlock_kernel();
}

/* Checks if the fp is valid */
int invalid_frame_pointer (void *fp, int fplen)
{
	if ((((unsigned long) fp) & 7) ||
	    !__access_ok((unsigned long)fp, fplen) ||
	    ((sparc_cpu_model == sun4 || sparc_cpu_model == sun4c) &&
	     ((unsigned long) fp < 0xe0000000 && (unsigned long) fp >= 0x20000000)))
		return 1;
	
	return 0;
}

static void setup_frame(struct sigaction *sa, unsigned long pc, unsigned long npc,
			struct pt_regs *regs, int signr, unsigned long oldmask)
{
	struct signal_sframe *sframep;
	struct sigcontext *sc;
	int window = 0;
	int old_status = current->tss.sstk_info.cur_status;

	synchronize_user_stack();
	sframep = (struct signal_sframe *) regs->u_regs[UREG_FP];
	sframep = (struct signal_sframe *) (((unsigned long) sframep)-SF_ALIGNEDSZ);
	if (invalid_frame_pointer (sframep, sizeof(*sframep))){
#ifdef DEBUG_SIGNALS /* fills up the console logs during crashme runs, yuck... */
		printk("%s [%d]: User has trashed signal stack\n",
		       current->comm, current->pid);
		printk("Sigstack ptr %p handler at pc<%08lx> for sig<%d>\n",
		       sframep, pc, signr);
#endif
		/* Don't change signal code and address, so that
		 * post mortem debuggers can have a look.
		 */
		goto sigill_and_return;
	}

	sc = &sframep->sig_context;

	/* We've already made sure frame pointer isn't in kernel space... */
	__put_user(old_status, &sc->sigc_onstack);
	__put_user(oldmask, &sc->sigc_mask);
	__put_user(regs->u_regs[UREG_FP], &sc->sigc_sp);
	__put_user(pc, &sc->sigc_pc);
	__put_user(npc, &sc->sigc_npc);
	__put_user(regs->psr, &sc->sigc_psr);
	__put_user(regs->u_regs[UREG_G1], &sc->sigc_g1);
	__put_user(regs->u_regs[UREG_I0], &sc->sigc_o0);
	__put_user(current->tss.w_saved, &sc->sigc_oswins);
	if(current->tss.w_saved)
		for(window = 0; window < current->tss.w_saved; window++) {
			sc->sigc_spbuf[window] =
				(char *)current->tss.rwbuf_stkptrs[window];
			copy_to_user(&sc->sigc_wbuf[window],
			       &current->tss.reg_window[window],
			       sizeof(struct reg_window));
		}
	else
		copy_to_user(sframep, (char *)regs->u_regs[UREG_FP],
		       sizeof(struct reg_window));

	current->tss.w_saved = 0; /* So process is allowed to execute. */
	__put_user(signr, &sframep->sig_num);
	if(signr == SIGSEGV ||
	   signr == SIGILL ||
	   signr == SIGFPE ||
	   signr == SIGBUS ||
	   signr == SIGEMT) {
		__put_user(current->tss.sig_desc, &sframep->sig_code);
		__put_user(current->tss.sig_address, &sframep->sig_address);
	} else {
		__put_user(0, &sframep->sig_code);
		__put_user(0, &sframep->sig_address);
	}
	__put_user(sc, &sframep->sig_scptr);
	regs->u_regs[UREG_FP] = (unsigned long) sframep;
	regs->pc = (unsigned long) sa->sa_handler;
	regs->npc = (regs->pc + 4);
	return;

sigill_and_return:
	/* Ugh, we need to grab master lock in these rare cases ;-( */
	lock_kernel();
	do_exit(SIGILL);
	unlock_kernel();
}


static inline void
save_fpu_state(struct pt_regs *regs, __siginfo_fpu_t *fpu)
{
#ifdef __SMP__
	if (current->flags & PF_USEDFPU) {
		put_psr(get_psr() | PSR_EF);
		fpsave(&current->tss.float_regs[0], &current->tss.fsr,
		       &current->tss.fpqueue[0], &current->tss.fpqdepth);
		regs->psr &= ~(PSR_EF);
		current->flags &= ~(PF_USEDFPU);
	}
#else
	if (current == last_task_used_math) {
		put_psr(get_psr() | PSR_EF);
		fpsave(&current->tss.float_regs[0], &current->tss.fsr,
		       &current->tss.fpqueue[0], &current->tss.fpqdepth);
		last_task_used_math = 0;
		regs->psr &= ~(PSR_EF);
	}
#endif
	copy_to_user(&fpu->si_float_regs[0], &current->tss.float_regs[0],
		     (sizeof(unsigned long) * 32));
	__put_user(current->tss.fsr, &fpu->si_fsr);
	__put_user(current->tss.fpqdepth, &fpu->si_fpqdepth);
	if (current->tss.fpqdepth != 0)
		copy_to_user(&fpu->si_fpqueue[0], &current->tss.fpqueue[0],
			     ((sizeof(unsigned long) +
			     (sizeof(unsigned long *)))*16));
	current->used_math = 0;
}

static void new_setup_frame(struct sigaction *sa, struct pt_regs *regs,
			    int signo, unsigned long oldmask)
{
	struct new_signal_frame *sf;
	int sigframe_size;

	/* 1. Make sure everything is clean */
	synchronize_user_stack();

	sigframe_size = NF_ALIGNEDSZ;
	if (!current->used_math)
		sigframe_size -= sizeof(__siginfo_fpu_t);

	sf = (struct new_signal_frame *)(regs->u_regs[UREG_FP] - sigframe_size);

	if (invalid_frame_pointer (sf, sigframe_size))
		goto sigill_and_return;

	if (current->tss.w_saved != 0) {
		printk ("%s [%d]: Invalid user stack frame for "
			"signal delivery.\n", current->comm, current->pid);
		goto sigill_and_return;
	}

	/* 2. Save the current process state */
	copy_to_user(&sf->info.si_regs, regs, sizeof (struct pt_regs));

	if (current->used_math) {
		save_fpu_state(regs, &sf->fpu_state);
		sf->fpu_save = &sf->fpu_state;
	} else {
		sf->fpu_save = NULL;
	}

	__put_user(oldmask, &sf->info.si_mask);
	copy_to_user(sf, (char *) regs->u_regs [UREG_FP],
		     sizeof (struct reg_window));
	
	/* 3. return to kernel instructions */
	__put_user(0x821020d8, &sf->insns [0]); /* mov __NR_sigreturn, %g1 */
	__put_user(0x91d02010, &sf->insns [1]); /* t 0x10 */

	/* 4. signal handler back-trampoline and parameters */
	regs->u_regs[UREG_FP] = (unsigned long) sf;
	regs->u_regs[UREG_I0] = signo;
	regs->u_regs[UREG_I1] = (unsigned long) &sf->info;
	regs->u_regs[UREG_I7] = (unsigned long) (&(sf->insns[0]) - 2);

	/* 5. signal handler */
	regs->pc = (unsigned long) sa->sa_handler;
	regs->npc = (regs->pc + 4);

	/* Flush instruction space. */
	flush_sig_insns(current->mm, (unsigned long) &(sf->insns[0]));
	return;

sigill_and_return:
	lock_kernel();
	do_exit(SIGILL);
	unlock_kernel();
}


/* Setup a Solaris stack frame */
static inline void
setup_svr4_frame(struct sigaction *sa, unsigned long pc, unsigned long npc,
		 struct pt_regs *regs, int signr, unsigned long oldmask)
{
	svr4_signal_frame_t *sfp;
	svr4_gregset_t  *gr;
	svr4_siginfo_t  *si;
	svr4_mcontext_t *mc;
	svr4_gwindows_t *gw;
	svr4_ucontext_t *uc;
	int window = 0;

	synchronize_user_stack();
	sfp = (svr4_signal_frame_t *) regs->u_regs[UREG_FP] - REGWIN_SZ;
	sfp = (svr4_signal_frame_t *) (((unsigned long) sfp)-SVR4_SF_ALIGNED);

	if (invalid_frame_pointer (sfp, sizeof (*sfp))){
#ifdef DEBUG_SIGNALS
		printk ("Invalid stack frame\n");
#endif
		goto sigill_and_return;
	}

	/* Start with a clean frame pointer and fill it */
	clear_user(sfp, sizeof (*sfp));

	/* Setup convenience variables */
	si = &sfp->si;
	uc = &sfp->uc;
	gw = &sfp->gw;
	mc = &uc->mcontext;
	gr = &mc->greg;
	
	/* FIXME: where am I supposed to put this?
	 * sc->sigc_onstack = old_status;
	 * anyways, it does not look like it is used for anything at all.
	 */
	__put_user(oldmask, &uc->sigmask.sigbits [0]);

	/* Store registers */
	__put_user(regs->pc, &((*gr) [SVR4_PC]));
	__put_user(regs->npc, &((*gr) [SVR4_NPC]));
	__put_user(regs->psr, &((*gr) [SVR4_PSR]));
	__put_user(regs->y, &((*gr) [SVR4_Y]));
	
	/* Copy g [1..7] and o [0..7] registers */
	copy_to_user(&(*gr)[SVR4_G1], &regs->u_regs [UREG_G1], sizeof (long) * 7);
	copy_to_user(&(*gr)[SVR4_O0], &regs->u_regs [UREG_I0], sizeof (long) * 8);

	/* Setup sigaltstack, FIXME */
	__put_user(0xdeadbeef, &uc->stack.sp);
	__put_user(0, &uc->stack.size);
	__put_user(0, &uc->stack.flags);	/* Possible: ONSTACK, DISABLE */

	/* Save the currently window file: */

	/* 1. Link sfp->uc->gwins to our windows */
	__put_user(gw, &mc->gwin);
	    
	/* 2. Number of windows to restore at setcontext (): */
	__put_user(current->tss.w_saved, &gw->count);

	/* 3. Save each valid window
	 *    Currently, it makes a copy of the windows from the kernel copy.
	 *    David's code for SunOS, makes the copy but keeps the pointer to
	 *    the kernel.  My version makes the pointer point to a userland 
	 *    copy of those.  Mhm, I wonder if I shouldn't just ignore those
	 *    on setcontext and use those that are on the kernel, the signal
	 *    handler should not be modyfing those, mhm.
	 *
	 *    These windows are just used in case synchronize_user_stack failed
	 *    to flush the user windows.
	 */
	for(window = 0; window < current->tss.w_saved; window++) {
		__put_user((int *) &(gw->win [window]), &gw->winptr [window]);
		copy_to_user(&gw->win [window], &current->tss.reg_window [window], sizeof (svr4_rwindow_t));
		__put_user(0, gw->winptr [window]);
	}

	/* 4. We just pay attention to the gw->count field on setcontext */
	current->tss.w_saved = 0; /* So process is allowed to execute. */

	/* Setup the signal information.  Solaris expects a bunch of
	 * information to be passed to the signal handler, we don't provide
	 * that much currently, should use those that David already
	 * is providing with tss.sig_desc
	 */
	__put_user(signr, &si->siginfo.signo);
	__put_user(SVR4_SINOINFO, &si->siginfo.code);

	regs->u_regs[UREG_FP] = (unsigned long) sfp;
	regs->pc = (unsigned long) sa->sa_handler;
	regs->npc = (regs->pc + 4);

#ifdef DEBUG_SIGNALS
	printk ("Solaris-frame: %x %x\n", (int) regs->pc, (int) regs->npc);
#endif
	/* Arguments passed to signal handler */
	if (regs->u_regs [14]){
		struct reg_window *rw = (struct reg_window *) regs->u_regs [14];

		__put_user(signr, &rw->ins [0]);
		__put_user(si, &rw->ins [1]);
		__put_user(uc, &rw->ins [2]);
		__put_user(sfp, &rw->ins [6]);	/* frame pointer */
		regs->u_regs[UREG_I0] = signr;
		regs->u_regs[UREG_I1] = (uint) si;
		regs->u_regs[UREG_I2] = (uint) uc;
	}
	return;

sigill_and_return:
	lock_kernel();
	do_exit(SIGILL);
	unlock_kernel();
}

asmlinkage int svr4_getcontext (svr4_ucontext_t *uc, struct pt_regs *regs)
{
	svr4_gregset_t  *gr;
	svr4_mcontext_t *mc;

	synchronize_user_stack();

	if (current->tss.w_saved)
		goto sigsegv_and_return;

	if(clear_user(uc, sizeof (*uc)))
		return -EFAULT;

	/* Setup convenience variables */
	mc = &uc->mcontext;
	gr = &mc->greg;
	
	/* We only have < 32 signals, fill the first slot only */
	__put_user(current->blocked, &uc->sigmask.sigbits [0]);

	/* Store registers */
	__put_user(regs->pc, &uc->mcontext.greg [SVR4_PC]);
	__put_user(regs->npc, &uc->mcontext.greg [SVR4_NPC]);
	__put_user(regs->psr, &uc->mcontext.greg [SVR4_PSR]);
        __put_user(regs->y, &uc->mcontext.greg [SVR4_Y]);
	
	/* Copy g [1..7] and o [0..7] registers */
	copy_to_user(&(*gr)[SVR4_G1], &regs->u_regs [UREG_G1], sizeof (uint) * 7);
	copy_to_user(&(*gr)[SVR4_O0], &regs->u_regs [UREG_I0], sizeof (uint) * 8);

	/* Setup sigaltstack, FIXME */
	__put_user(0xdeadbeef, &uc->stack.sp);
	__put_user(0, &uc->stack.size);
	__put_user(0, &uc->stack.flags);	/* Possible: ONSTACK, DISABLE */

	/* The register file is not saved
	 * we have already stuffed all of it with sync_user_stack
	 */
	return 0;

sigsegv_and_return:
	lock_kernel();
	do_exit(SIGSEGV);
	unlock_kernel();
	return -EFAULT;
}

/* Set the context for a svr4 application, this is Solaris way to sigreturn */
asmlinkage int svr4_setcontext (svr4_ucontext_t *c, struct pt_regs *regs)
{
	struct thread_struct *tp = &current->tss;
	svr4_gregset_t  *gr;
	unsigned long pc, npc, psr, mask;
	
	/* Fixme: restore windows, or is this already taken care of in
	 * svr4_setup_frame when sync_user_windows is done?
	 */
	flush_user_windows();
	
	if (tp->w_saved)
		goto sigsegv_and_return;

	if (((uint) c) & 3)
		goto sigsegv_and_return;

	if(!__access_ok((unsigned long)c, sizeof(*c)))
		goto sigsegv_and_return;

	/* Check for valid PC and nPC */
	gr = &c->mcontext.greg;
	__get_user(pc, &((*gr)[SVR4_PC]));
	__get_user(npc, &((*gr)[SVR4_NPC]));

	if((pc | npc) & 3)
		goto sigsegv_and_return;

	/* Retrieve information from passed ucontext */
	/* note that nPC is ored a 1, this is used to inform entry.S */
	/* that we don't want it to mess with our PC and nPC */

	/* This is pretty much atomic, no amount locking would prevent
	 * the races which exist anyways.
	 */
	__get_user(mask, &c->sigmask.sigbits [0]);
	current->blocked = (mask & _BLOCKABLE);
	regs->pc = pc;
	regs->npc = npc | 1;
	__get_user(regs->y, &((*gr) [SVR4_Y]));
	__get_user(psr, &((*gr) [SVR4_PSR]));
	regs->psr &= ~(PSR_ICC);
	regs->psr |= (psr & PSR_ICC);

	/* Restore g[1..7] and o[0..7] registers */
	copy_from_user(&regs->u_regs [UREG_G1], &(*gr)[SVR4_G1], sizeof (long) * 7);
	copy_from_user(&regs->u_regs [UREG_I0], &(*gr)[SVR4_O0], sizeof (long) * 8);
	return 0;

sigsegv_and_return:
	lock_kernel();
	do_exit(SIGSEGV);
	unlock_kernel();
	return -EFAULT;
}

static inline void handle_signal(unsigned long signr, struct sigaction *sa,
				 unsigned long oldmask, struct pt_regs *regs,
				 int svr4_signal)
{
	if(svr4_signal)
		setup_svr4_frame(sa, regs->pc, regs->npc, regs, signr, oldmask);
	else {
		if (current->tss.new_signal)
			new_setup_frame (sa, regs, signr, oldmask);
		else
			setup_frame(sa, regs->pc, regs->npc, regs, signr, oldmask);
	}
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
			regs->psr |= PSR_C;
			break;
		case ERESTARTSYS:
			if(!(sa->sa_flags & SA_RESTART))
				goto no_system_call_restart;
		/* fallthrough */
		case ERESTARTNOINTR:
			regs->u_regs[UREG_I0] = orig_i0;
			regs->pc -= 4;
			regs->npc -= 4;
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
	int svr4_signal = current->personality == PER_SVR4;
	
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

			/* This happens to be SMP safe so no need to
			 * grab master kernel lock even in this case.
			 */
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
				/* The operations performed by is_orphaned_pgrp()
				 * are protected by the tasklist_lock.
				 */
				if (is_orphaned_pgrp(current->pgrp))
					continue;

			case SIGSTOP:
				if (current->flags & PF_PTRACED)
					continue;
				current->state = TASK_STOPPED;
				current->exit_code = signr;

				/* notify_parent() is SMP safe */
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

				lock_kernel(); /* 8-( */
				do_exit(signr);
				unlock_kernel();
			}
		}
		if(restart_syscall)
			syscall_restart(orig_i0, regs, sa);
		handle_signal(signr, sa, oldmask, regs, svr4_signal);
		return 1;
	}
	if(restart_syscall &&
	   (regs->u_regs[UREG_I0] == ERESTARTNOHAND ||
	    regs->u_regs[UREG_I0] == ERESTARTSYS ||
	    regs->u_regs[UREG_I0] == ERESTARTNOINTR)) {
		/* replay the system call when we are done */
		regs->u_regs[UREG_I0] = orig_i0;
		regs->pc -= 4;
		regs->npc -= 4;
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
		if(copy_to_user(ossptr, &current->tss.sstk_info, sizeof(struct sigstack)))
			goto out;
	}

	/* Now see if we want to update the new state. */
	if(ssptr) {
		if(copy_from_user(&current->tss.sstk_info, ssptr, sizeof(struct sigstack)))
			goto out;
	}
	ret = 0;
out:
	unlock_kernel();
	return ret;
}

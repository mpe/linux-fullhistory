/*  $Id: signal32.c,v 1.6 1997/04/16 10:27:17 jj Exp $
 *  arch/sparc64/kernel/signal32.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1997 Eddie C. Dost   (ecd@skynet.be)
 *  Copyright (C) 1997 Jakub Jelinek   (jj@sunsite.mff.cuni.cz)
 */

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
#include <asm/psrcompat.h>
#include <asm/fpumacro.h>
#include <asm/smp_lock.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

#define synchronize_user_stack() do { } while (0)

asmlinkage int sys_wait4(pid_t pid, unsigned long *stat_addr,
			 int options, unsigned long *ru);

asmlinkage int do_signal32(unsigned long oldmask, struct pt_regs * regs,
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
struct signal_sframe32 {
	struct reg_window32 sig_window;
	int sig_num;
	int sig_code;
	/* struct sigcontext32 * */ u32 sig_scptr;
	int sig_address;
	struct sigcontext32 sig_context;
};

/* 
 * And the new one, intended to be used for Linux applications only
 * (we have enough in there to work with clone).
 * All the interesting bits are in the info field.
 */

struct new_signal_frame32 {
	struct sparc_stackf32	ss;
	__siginfo32_t		info;
	/* __siginfo_fpu32_t * */ u32 fpu_save;
	unsigned int		insns [2];
	__siginfo_fpu32_t	fpu_state;
};

/* Align macros */
#define SF_ALIGNEDSZ  (((sizeof(struct signal_sframe32) + 7) & (~7)))
#define NF_ALIGNEDSZ  (((sizeof(struct new_signal_frame32) + 7) & (~7)))

/*
 * atomically swap in the new signal mask, and wait for a signal.
 * This is really tricky on the Sparc, watch out...
 */
asmlinkage void _sigpause32_common(unsigned int set, struct pt_regs *regs)
{
	unsigned int mask;

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
		regs->tstate |= TSTATE_ICARRY;
		regs->u_regs[UREG_I0] = EINTR;
		if (do_signal32(mask, regs, 0, 0))
			return;
	}
}

asmlinkage void do_sigpause32(unsigned int set, struct pt_regs *regs)
{
	_sigpause32_common(set, regs);
}

asmlinkage void do_sigsuspend32(struct pt_regs *regs)
{
	_sigpause32_common(regs->u_regs[UREG_I0], regs);
}


static inline void
restore_fpu_state32(struct pt_regs *regs, __siginfo_fpu32_t *fpu)
{
#ifdef __SMP__
	if (current->flags & PF_USEDFPU)
		regs->tstate &= ~(TSTATE_PEF);
#else
	if (current == last_task_used_math) {
		last_task_used_math = 0;
		regs->tstate &= ~(TSTATE_PEF);
	}
#endif
	current->used_math = 1;
	current->flags &= ~PF_USEDFPU;

	copy_from_user(&current->tss.float_regs[0],
		       &fpu->si_float_regs[0],
		       (sizeof(unsigned int) * 32));
	__get_user(current->tss.fsr, &fpu->si_fsr);
}

void do_new_sigreturn32(struct pt_regs *regs)
{
	struct new_signal_frame32 *sf;
	unsigned int psr, i;
	unsigned pc, npc, fpu_save, mask;
	
	sf = (struct new_signal_frame32 *) regs->u_regs [UREG_FP];
	/* 1. Make sure we are not getting garbage from the user */
	if (verify_area (VERIFY_READ, sf, sizeof (*sf))){
		goto segv;
	}
	if (((unsigned long) sf) & 3){
		goto segv;
	}
	get_user(pc, &sf->info.si_regs.pc);
	__get_user(npc, &sf->info.si_regs.npc);
	if ((pc | npc) & 3){
		goto segv;
	}
	regs->tpc = pc;
	regs->tnpc = npc;

	/* 2. Restore the state */
	__get_user(regs->y, &sf->info.si_regs.y);
	__get_user(psr, &sf->info.si_regs.psr);
	for (i = 0; i < 16; i++)
		__get_user(regs->u_regs[i], &sf->info.si_regs.u_regs[i]);

	/* User can only change condition codes and FPU enabling in %tstate. */
	regs->tstate &= ~(TSTATE_ICC | TSTATE_PEF);
	regs->tstate |= psr_to_tstate_icc(psr);
	if (psr & PSR_EF) regs->tstate |= TSTATE_PEF;

	__get_user(fpu_save, &sf->fpu_save);
	if (fpu_save)
		restore_fpu_state32(regs, &sf->fpu_state);
	__get_user(mask, &sf->info.si_mask);
	current->blocked = mask & _BLOCKABLE;
	return;
segv:
	lock_kernel();
	do_exit(SIGSEGV);
}

asmlinkage void do_sigreturn32(struct pt_regs *regs)
{
	struct sigcontext32 *scptr;
	unsigned pc, npc, psr;
	unsigned long mask;

	synchronize_user_stack();
	if (current->tss.new_signal)
		return do_new_sigreturn32(regs);

	scptr = (struct sigcontext32 *) regs->u_regs[UREG_I0];
	/* Check sanity of the user arg. */
	if(verify_area(VERIFY_READ, scptr, sizeof(struct sigcontext32)) ||
	   (((unsigned long) scptr) & 3)) {
		goto segv;
	}
	__get_user(pc, &scptr->sigc_pc);
	__get_user(npc, &scptr->sigc_npc);
	if((pc | npc) & 3)
		goto segv; /* Nice try. */

	__get_user(mask, &scptr->sigc_mask);
	current->blocked = (mask & _BLOCKABLE);
	__get_user(current->tss.sstk_info.cur_status, &scptr->sigc_onstack);
	current->tss.sstk_info.cur_status &= 1;
	regs->tpc = pc;
	regs->tnpc = npc;
	__get_user(regs->u_regs[UREG_FP], &scptr->sigc_sp);
	__get_user(regs->u_regs[UREG_I0], &scptr->sigc_o0);
	__get_user(regs->u_regs[UREG_G1], &scptr->sigc_g1);

	/* User can only change condition codes in %tstate. */
	__get_user(psr, &scptr->sigc_psr);
	regs->tstate &= ~(TSTATE_ICC);
	regs->tstate |= psr_to_tstate_icc(psr);
	return;
segv:
	lock_kernel ();
	do_exit (SIGSEGV);
}

/* Checks if the fp is valid */
static int invalid_frame_pointer(void *fp, int fplen)
{
	if ((((unsigned long) fp) & 7) || ((unsigned long)fp) > 0x100000000ULL - fplen)
		return 1;
	return 0;
}

static void
setup_frame32(struct sigaction *sa, unsigned long pc, unsigned long npc,
	      struct pt_regs *regs, int signr, unsigned long oldmask)
{
	struct signal_sframe32 *sframep;
	struct sigcontext32 *sc;
	int window = 0;
	int old_status = current->tss.sstk_info.cur_status;
	unsigned psr;
	int i;
	u32 temp;

	synchronize_user_stack();
	sframep = (struct signal_sframe32 *) regs->u_regs[UREG_FP];
	sframep = (struct signal_sframe32 *) (((unsigned long) sframep)-SF_ALIGNEDSZ);
	if (invalid_frame_pointer (sframep, sizeof(*sframep))){
#ifdef DEBUG_SIGNALS /* fills up the console logs during crashme runs, yuck... */
		printk("%s [%d]: User has trashed signal stack\n",
		       current->comm, current->pid);
		printk("Sigstack ptr %p handler at pc<%016lx> for sig<%d>\n",
		       sframep, pc, signr);
#endif
		/* Don't change signal code and address, so that
		 * post mortem debuggers can have a look.
		 */
		lock_kernel ();
		do_exit(SIGILL);
	}

	sc = &sframep->sig_context;

	/* We've already made sure frame pointer isn't in kernel space... */
	__put_user(old_status, &sc->sigc_onstack);
	__put_user(oldmask, &sc->sigc_mask);
	__put_user(regs->u_regs[UREG_FP], &sc->sigc_sp);
	__put_user(pc, &sc->sigc_pc);
	__put_user(npc, &sc->sigc_npc);
	psr = tstate_to_psr (regs->tstate);
	__put_user(psr, &sc->sigc_psr);
	__put_user(regs->u_regs[UREG_G1], &sc->sigc_g1);
	__put_user(regs->u_regs[UREG_I0], &sc->sigc_o0);
	__put_user(current->tss.w_saved, &sc->sigc_oswins);
#if 0
/* w_saved is not currently used... */
	if(current->tss.w_saved)
		for(window = 0; window < current->tss.w_saved; window++) {
			sc->sigc_spbuf[window] =
				(char *)current->tss.rwbuf_stkptrs[window];
			copy_to_user(&sc->sigc_wbuf[window],
			       &current->tss.reg_window[window],
			       sizeof(struct reg_window));
		}
	else
#endif	
		for (i = 0; i < 16; i++) {
			get_user (temp, (((u32 *)(regs->u_regs[UREG_FP]))+i));
			put_user (temp, (((u32 *)sframep)+i));
		}
		       
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
	__put_user((u64)sc, &sframep->sig_scptr);
	regs->u_regs[UREG_FP] = (unsigned long) sframep;
	regs->tpc = (unsigned long) sa->sa_handler;
	regs->tnpc = (regs->tpc + 4);
}


static inline void
save_fpu_state32(struct pt_regs *regs, __siginfo_fpu32_t *fpu)
{
#ifdef __SMP__
	if (current->flags & PF_USEDFPU) {
		fpsave32(&current->tss.float_regs[0], &current->tss.fsr);
		regs->tstate &= ~(TSTATE_PEF);
		current->flags &= ~(PF_USEDFPU);
	}
#else
	if (current == last_task_used_math) {
		fpsave32((unsigned long *)&current->tss.float_regs[0], &current->tss.fsr);
		last_task_used_math = 0;
		regs->tstate &= ~(TSTATE_PEF);
	}
#endif
	copy_to_user(&fpu->si_float_regs[0], &current->tss.float_regs[0],
		     (sizeof(unsigned int) * 32));
	__put_user(current->tss.fsr, &fpu->si_fsr);
	current->used_math = 0;
}

static inline void
new_setup_frame32(struct sigaction *sa, struct pt_regs *regs,
		  int signo, unsigned long oldmask)
{
	struct new_signal_frame32 *sf;
	int sigframe_size;
	u32 psr, tmp;
	int i;

	/* 1. Make sure everything is clean */
	synchronize_user_stack();
	sigframe_size = NF_ALIGNEDSZ;
	if (!current->used_math)
		sigframe_size -= sizeof(__siginfo_fpu32_t);

	sf = (struct new_signal_frame32 *)(regs->u_regs[UREG_FP] - sigframe_size);
	
	if (invalid_frame_pointer (sf, sigframe_size)){
		lock_kernel ();
		do_exit(SIGILL);
	}

	if (current->tss.w_saved != 0){
		printk ("%s[%d]: Invalid user stack frame for "
			"signal delivery.\n", current->comm, current->pid);
		lock_kernel ();
		do_exit (SIGILL);
	}

	/* 2. Save the current process state */
	put_user(regs->tpc, &sf->info.si_regs.pc);
	__put_user(regs->tnpc, &sf->info.si_regs.npc);
	__put_user(regs->y, &sf->info.si_regs.y);
	psr = tstate_to_psr (regs->tstate);
	__put_user(psr, &sf->info.si_regs.psr);
	for (i = 0; i < 16; i++)
		__put_user(regs->u_regs[i], &sf->info.si_regs.u_regs[i]);

	if (current->used_math) {
		save_fpu_state32(regs, &sf->fpu_state);
		__put_user((u64)&sf->fpu_state, &sf->fpu_save);
	} else {
		__put_user(0, &sf->fpu_save);
	}

	__put_user(oldmask, &sf->info.si_mask);
	for (i = 0; i < sizeof(struct reg_window32)/4; i++) {
		__get_user(tmp, (((u32 *)regs->u_regs[UREG_FP])+i));
		__put_user(tmp, (((u32 *)sf)+i));
	}
	
	/* 3. return to kernel instructions */
	__put_user(0x821020d8, &sf->insns[0]); /* mov __NR_sigreturn, %g1 */
	__put_user(0x91d02010, &sf->insns[1]); /* t 0x10 */

	/* 4. signal handler back-trampoline and parameters */
	regs->u_regs[UREG_FP] = (unsigned long) sf;
	regs->u_regs[UREG_I0] = signo;
	regs->u_regs[UREG_I1] = (unsigned long) &sf->info;
	regs->u_regs[UREG_I7] = (unsigned long) (&(sf->insns[0]) - 2);

	/* 5. signal handler */
	regs->tpc = (unsigned long) sa->sa_handler;
	regs->tnpc = (regs->tpc + 4);

	/* Flush instruction space. */
	__asm__ __volatile__ ("flush %0; flush %0 + 4" : : "r" (&(sf->insns[0])));
		
}

/* Setup a Solaris stack frame */
static inline void
setup_svr4_frame32(struct sigaction *sa, unsigned long pc, unsigned long npc,
		   struct pt_regs *regs, int signr, unsigned long oldmask)
{
	svr4_signal_frame_t *sfp;
	svr4_gregset_t  *gr;
	svr4_siginfo_t  *si;
	svr4_mcontext_t *mc;
	svr4_gwindows_t *gw;
	svr4_ucontext_t *uc;
	int window = 0;
	unsigned psr;
	int i;

	synchronize_user_stack();
	sfp = (svr4_signal_frame_t *) regs->u_regs[UREG_FP] - REGWIN_SZ;
	sfp = (svr4_signal_frame_t *) (((unsigned long) sfp)-SVR4_SF_ALIGNED);

	if (invalid_frame_pointer (sfp, sizeof (*sfp))){
#ifdef DEBUG_SIGNALS
		printk ("Invalid stack frame\n");
#endif
		lock_kernel ();
		do_exit(SIGILL);
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
	__put_user(regs->tpc, &((*gr) [SVR4_PC]));
	__put_user(regs->tnpc, &((*gr) [SVR4_NPC]));
	psr = tstate_to_psr (regs->tstate);
	__put_user(psr, &((*gr) [SVR4_PSR]));
	__put_user(regs->y, &((*gr) [SVR4_Y]));
	
	/* Copy g [1..7] and o [0..7] registers */
	for (i = 0; i < 7; i++)
		__put_user(regs->u_regs[UREG_G1+i], (&(*gr)[SVR4_G1])+i);
	for (i = 0; i < 8; i++)
		__put_user(regs->u_regs[UREG_I0+i], (&(*gr)[SVR4_O0])+i);

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
#if 0	 
	for(window = 0; window < current->tss.w_saved; window++) {
		__put_user((int *) &(gw->win [window]), &gw->winptr [window]);
		copy_to_user(&gw->win [window], &current->tss.reg_window [window], sizeof (svr4_rwindow_t));
		__put_user(0, gw->winptr [window]);
	}
#endif	

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
	regs->tpc = (unsigned long) sa->sa_handler;
	regs->tnpc = (regs->tpc + 4);

#ifdef DEBUG_SIGNALS
	printk ("Solaris-frame: %x %x\n", (int) regs->tpc, (int) regs->tnpc);
#endif
	/* Arguments passed to signal handler */
	if (regs->u_regs [14]){
		struct reg_window32 *rw = (struct reg_window32 *) regs->u_regs [14];

		__put_user(signr, &rw->ins [0]);
		__put_user((u64)si, &rw->ins [1]);
		__put_user((u64)uc, &rw->ins [2]);
		__put_user((u64)sfp, &rw->ins [6]);	/* frame pointer */
		regs->u_regs[UREG_I0] = signr;
		regs->u_regs[UREG_I1] = (u32)(u64) si;
		regs->u_regs[UREG_I2] = (u32)(u64) uc;
	}
}

asmlinkage int
svr4_getcontext32(svr4_ucontext_t *uc, struct pt_regs *regs)
{
	svr4_gregset_t  *gr;
	svr4_mcontext_t *mc;
	int i;

	synchronize_user_stack();
	if (current->tss.w_saved){
		printk ("Uh oh, w_saved is not zero (%ld)\n", current->tss.w_saved);
		do_exit (SIGSEGV);
	}
	if(clear_user(uc, sizeof (*uc)))
		return -EFAULT;

	/* Setup convenience variables */
	mc = &uc->mcontext;
	gr = &mc->greg;
	
	/* We only have < 32 signals, fill the first slot only */
	__put_user(current->blocked, &uc->sigmask.sigbits [0]);

	/* Store registers */
	__put_user(regs->tpc, &uc->mcontext.greg [SVR4_PC]);
	__put_user(regs->tnpc, &uc->mcontext.greg [SVR4_NPC]);
	__put_user(tstate_to_psr(regs->tstate), &uc->mcontext.greg [SVR4_PSR]);
        __put_user(regs->y, &uc->mcontext.greg [SVR4_Y]);
	
	/* Copy g [1..7] and o [0..7] registers */
	for (i = 0; i < 7; i++)
		__put_user(regs->u_regs[UREG_G1+i], (&(*gr)[SVR4_G1])+i);
	for (i = 0; i < 8; i++)
		__put_user(regs->u_regs[UREG_I0+i], (&(*gr)[SVR4_O0])+i);

	/* Setup sigaltstack, FIXME */
	__put_user(0xdeadbeef, &uc->stack.sp);
	__put_user(0, &uc->stack.size);
	__put_user(0, &uc->stack.flags);	/* Possible: ONSTACK, DISABLE */

	/* The register file is not saved
	 * we have already stuffed all of it with sync_user_stack
	 */
	return 0;
}


/* Set the context for a svr4 application, this is Solaris way to sigreturn */
asmlinkage int svr4_setcontext32(svr4_ucontext_t *c, struct pt_regs *regs)
{
	struct thread_struct *tp = &current->tss;
	svr4_gregset_t  *gr;
	u32 pc, npc, psr;
	int i;
	
	/* Fixme: restore windows, or is this already taken care of in
	 * svr4_setup_frame when sync_user_windows is done?
	 */
	flush_user_windows();
	
	if (tp->w_saved){
		printk ("Uh oh, w_saved is: 0x%lx\n", tp->w_saved);
		do_exit(SIGSEGV);
	}
	if (((unsigned long) c) & 3){
		printk ("Unaligned structure passed\n");
		do_exit (SIGSEGV);
	}

	if(!__access_ok((unsigned long)c, sizeof(*c))) {
		/* Miguel, add nice debugging msg _here_. ;-) */
		do_exit(SIGSEGV);
	}

	/* Check for valid PC and nPC */
	gr = &c->mcontext.greg;
	__get_user(pc, &((*gr)[SVR4_PC]));
	__get_user(npc, &((*gr)[SVR4_NPC]));
	if((pc | npc) & 3) {
	        printk ("setcontext, PC or nPC were bogus\n");
		do_exit (SIGSEGV);
	}
	/* Retrieve information from passed ucontext */
	    /* note that nPC is ored a 1, this is used to inform entry.S */
	    /* that we don't want it to mess with our PC and nPC */
	__get_user(current->blocked, &c->sigmask.sigbits [0]);
	current->blocked &= _BLOCKABLE;
	regs->tpc = pc;
	regs->tnpc = npc | 1;
	__get_user(regs->y, &((*gr) [SVR4_Y]));
	__get_user(psr, &((*gr) [SVR4_PSR]));
	regs->tstate &= ~(TSTATE_ICC);
	regs->tstate |= psr_to_tstate_icc(psr);

	/* Restore g[1..7] and o[0..7] registers */
	for (i = 0; i < 7; i++)
		__put_user(regs->u_regs[UREG_G1+i], (&(*gr)[SVR4_G1])+i);
	for (i = 0; i < 8; i++)
		__put_user(regs->u_regs[UREG_I0+i], (&(*gr)[SVR4_O0])+i);

	return -EINTR;
}

static inline void handle_signal32(unsigned long signr, struct sigaction *sa,
				   unsigned long oldmask, struct pt_regs *regs,
				   int svr4_signal)
{
	if(svr4_signal)
		setup_svr4_frame32(sa, regs->tpc, regs->tnpc, regs, signr, oldmask);
	else {
		if (current->tss.new_signal)
			new_setup_frame32(sa, regs, signr, oldmask);
		else
			setup_frame32(sa, regs->tpc, regs->tnpc, regs, signr, oldmask);
	}
	if(sa->sa_flags & SA_ONESHOT)
		sa->sa_handler = NULL;
	if(!(sa->sa_flags & SA_NOMASK))
		current->blocked |= (sa->sa_mask | _S(signr)) & _BLOCKABLE;
}

static inline void syscall_restart32(unsigned long orig_i0, struct pt_regs *regs,
				     struct sigaction *sa)
{
	switch(regs->u_regs[UREG_I0]) {
		case ERESTARTNOHAND:
		no_system_call_restart:
			regs->u_regs[UREG_I0] = EINTR;
			regs->tstate |= TSTATE_ICARRY;
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
asmlinkage int do_signal32(unsigned long oldmask, struct pt_regs * regs,
			   unsigned long orig_i0, int restart_syscall)
{
	unsigned long signr, mask = ~current->blocked;
	struct sigaction *sa;
	int svr4_signal = current->personality == PER_SVR4;
	
	while ((signr = current->signal & mask) != 0) {
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
				current->signal |= _S(signr);
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
					if(current->binfmt->core_dump(signr, regs))
						signr |= 0x80;
				}
#ifdef DEBUG_SIGNALS
				/* Very useful to debug dynamic linker problems */
				printk ("Sig ILL going...\n");
				show_regs (regs);
#endif
				/* fall through */
			default:
				current->signal |= _S(signr & 0x7f);
				current->flags |= PF_SIGNALED;
				do_exit(signr);
			}
		}
		if(restart_syscall)
			syscall_restart32(orig_i0, regs, sa);
		handle_signal32(signr, sa, oldmask, regs, svr4_signal);
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

struct sigstack32 {
	u32 the_stack;
	int cur_status;
};

asmlinkage int
sys32_sigstack(struct sigstack32 *ssptr, struct sigstack32 *ossptr)
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

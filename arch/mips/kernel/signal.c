/*
 *  linux/arch/mips/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>

#include <asm/segment.h>
#include <asm/cachectl.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

asmlinkage int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options);

/*
 * atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int sys_sigsuspend(int restart, unsigned long oldmask, unsigned long set)
{
	unsigned long mask;
	struct pt_regs * regs = (struct pt_regs *) &restart;

	mask = current->blocked;
	current->blocked = set & _BLOCKABLE;
	regs->reg2 = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(mask,regs))
			return -EINTR;
	}
}

/*
 * This sets regs->reg29 even though we don't actually use sigstacks yet..
 */
asmlinkage int sys_sigreturn(unsigned long __unused)
{
	struct sigcontext_struct context;
	struct pt_regs * regs;

	regs = (struct pt_regs *) &__unused;
	if (verify_area(VERIFY_READ, (void *) regs->reg29, sizeof(context)))
		goto badframe;
	memcpy_fromfs(&context,(void *) regs->reg29, sizeof(context));
	current->blocked = context.oldmask & _BLOCKABLE;
	regs->reg1  = context.sc_at;
	regs->reg2  = context.sc_v0;
	regs->reg3  = context.sc_v1;
	regs->reg4  = context.sc_a0;
	regs->reg5  = context.sc_a1;
	regs->reg6  = context.sc_a2;
	regs->reg7  = context.sc_a3;
	regs->reg8  = context.sc_t0;
	regs->reg9  = context.sc_t1;
	regs->reg10 = context.sc_t2;
	regs->reg11 = context.sc_t3;
	regs->reg12 = context.sc_t4;
	regs->reg13 = context.sc_t5;
	regs->reg14 = context.sc_t6;
	regs->reg15 = context.sc_t7;
	regs->reg16 = context.sc_s0;
	regs->reg17 = context.sc_s1;
	regs->reg18 = context.sc_s2;
	regs->reg19 = context.sc_s3;
	regs->reg20 = context.sc_s4;
	regs->reg21 = context.sc_s5;
	regs->reg22 = context.sc_s6;
	regs->reg23 = context.sc_s7;
	regs->reg24 = context.sc_t8;
	regs->reg25 = context.sc_t9;
	/*
	 * Skip k0/k1
	 */
	regs->reg28 = context.sc_gp;
	regs->reg29 = context.sc_sp;
	regs->reg30 = context.sc_fp;
	regs->reg31 = context.sc_ra;
	regs->cp0_epc = context.sc_epc;
	regs->cp0_cause = context.sc_cause;

	/*
	 * disable syscall checks
	 */
	regs->orig_reg2 = -1;
	return regs->orig_reg2;
badframe:
	do_exit(SIGSEGV);
}

/*
 * Set up a signal frame...
 */
static void setup_frame(struct sigaction * sa, unsigned long ** fp,
                        unsigned long pc, struct pt_regs *regs,
                        int signr, unsigned long oldmask)
{
	unsigned long * frame;

	frame = *fp;
	frame -= 32;
	if (verify_area(VERIFY_WRITE,frame,21*4))
		do_exit(SIGSEGV);
	/*
	 * set up the "normal" stack seen by the signal handler
	 */
	put_fs_long(regs->reg1 , frame   );
	put_fs_long(regs->reg2 , frame+ 1);
	put_fs_long(regs->reg3 , frame+ 2);
	put_fs_long(regs->reg4 , frame+ 3);
	put_fs_long(regs->reg5 , frame+ 4);
	put_fs_long(regs->reg6 , frame+ 5);
	put_fs_long(regs->reg7 , frame+ 6);
	put_fs_long(regs->reg8 , frame+ 7);
	put_fs_long(regs->reg9 , frame+ 8);
	put_fs_long(regs->reg10, frame+ 9);
	put_fs_long(regs->reg11, frame+10);
	put_fs_long(regs->reg12, frame+11);
	put_fs_long(regs->reg13, frame+12);
	put_fs_long(regs->reg14, frame+13);
	put_fs_long(regs->reg15, frame+14);
	put_fs_long(regs->reg16, frame+15);
	put_fs_long(regs->reg17, frame+16);
	put_fs_long(regs->reg18, frame+17);
	put_fs_long(regs->reg19, frame+18);
	put_fs_long(regs->reg20, frame+19);
	put_fs_long(regs->reg21, frame+20);
	put_fs_long(regs->reg22, frame+21);
	put_fs_long(regs->reg23, frame+22);
	put_fs_long(regs->reg24, frame+23);
	put_fs_long(regs->reg25, frame+24);
	/*
	 * Don't copy k0/k1
	 */
	put_fs_long(regs->reg28, frame+25);
	put_fs_long(regs->reg29, frame+26);
	put_fs_long(regs->reg30, frame+27);
	put_fs_long(regs->reg31, frame+28);
	put_fs_long(pc         , frame+29);
	put_fs_long(oldmask    , frame+30);
	/*
	 * set up the return code...
	 *
	 *         .set    noreorder
	 *         .set    noat
	 *         syscall
	 *         li      $1,__NR_sigreturn
	 *         .set    at
	 *         .set    reorder
	 */
	put_fs_long(0x24010077, frame+31);		/* li	$1,119 */
	put_fs_long(0x000000c0, frame+32);		/* syscall     */
	*fp = frame;
	/*
	 * Flush caches so the instructions will be correctly executed.
	 */
	sys_cacheflush(frame, 32*4, BCACHE);
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
	unsigned long *frame = NULL;
	unsigned long pc = 0;
	unsigned long signr;
	struct sigaction * sa;

	while ((signr = current->signal & mask)) {
		__asm__(".set\tnoreorder\n\t"
			".set\tnoat\n\t"
			"li\t%0,31\n"
			"1:\tsllv\t$1,%1,%0\n\t"
			"bgezl\t$1,1b\n\t"
			"subu\t$8,1\n\t"
			"subu\t%0,31\n\t"
			"subu\t%0,$0,%0\n\t"
			"li\t$1,1\n\t"
			"sllv\t$1,$1,%0\n\t"
			"nor\t$1,$0\n\t"
			"xor\t%1,$1\n\t"
			".set\tat\n\t"
			".set\treorder"
			:"=r" (signr),"=r" (current->signal)
			:"0"  (signr),"1"  (current->signal)
			:"$1");
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
			case SIGIOT: case SIGFPE: case SIGSEGV:
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
		if (regs->orig_reg2 >= 0) {
			if (regs->reg2 == -ERESTARTNOHAND ||
			   (regs->reg2 == -ERESTARTSYS &&
			    !(sa->sa_flags & SA_RESTART)))
				regs->reg2 = -EINTR;
		}
		handler_signal |= 1 << (signr-1);
		mask &= ~sa->sa_mask;
	}
	if (regs->orig_reg2 >= 0 &&
	    (regs->reg2 == -ERESTARTNOHAND ||
	     regs->reg2 == -ERESTARTSYS ||
	     regs->reg2 == -ERESTARTNOINTR)) {
		regs->reg2 = regs->orig_reg2;
		regs->cp0_epc -= 4;
	}
	if (!handler_signal)		/* no handler will be called - return 0 */
		return 0;
	pc = regs->cp0_epc;
	frame = (unsigned long *) regs->reg29;
	signr = 1;
	sa = current->sigaction;
	for (mask = 1 ; mask ; sa++,signr++,mask += mask) {
		if (mask > handler_signal)
			break;
		if (!(mask & handler_signal))
			continue;
		setup_frame(sa,&frame,pc,regs,signr,oldmask);
		pc = (unsigned long) sa->sa_handler;
		if (sa->sa_flags & SA_ONESHOT)
			sa->sa_handler = NULL;
		/*
		 * force a kernel-mode page-in of the signal
		 * handler to reduce races
		 */
		__asm__(".set\tnoat\n\t"
			"lwu\t$1,(%0)\n\t"
			".set\tat\n\t"
			:
			:"r" ((char *) pc)
			:"$1");
		current->blocked |= sa->sa_mask;
		oldmask |= sa->sa_mask;
	}
	regs->reg29 = (unsigned long) frame;
	regs->cp0_epc = pc;		/* "return" to the first handler */
	return 1;
}

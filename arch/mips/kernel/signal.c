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

#include <asm/bitops.h>
#include <asm/segment.h>
#include <asm/cachectl.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

asmlinkage int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options);
asmlinkage int do_signal(unsigned long oldmask, struct pt_regs *regs);

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
asmlinkage int sys_sigreturn(struct pt_regs *regs)
{
	struct sigcontext_struct *context;

	/*
	 * We don't support fixing ADEL/ADES exceptions for signal stack frames.
	 * No big loss - who doesn't care about the alignment of this stack
	 * really deserves to loose.
	 */
	context = (struct sigcontext_struct *) regs->reg29;
	if (verify_area(VERIFY_READ, context, sizeof(struct sigcontext_struct)) ||
	    (regs->reg29 & 3))
		goto badframe;

	current->blocked = context->sc_oldmask & _BLOCKABLE;
	regs->reg1  = context->sc_at;
	regs->reg2  = context->sc_v0;
	regs->reg3  = context->sc_v1;
	regs->reg4  = context->sc_a0;
	regs->reg5  = context->sc_a1;
	regs->reg6  = context->sc_a2;
	regs->reg7  = context->sc_a3;
	regs->reg8  = context->sc_t0;
	regs->reg9  = context->sc_t1;
	regs->reg10 = context->sc_t2;
	regs->reg11 = context->sc_t3;
	regs->reg12 = context->sc_t4;
	regs->reg13 = context->sc_t5;
	regs->reg14 = context->sc_t6;
	regs->reg15 = context->sc_t7;
	regs->reg16 = context->sc_s0;
	regs->reg17 = context->sc_s1;
	regs->reg18 = context->sc_s2;
	regs->reg19 = context->sc_s3;
	regs->reg20 = context->sc_s4;
	regs->reg21 = context->sc_s5;
	regs->reg22 = context->sc_s6;
	regs->reg23 = context->sc_s7;
	regs->reg24 = context->sc_t8;
	regs->reg25 = context->sc_t9;
	/*
	 * Skip k0/k1
	 */
	regs->reg28 = context->sc_gp;
	regs->reg29 = context->sc_sp;
	regs->reg30 = context->sc_fp;
	regs->reg31 = context->sc_ra;
	regs->cp0_epc = context->sc_epc;
	regs->cp0_cause = context->sc_cause;

	/*
	 * disable syscall checks
	 */
	regs->orig_reg2 = -1;
	return context->sc_v0;

badframe:
	do_exit(SIGSEGV);
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
 *             arg save space                   ; 16 bytes argument save space
 *	       code1   (addiu sp,#1-offset)	; syscall number
 *	       code2   (li v0,__NR_sigreturn)	; syscall number
 *	       code3   (syscall)		; do sigreturn(2)
 *     #1|     at, v0, v1, a0, a1, a2, a3       ; All integer registers
 *       |     t0, t1, t2, t3, t4, t5, t6, t7   ; except zero, k0 and k1
 *       |     s0, s1, s2, s3, s4, s5, s6, s7
 *       |     t8, t9, gp, sp, fp, ra;
 *       |     epc                              ; old program counter
 *       |     cause                            ; CP0 cause register
 *       |     oldmask
 */

struct sc {
	unsigned long	ass[4];
	unsigned int	code[4];
	struct sigcontext_struct scc;
};
#define scc_offset ((size_t)&((struct sc *)0)->scc)

static void setup_frame(struct sigaction * sa, struct sc **fp,
                        unsigned long pc, struct pt_regs *regs,
                        int signr, unsigned long oldmask)
{
	struct sc *frame;

	frame = *fp;
	frame--;

	/*
	 * We don't support fixing ADEL/ADES exceptions for signal stack frames.
	 * No big loss - who doesn't care about the alignment of this stack
	 * really deserves to loose.
	 */
	if (verify_area(VERIFY_WRITE, frame, sizeof (struct sc)) ||
	    ((unsigned long)frame & 3))
		do_exit(SIGSEGV);

	/*
	 * Set up the return code ...
	 *
	 *         .set    noreorder
	 *         addiu   sp,24
	 *         li      v0,__NR_sigreturn
	 *         syscall
	 *         .set    reorder
	 */
	frame->code[0] = 0x27bd0000 + scc_offset;
	frame->code[1] = 0x24020000 + __NR_sigreturn;
	frame->code[2] = 0x0000000c;

	/*
	 * Flush caches so that the instructions will be correctly executed.
	 */
	sys_cacheflush (frame->code, sizeof (frame->code), ICACHE);

	/*
	 * Set up the "normal" sigcontext_struct
	 */
	frame->scc.sc_at = regs->reg1;		/* Assembler temporary */
	frame->scc.sc_v0 = regs->reg2;		/* Result registers */
	frame->scc.sc_v1 = regs->reg3;
	frame->scc.sc_a0 = regs->reg4;		/* Argument registers */
	frame->scc.sc_a1 = regs->reg5;
	frame->scc.sc_a2 = regs->reg6;
	frame->scc.sc_a3 = regs->reg7;

	frame->scc.sc_t0 = regs->reg8;		/* Caller saved */
	frame->scc.sc_t1 = regs->reg9;
	frame->scc.sc_t2 = regs->reg10;
	frame->scc.sc_t3 = regs->reg11;
	frame->scc.sc_t4 = regs->reg12;
	frame->scc.sc_t5 = regs->reg13;
	frame->scc.sc_t6 = regs->reg14;
	frame->scc.sc_t7 = regs->reg15;

	frame->scc.sc_s0 = regs->reg16;		/* Callee saved */
	frame->scc.sc_s1 = regs->reg17;
	frame->scc.sc_s2 = regs->reg18;
	frame->scc.sc_s3 = regs->reg19;
	frame->scc.sc_s4 = regs->reg20;
	frame->scc.sc_s5 = regs->reg21;
	frame->scc.sc_s6 = regs->reg22;
	frame->scc.sc_s7 = regs->reg23;

	frame->scc.sc_t8 = regs->reg24;		/* Caller saved */
	frame->scc.sc_t9 = regs->reg25;

	/*
	 * Don't copy k0/k1
	 */
	frame->scc.sc_gp = regs->reg28;		/* global pointer / s8 */
	frame->scc.sc_sp = regs->reg29;		/* old stack pointer */
	frame->scc.sc_fp = regs->reg30;		/* old frame pointer */
	frame->scc.sc_ra = regs->reg31;		/* old return address */

	frame->scc.sc_epc = regs->cp0_epc;	/* Program counter */
	frame->scc.sc_cause = regs->cp0_cause;	/* c0_epc register */

	frame->scc.sc_oldmask = oldmask;
	*fp = frame;

	regs->reg4 = signr;             /* argument for handler */
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
	struct sc *frame = NULL;
	unsigned long pc = 0;
	unsigned long signr;
	struct sigaction * sa;

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
				current->signal |= _S(signr);
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

			case SIGSTOP: case SIGTSTP: case SIGTTIN: case SIGTTOU:
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
			case SIGIOT: case SIGFPE: case SIGSEGV: case SIGBUS:
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
	/*
	 * Who's code doesn't conform to the restartable syscall convention
	 * dies here!!!  The li instruction, a single machine instruction,
	 * must directly be followed by the syscall instruction.
	 */
	if (regs->orig_reg2 >= 0 &&
	    (regs->reg2 == -ERESTARTNOHAND ||
	     regs->reg2 == -ERESTARTSYS ||
	     regs->reg2 == -ERESTARTNOINTR))
	{
		regs->reg2 = regs->orig_reg2;
		regs->cp0_epc -= 8;
	}
	if (!handler_signal)		/* no handler will be called - return 0 */
		return 0;
	pc = regs->cp0_epc;
	frame = (struct sc *) regs->reg29;
	signr = 1;
	sa = current->sig->action;
	for (mask = 1 ; mask ; sa++,signr++,mask += mask) {
		if (mask > handler_signal)
			break;
		if (!(mask & handler_signal))
			continue;
		setup_frame(sa, &frame, pc, regs, signr, oldmask);
		pc = (unsigned long) sa->sa_handler;
		if (sa->sa_flags & SA_ONESHOT)
			sa->sa_handler = NULL;
		current->blocked |= sa->sa_mask;
		oldmask |= sa->sa_mask;
	}
	regs->reg29 = (unsigned long) frame;		/* Stack pointer */
	regs->reg31 = (unsigned long) frame->code;	/* Return address */
	regs->cp0_epc = pc;		/* "return" to the first handler */

	return 1;
}

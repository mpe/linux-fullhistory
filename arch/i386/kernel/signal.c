/*
 *  linux/arch/i386/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/config.h>

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>

#include <asm/segment.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

asmlinkage int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options);
asmlinkage int do_signal(unsigned long oldmask, struct pt_regs * regs);

/*
 * atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int sys_sigsuspend(int restart, unsigned long oldmask, unsigned long set)
{
	unsigned long mask;
	struct pt_regs * regs = (struct pt_regs *) &restart;

	mask = current->blocked;
	current->blocked = set & _BLOCKABLE;
	regs->eax = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(mask,regs))
			return -EINTR;
	}
}

static inline void restore_i387_hard(struct _fpstate *buf)
{
#ifdef __SMP__
	if (current->flags & PF_USEDFPU) {
		stts();
	}
#else
	if (current == last_task_used_math) {
		last_task_used_math = NULL;
		stts();
	}
#endif
	current->used_math = 1;
	current->flags &= ~PF_USEDFPU;
	memcpy_fromfs(&current->tss.i387.hard, buf, sizeof(*buf));
}

static void restore_i387(struct _fpstate *buf)
{
#ifndef CONFIG_MATH_EMULATION
	restore_i387_hard(buf);
#else
	if (hard_math) {
		restore_i387_hard(buf);
		return;
	}
	restore_i387_soft(buf);
#endif	
}
	

/*
 * This sets regs->esp even though we don't actually use sigstacks yet..
 */
asmlinkage int sys_sigreturn(unsigned long __unused)
{
#define COPY(x) regs->x = context.x
#define COPY_SEG(x) \
if ((context.x & 0xfffc) && (context.x & 3) != 3) goto badframe; COPY(x);
#define COPY_SEG_STRICT(x) \
if (!(context.x & 0xfffc) || (context.x & 3) != 3) goto badframe; COPY(x);
	struct sigcontext_struct context;
	struct pt_regs * regs;

	regs = (struct pt_regs *) &__unused;
	if (verify_area(VERIFY_READ, (void *) regs->esp, sizeof(context)))
		goto badframe;
	memcpy_fromfs(&context,(void *) regs->esp, sizeof(context));
	current->blocked = context.oldmask & _BLOCKABLE;
	COPY_SEG(ds);
	COPY_SEG(es);
	COPY_SEG(fs);
	COPY_SEG(gs);
	COPY_SEG_STRICT(ss);
	COPY_SEG_STRICT(cs);
	COPY(eip);
	COPY(ecx); COPY(edx);
	COPY(ebx);
	COPY(esp); COPY(ebp);
	COPY(edi); COPY(esi);
	regs->eflags &= ~0x40DD5;
	regs->eflags |= context.eflags & 0x40DD5;
	regs->orig_eax = -1;		/* disable syscall checks */
	if (context.fpstate) {
		struct _fpstate * buf = context.fpstate;
		if (verify_area(VERIFY_READ, buf, sizeof(*buf)))
			goto badframe;
		restore_i387(buf);
	}
	return context.eax;
badframe:
	do_exit(SIGSEGV);
}

static inline struct _fpstate * save_i387_hard(struct _fpstate * buf)
{
#ifdef __SMP__
	if (current->flags & PF_USEDFPU) {
		__asm__ __volatile__("fnsave %0":"=m" (current->tss.i387.hard));
		stts();
		current->flags &= ~PF_USEDFPU;
	}
#else
	if (current == last_task_used_math) {
		__asm__ __volatile__("fnsave %0":"=m" (current->tss.i387.hard));
		last_task_used_math = NULL;
		__asm__ __volatile__("fwait");	/* not needed on 486+ */
		stts();
	}
#endif
	current->tss.i387.hard.status = current->tss.i387.hard.swd;
	memcpy_tofs(buf, &current->tss.i387.hard, sizeof(*buf));
	current->used_math = 0;
	return buf;
}

static struct _fpstate * save_i387(struct _fpstate * buf)
{
	if (!current->used_math)
		return NULL;

#ifndef CONFIG_MATH_EMULATION
	return save_i387_hard(buf);
#else
	if (hard_math)
		return save_i387_hard(buf);
	return save_i387_soft(buf);
#endif
}

/*
 * Set up a signal frame... Make the stack look the way iBCS2 expects
 * it to look.
 */
static void setup_frame(struct sigaction * sa,
	struct pt_regs * regs, int signr,
	unsigned long oldmask)
{
	unsigned long * frame;

	frame = (unsigned long *) regs->esp;
	if (regs->ss != USER_DS && sa->sa_restorer)
		frame = (unsigned long *) sa->sa_restorer;
	frame -= 64;
	if (verify_area(VERIFY_WRITE,frame,64*4))
		do_exit(SIGSEGV);

/* set up the "normal" stack seen by the signal handler (iBCS2) */
#define __CODE ((unsigned long)(frame+24))
#define CODE(x) ((unsigned long *) ((x)+__CODE))
	put_user(__CODE,frame);
	if (current->exec_domain && current->exec_domain->signal_invmap)
		put_user(current->exec_domain->signal_invmap[signr], frame+1);
	else
		put_user(signr, frame+1);
	put_user(regs->gs, frame+2);
	put_user(regs->fs, frame+3);
	put_user(regs->es, frame+4);
	put_user(regs->ds, frame+5);
	put_user(regs->edi, frame+6);
	put_user(regs->esi, frame+7);
	put_user(regs->ebp, frame+8);
	put_user(regs->esp, frame+9);
	put_user(regs->ebx, frame+10);
	put_user(regs->edx, frame+11);
	put_user(regs->ecx, frame+12);
	put_user(regs->eax, frame+13);
	put_user(current->tss.trap_no, frame+14);
	put_user(current->tss.error_code, frame+15);
	put_user(regs->eip, frame+16);
	put_user(regs->cs, frame+17);
	put_user(regs->eflags, frame+18);
	put_user(regs->esp, frame+19);
	put_user(regs->ss, frame+20);
	put_user(save_i387((struct _fpstate *)(frame+32)),frame+21);
/* non-iBCS2 extensions.. */
	put_user(oldmask, frame+22);
	put_user(current->tss.cr2, frame+23);
/* set up the return code... */
	put_user(0x0000b858, CODE(0));	/* popl %eax ; movl $,%eax */
	put_user(0x80cd0000, CODE(4));	/* int $0x80 */
	put_user(__NR_sigreturn, CODE(2));
#undef __CODE
#undef CODE

	/* Set up registers for signal handler */
	regs->esp = (unsigned long) frame;
	regs->eip = (unsigned long) sa->sa_handler;
	regs->cs = USER_CS; regs->ss = USER_DS;
	regs->ds = USER_DS; regs->es = USER_DS;
	regs->gs = USER_DS; regs->fs = USER_DS;
	regs->eflags &= ~TF_MASK;
}

/*
 * OK, we're invoking a handler
 */	
static void handle_signal(unsigned long signr, struct sigaction *sa,
	unsigned long oldmask, struct pt_regs * regs)
{
	/* are we from a system call? */
	if (regs->orig_eax >= 0) {
		/* If so, check system call restarting.. */
		switch (regs->eax) {
			case -ERESTARTNOHAND:
				regs->eax = -EINTR;
				break;

			case -ERESTARTSYS:
				if (!(sa->sa_flags & SA_RESTART)) {
					regs->eax = -EINTR;
					break;
				}
			/* fallthrough */
			case -ERESTARTNOINTR:
				regs->eax = regs->orig_eax;
				regs->eip -= 2;
		}
	}

	/* set up the stack frame */
	setup_frame(sa, regs, signr, oldmask);

	if (sa->sa_flags & SA_ONESHOT)
		sa->sa_handler = NULL;
	if (!(sa->sa_flags & SA_NOMASK))
		current->blocked |= (sa->sa_mask | _S(signr)) & _BLOCKABLE;
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
	unsigned long signr;
	struct sigaction * sa;

	while ((signr = current->signal & mask)) {
		/*
		 *	This stops gcc flipping out. Otherwise the assembler
		 *	including volatiles for the inline function to get
		 *	current combined with this gets it confused.
		 */
	        struct task_struct *t=current;
		__asm__("bsf %3,%1\n\t"
			"btrl %1,%0"
			:"=m" (t->signal),"=r" (signr)
			:"0" (t->signal), "1" (signr));
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
				if (current->binfmt && current->binfmt->core_dump) {
					if (current->binfmt->core_dump(signr, regs))
						signr |= 0x80;
				}
				/* fall through */
			default:
				current->signal |= _S(signr & 0x7f);
				current->flags |= PF_SIGNALED;
				do_exit(signr);
			}
		}
		handle_signal(signr, sa, oldmask, regs);
		return 1;
	}

	/* Did we come from a system call? */
	if (regs->orig_eax >= 0) {
		/* Restart the system call - no handlers present */
		if (regs->eax == -ERESTARTNOHAND ||
		    regs->eax == -ERESTARTSYS ||
		    regs->eax == -ERESTARTNOINTR) {
			regs->eax = regs->orig_eax;
			regs->eip -= 2;
		}
	}
	return 0;
}

/* $Id: irixsig.c,v 1.5 1997/12/06 09:57:38 ralf Exp $
 * irixsig.c: WHEEE, IRIX signals!  YOW, am I compatable or what?!?!
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/time.h>

#include <asm/ptrace.h>
#include <asm/uaccess.h>

#undef DEBUG_SIG

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

struct sigctx_irix5 {
	u32 rmask, cp0_status;
	u64 pc;
	u64 regs[32];
	u64 fpregs[32];
	u32 usedfp, fpcsr, fpeir, sstk_flags;
	u64 hi, lo;
	u64 cp0_cause, cp0_badvaddr, _unused0;
	u32 sigset[4];
	u64 weird_fpu_thing;
	u64 _unused1[31];
};

#ifdef DEBUG_SIG
/* Debugging */
static inline void dump_irix5_sigctx(struct sigctx_irix5 *c)
{
	int i;

	printk("misc: rmask[%08lx] status[%08lx] pc[%08lx]\n",
	       (unsigned long) c->rmask,
	       (unsigned long) c->cp0_status,
	       (unsigned long) c->pc);
	printk("regs: ");
	for(i = 0; i < 16; i++)
		printk("[%d]<%08lx> ", i, (unsigned long) c->regs[i]);
	printk("\nregs: ");
	for(i = 16; i < 32; i++)
		printk("[%d]<%08lx> ", i, (unsigned long) c->regs[i]);
	printk("\nfpregs: ");
	for(i = 0; i < 16; i++)
		printk("[%d]<%08lx> ", i, (unsigned long) c->fpregs[i]);
	printk("\nfpregs: ");
	for(i = 16; i < 32; i++)
		printk("[%d]<%08lx> ", i, (unsigned long) c->fpregs[i]);
	printk("misc: usedfp[%d] fpcsr[%08lx] fpeir[%08lx] stk_flgs[%08lx]\n",
	       (int) c->usedfp, (unsigned long) c->fpcsr,
	       (unsigned long) c->fpeir, (unsigned long) c->sstk_flags);
	printk("misc: hi[%08lx] lo[%08lx] cause[%08lx] badvaddr[%08lx]\n",
	       (unsigned long) c->hi, (unsigned long) c->lo,
	       (unsigned long) c->cp0_cause, (unsigned long) c->cp0_badvaddr);
	printk("misc: sigset<0>[%08lx] sigset<1>[%08lx] sigset<2>[%08lx] "
	       "sigset<3>[%08lx]\n", (unsigned long) c->sigset[0],
	       (unsigned long) c->sigset[1], (unsigned long) c->sigset[2],
	       (unsigned long) c->sigset[3]);
}
#endif

static void setup_irix_frame(struct sigaction * sa, struct pt_regs *regs,
			     int signr, unsigned long oldmask)
{
	unsigned long sp;
	struct sigctx_irix5 *ctx;
	int i;

	sp = regs->regs[29];
	sp -= sizeof(struct sigctx_irix5);
	sp &= ~(0xf);
	ctx = (struct sigctx_irix5 *) sp;
	if (!access_ok(VERIFY_WRITE, ctx, sizeof(*ctx)))
		goto segv_and_exit;

	__put_user(0, &ctx->weird_fpu_thing);
	__put_user(~(0x00000001), &ctx->rmask);
	__put_user(0, &ctx->regs[0]);
	for(i = 1; i < 32; i++)
		__put_user((u64) regs->regs[i], &ctx->regs[i]);

	__put_user((u64) regs->hi, &ctx->hi);
	__put_user((u64) regs->lo, &ctx->lo);
	__put_user((u64) regs->cp0_epc, &ctx->pc);
	__put_user(current->used_math, &ctx->usedfp);
	__put_user((u64) regs->cp0_cause, &ctx->cp0_cause);
	__put_user((u64) regs->cp0_badvaddr, &ctx->cp0_badvaddr);

	__put_user(0, &ctx->sstk_flags); /* XXX sigstack unimp... todo... */

	__put_user(0, &ctx->sigset[1]);
	__put_user(0, &ctx->sigset[2]);
	__put_user(0, &ctx->sigset[3]);
	__put_user(oldmask, &ctx->sigset[0]);

#ifdef DEBUG_SIG
	dump_irix5_sigctx(ctx);
#endif

	regs->regs[5] = 0; /* XXX sigcode XXX */
	regs->regs[4] = (unsigned long) signr;
	regs->regs[6] = regs->regs[29] = sp;
	regs->regs[7] = (unsigned long) sa->sa_handler;
	regs->regs[25] = regs->cp0_epc = current->tss.irix_trampoline;
	return;

segv_and_exit:
	lock_kernel();
	do_exit(SIGSEGV);
	unlock_kernel();
}

asmlinkage int sys_wait4(pid_t pid, unsigned long *stat_addr,
                         int options, unsigned long *ru);

asmlinkage int do_irix_signal(unsigned long oldmask, struct pt_regs * regs)
{
	unsigned long mask = ~current->blocked;
	unsigned long handler_signal = 0;
	unsigned long signr;
	struct sigaction * sa;

#ifdef DEBUG_SIG
	printk("[%s:%d] Delivering IRIX signal oldmask=%08lx\n",
	       current->comm, current->pid, oldmask);
#endif
	while ((signr = current->signal & mask)) {
		signr = ffz(~signr);
		clear_bit(signr, &current->signal);
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
					notify_parent(current, SIGCHLD);
				schedule();
				continue;

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGIOT: case SIGFPE: case SIGSEGV: case SIGBUS:
				lock_kernel();
				if (current->binfmt && current->binfmt->core_dump) {
					if (current->binfmt->core_dump(signr, regs))
						signr |= 0x80;
				}
				unlock_kernel();
				/* fall through */
			default:
				current->signal |= _S(signr & 0x7f);
				current->flags |= PF_SIGNALED;
				lock_kernel(); /* 8-( */
				do_exit(signr);
				unlock_kernel();
			}
		}
		/*
		 * OK, we're invoking a handler
		 */
		if (regs->orig_reg2 >= 0) {
			if (regs->regs[2] == ERESTARTNOHAND) {
				regs->regs[2] = EINTR;
			} else if((regs->regs[2] == ERESTARTSYS &&
				   !(sa->sa_flags & SA_RESTART))) {
				regs->regs[2] = regs->orig_reg2;
				regs->cp0_epc -= 8;
			}
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
	    (regs->regs[2] == ERESTARTNOHAND ||
	     regs->regs[2] == ERESTARTSYS ||
	     regs->regs[2] == ERESTARTNOINTR)) {
		regs->regs[2] = regs->orig_reg2;
		regs->cp0_epc -= 8;
	}
	if (!handler_signal)		/* no handler will be called - return 0 */
		return 0;
	signr = 1;
	sa = current->sig->action;
	for (mask = 1 ; mask ; sa++,signr++,mask += mask) {
		if (mask > handler_signal)
			break;
		if (!(mask & handler_signal))
			continue;
		setup_irix_frame(sa, regs, signr, oldmask);
		if (sa->sa_flags & SA_ONESHOT)
			sa->sa_handler = NULL;
		current->blocked |= sa->sa_mask;
		oldmask |= sa->sa_mask;
	}

	return 1;
}

asmlinkage unsigned long irix_sigreturn(struct pt_regs *regs)
{
	struct sigctx_irix5 *context, *magic;
	unsigned long umask, mask;
	u64 *fregs, res;
	int sig, i, base = 0;

	if(regs->regs[2] == 1000)
		base = 1;

	context = (struct sigctx_irix5 *) regs->regs[base + 4];
	magic = (struct sigctx_irix5 *) regs->regs[base + 5];
	sig = (int) regs->regs[base + 6];
#ifdef DEBUG_SIG
	printk("[%s:%d] IRIX sigreturn(scp[%p],ucp[%p],sig[%d])\n",
	       current->comm, current->pid, context, magic, sig);
#endif
	if (!context)
		context = magic;
	if (!access_ok(VERIFY_READ, context, sizeof(struct sigctx_irix5)))
		goto badframe;

#ifdef DEBUG_SIG
	dump_irix5_sigctx(context);
#endif

	__get_user(regs->cp0_epc, &context->pc);
	umask = context->rmask; mask = 2;
	for (i = 1; i < 32; i++, mask <<= 1) {
		if(umask & mask)
			__get_user(regs->regs[i], &context->regs[i]);
	}
	__get_user(regs->hi, &context->hi);
	__get_user(regs->lo, &context->lo);

	if ((umask & 1) && context->usedfp) {
		fregs = (u64 *) &current->tss.fpu;
		for(i = 0; i < 32; i++)
			fregs[i] = (u64) context->fpregs[i];
		__get_user(current->tss.fpu.hard.control, &context->fpcsr);
	}

	/* XXX do sigstack crapola here... XXX */

	regs->orig_reg2 = -1;
	__get_user(current->blocked, &context->sigset[0]);
	current->blocked &= _BLOCKABLE;
	__get_user(res, &context->regs[2]);
	return res;

badframe:
	lock_kernel();
	do_exit(SIGSEGV);
	unlock_kernel();

	return res;
}

struct sigact_irix5 {
	int flags;
	void (*handler)(int);
	u32 sigset[4];
	int _unused0[2];
};

#ifdef DEBUG_SIG
static inline void dump_sigact_irix5(struct sigact_irix5 *p)
{
	printk("<f[%d] hndlr[%08lx] msk[%08lx]>", p->flags,
	       (unsigned long) p->handler,
	       (unsigned long) p->sigset[0]);
}
#endif

static inline void check_pending(int signum)
{
	struct sigaction *p;

	p = signum - 1 + current->sig->action;
	spin_lock(&current->sigmask_lock);
	if (p->sa_handler == SIG_IGN) {
		current->signal &= ~_S(signum);
	} else if (p->sa_handler == SIG_DFL) {
		if (signum != SIGCONT && signum != SIGCHLD && signum != SIGWINCH)
			return;
		current->signal &= ~_S(signum);
	}	
	spin_unlock(&current->sigmask_lock);
}

asmlinkage int irix_sigaction(int sig, struct sigact_irix5 *new,
			      struct sigact_irix5 *old, unsigned long trampoline)
{
	struct sigaction new_sa, *p;
	int res;

#ifdef DEBUG_SIG
	printk(" (%d,%s,%s,%08lx) ", sig, (!new ? "0" : "NEW"),
	       (!old ? "0" : "OLD"), trampoline);
	if(new) {
		dump_sigact_irix5(new); printk(" ");
	}
#endif
	if(sig < 1 || sig > 32) {
		return -EINVAL;
	}
	p = sig - 1 + current->sig->action;

	if(new) {
		res = verify_area(VERIFY_READ, new, sizeof(*new));
		if(res)
			return res;
		if(sig == SIGKILL || sig == SIGSTOP) {
			return -EINVAL;
		}
		__get_user(new_sa.sa_flags, &new->flags);
		__get_user(new_sa.sa_handler, &(__sighandler_t) new->handler);
		__get_user(new_sa.sa_mask, &new->sigset[0]);

		if(new_sa.sa_handler != SIG_DFL && new_sa.sa_handler != SIG_IGN) {
			res = verify_area(VERIFY_READ, new_sa.sa_handler, 1);
			if(res)
				return res;
		}
	}
	/* Hmmm... methinks IRIX libc always passes a valid trampoline
	 * value for all invocations of sigaction.  Will have to
	 * investigate.  POSIX POSIX, die die die...
	 */
	current->tss.irix_trampoline = trampoline;
	if(old) {
		int res = verify_area(VERIFY_WRITE, old, sizeof(*old));
		if(res)
			return res;
		__put_user(p->sa_flags, &old->flags);
		__put_user(p->sa_handler, &old->handler);
		__put_user(p->sa_mask, &old->sigset[0]);
		__put_user(0, &old->sigset[1]);
		__put_user(0, &old->sigset[2]);
		__put_user(0, &old->sigset[3]);
		__put_user(0, &old->_unused0[0]);
		__put_user(0, &old->_unused0[1]);
	}
	if(new) {
		spin_lock_irq(&current->sig->siglock);
		*p = new_sa;
		check_pending(sig);
		spin_unlock_irq(&current->sig->siglock);
	}

	return 0;
}

asmlinkage int irix_sigpending(unsigned long *set)
{
	int res;

	lock_kernel();
	res = verify_area(VERIFY_WRITE, set, (sizeof(unsigned long) * 4));
	if(!res) {
		/* fill in "set" with signals pending but blocked. */
		spin_lock_irq(&current->sigmask_lock);
		__put_user(0, &set[1]);
		__put_user(0, &set[2]);
		__put_user(0, &set[3]);
		__put_user((current->blocked & current->signal), &set[0]);
		spin_unlock_irq(&current->sigmask_lock);
	}
	return res;
}

asmlinkage int irix_sigprocmask(int how, unsigned long *new, unsigned long *old)
{
	unsigned long bits, oldbits = current->blocked;
	int error;

	if(new) {
		error = verify_area(VERIFY_READ, new, (sizeof(unsigned long) * 4));
		if(error)
			return error;
		bits = new[0] & _BLOCKABLE;
		switch(how) {
		case 1:
			current->blocked |= bits;
			break;

		case 2:
			current->blocked &= ~bits;
			break;

		case 3:
		case 256:
			current->blocked = bits;
			break;

		default:
			return -EINVAL;
		}
	}
	if(old) {
		error = verify_area(VERIFY_WRITE, old, (sizeof(unsigned long) * 4));
		if(error)
			return error;
		__put_user(0, &old[1]);
		__put_user(0, &old[2]);
		__put_user(0, &old[3]);
		__put_user(oldbits, &old[0]);
	}

	return 0;
}

asmlinkage int irix_sigsuspend(struct pt_regs *regs)
{
	unsigned int mask;
	unsigned long *uset;
	int base = 0, error;

	if(regs->regs[2] == 1000)
		base = 1;

	uset = (unsigned long *) regs->regs[base + 4];
	if(verify_area(VERIFY_READ, uset, (sizeof(unsigned long) * 4)))
		return -EFAULT;
	mask = current->blocked;

	current->blocked = uset[0] & _BLOCKABLE;
	while(1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if(do_irix_signal(mask, regs))
			return -EINTR;
	}
	return error;
}

/* hate hate hate... */
struct irix5_siginfo {
	int sig, code, error;
	union {
		char unused[128 - (3 * 4)]; /* Safety net. */
		struct {
			int pid;
			union {
				int uid;
				struct {
					int utime, status, stime;
				} child;
			} procdata;
		} procinfo;

		unsigned long fault_addr;

		struct {
			int fd;
			long band;
		} fileinfo;

		unsigned long sigval;
	} stuff;
};

static inline unsigned long timespectojiffies(struct timespec *value)
{
	unsigned long sec = (unsigned) value->tv_sec;
	long nsec = value->tv_nsec;

	if (sec > (LONG_MAX / HZ))
		return LONG_MAX;
	nsec += 1000000000L / HZ - 1;
	nsec /= 1000000000L / HZ;
	return HZ * sec + nsec;
}

asmlinkage int irix_sigpoll_sys(unsigned long *set, struct irix5_siginfo *info,
				struct timespec *tp)
{
	unsigned long mask, kset, expire = 0;
	int sig, error, timeo = 0;

	lock_kernel();
#ifdef DEBUG_SIG
	printk("[%s:%d] irix_sigpoll_sys(%p,%p,%p)\n",
	       current->comm, current->pid, set, info, tp);
#endif

	/* Must always specify the signal set. */
	if(!set)
		return -EINVAL;

	error = get_user(kset, &set[0]);
	if(error)
		goto out;

	if(info && clear_user(info, sizeof(*info))) {
		error = -EFAULT;
		goto out;
	}

	if(tp) {
		error = verify_area(VERIFY_READ, tp, sizeof(*tp));
		if(error)
			return error;
		if(!tp->tv_sec && !tp->tv_nsec) {
			error = -EINVAL;
			goto out;
		}
		expire = timespectojiffies(tp)+(tp->tv_sec||tp->tv_nsec)+jiffies;
		current->timeout = expire;
	}

	while(1) {
		current->state = TASK_INTERRUPTIBLE; schedule();
		if(current->signal & kset) break;
		if(tp && expire <= jiffies) {
			timeo = 1;
			break;
		}
		if(signal_pending(current)) return -EINTR;
	}

	if(timeo) return -EAGAIN;
	for(sig = 1, mask = 2; mask; mask <<= 1, sig++) {
		if(!(mask & kset)) continue;
		if(mask & current->signal) {
			/* XXX need more than this... */
			if(info) info->sig = sig;
			error = 0;
			goto out;
		}
	}

	/* Should not get here, but do something sane if we do. */
	error = -EINTR;

out:
	unlock_kernel();
	return error;
}

/* This is here because of irix5_siginfo definition. */
#define P_PID    0
#define P_PGID   2
#define P_ALL    7

extern int getrusage(struct task_struct *, int, struct rusage *);

#define W_EXITED     1
#define W_TRAPPED    2
#define W_STOPPED    4
#define W_CONT       8
#define W_NOHANG    64

#define W_MASK      (W_EXITED | W_TRAPPED | W_STOPPED | W_CONT | W_NOHANG)

asmlinkage int irix_waitsys(int type, int pid, struct irix5_siginfo *info,
			    int options, struct rusage *ru)
{
	int flag, retval;
	struct wait_queue wait = { current, NULL };
	struct task_struct *p;

	lock_kernel();
	if(!info) {
		retval = -EINVAL;
		goto out;
	}
	retval = verify_area(VERIFY_WRITE, info, sizeof(*info));
	if(retval)
		goto out;
	if(ru) {
		retval = verify_area(VERIFY_WRITE, ru, sizeof(*ru));
		if(retval)
			goto out;
	}
	if(options & ~(W_MASK)) {
		retval = -EINVAL;
		goto out;
	}
	if(type != P_PID && type != P_PGID && type != P_ALL) {
		retval = -EINVAL;
		goto out;
	}
	add_wait_queue(&current->wait_chldexit, &wait);
repeat:
	flag = 0;
	for(p = current->p_cptr; p; p = p->p_osptr) {
		if((type == P_PID) && p->pid != pid)
			continue;
		if((type == P_PGID) && p->pgrp != pid)
			continue;
		if((p->exit_signal != SIGCHLD))
			continue;
		flag = 1;
		switch(p->state) {
			case TASK_STOPPED:
				if (!p->exit_code)
					continue;
				if (!(options & (W_TRAPPED|W_STOPPED)) &&
				    !(p->flags & PF_PTRACED))
					continue;
				if (ru != NULL)
					getrusage(p, RUSAGE_BOTH, ru);
				__put_user(SIGCHLD, &info->sig);
				__put_user(0, &info->code);
				__put_user(p->pid, &info->stuff.procinfo.pid);
				__put_user((p->exit_code >> 8) & 0xff,
				           &info->stuff.procinfo.procdata.child.status);
				__put_user(p->times.tms_utime, &info->stuff.procinfo.procdata.child.utime);
				__put_user(p->times.tms_stime, &info->stuff.procinfo.procdata.child.stime);
				p->exit_code = 0;
				retval = 0;
				goto end_waitsys;
			case TASK_ZOMBIE:
				current->times.tms_cutime += p->times.tms_utime + p->times.tms_cutime;
				current->times.tms_cstime += p->times.tms_stime + p->times.tms_cstime;
				if (ru != NULL)
					getrusage(p, RUSAGE_BOTH, ru);
				__put_user(SIGCHLD, &info->sig);
				__put_user(1, &info->code);      /* CLD_EXITED */
				__put_user(p->pid, &info->stuff.procinfo.pid);
				__put_user((p->exit_code >> 8) & 0xff,
				           &info->stuff.procinfo.procdata.child.status);
				__put_user(p->times.tms_utime,
				           &info->stuff.procinfo.procdata.child.utime);
				__put_user(p->times.tms_stime,
				           &info->stuff.procinfo.procdata.child.stime);
				retval = 0;
				if (p->p_opptr != p->p_pptr) {
					REMOVE_LINKS(p);
					p->p_pptr = p->p_opptr;
					SET_LINKS(p);
					notify_parent(p, SIGCHLD);
				} else
					release(p);
				goto end_waitsys;
			default:
				continue;
		}
	}
	if(flag) {
		retval = 0;
		if(options & W_NOHANG)
			goto end_waitsys;
		retval = -ERESTARTSYS;
		if(signal_pending(current))
			goto end_waitsys;
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		goto repeat;
	}
	retval = -ECHILD;
end_waitsys:
	remove_wait_queue(&current->wait_chldexit, &wait);

out:
	unlock_kernel();
	return retval;
}

struct irix5_context {
	u32 flags;
	u32 link;
	u32 sigmask[4];
	struct { u32 sp, size, flags; } stack;
	int regs[36];
	u32 fpregs[32];
	u32 fpcsr;
	u32 _unused0;
	u32 _unused1[47];
	u32 weird_graphics_thing;
};

asmlinkage int irix_getcontext(struct pt_regs *regs)
{
	int error, i, base = 0;
	struct irix5_context *ctx;

	lock_kernel();
	if(regs->regs[2] == 1000)
		base = 1;
	ctx = (struct irix5_context *) regs->regs[base + 4];

#ifdef DEBUG_SIG
	printk("[%s:%d] irix_getcontext(%p)\n",
	       current->comm, current->pid, ctx);
#endif

	error = verify_area(VERIFY_WRITE, ctx, sizeof(*ctx));
	if(error)
		goto out;
	ctx->flags = 0x0f;
	ctx->link = current->tss.irix_oldctx;
	ctx->sigmask[1] = ctx->sigmask[2] = ctx->sigmask[4] = 0;
	ctx->sigmask[0] = current->blocked;

	/* XXX Do sigstack stuff someday... */
	ctx->stack.sp = ctx->stack.size = ctx->stack.flags = 0;

	ctx->weird_graphics_thing = 0;
	ctx->regs[0] = 0;
	for(i = 1; i < 32; i++)
		ctx->regs[i] = regs->regs[i];
	ctx->regs[32] = regs->lo;
	ctx->regs[33] = regs->hi;
	ctx->regs[34] = regs->cp0_cause;
	ctx->regs[35] = regs->cp0_epc;
	if(!current->used_math) {
		ctx->flags &= ~(0x08);
	} else {
		/* XXX wheee... */
		printk("Wheee, no code for saving IRIX FPU context yet.\n");
	}
	error = 0;

out:
	unlock_kernel();
	return error;
}

asmlinkage unsigned long irix_setcontext(struct pt_regs *regs)
{
	int error, base = 0;
	struct irix5_context *ctx;

	lock_kernel();
	if(regs->regs[2] == 1000)
		base = 1;
	ctx = (struct irix5_context *) regs->regs[base + 4];

#ifdef DEBUG_SIG
	printk("[%s:%d] irix_setcontext(%p)\n",
	       current->comm, current->pid, ctx);
#endif

	error = verify_area(VERIFY_READ, ctx, sizeof(*ctx));
	if(error)
		goto out;

	if(ctx->flags & 0x02) {
		/* XXX sigstack garbage, todo... */
		printk("Wheee, cannot do sigstack stuff in setcontext\n");
	}

	if(ctx->flags & 0x04) {
		int i;

		/* XXX extra control block stuff... todo... */
		for(i = 1; i < 32; i++)
			regs->regs[i] = ctx->regs[i];
		regs->lo = ctx->regs[32];
		regs->hi = ctx->regs[33];
		regs->cp0_epc = ctx->regs[35];
	}

	if(ctx->flags & 0x08) {
		/* XXX fpu context, blah... */
		printk("Wheee, cannot restore FPU context yet...\n");
	}
	current->tss.irix_oldctx = ctx->link;
	error = regs->regs[2];

out:
	unlock_kernel();
	return error;
}

struct irix_sigstack { unsigned long sp; int status; };

asmlinkage int irix_sigstack(struct irix_sigstack *new, struct irix_sigstack *old)
{
	int error;

	lock_kernel();
#ifdef DEBUG_SIG
	printk("[%s:%d] irix_sigstack(%p,%p)\n",
	       current->comm, current->pid, new, old);
#endif
	if(new) {
		error = verify_area(VERIFY_READ, new, sizeof(*new));
		if(error)
			goto out;
	}

	if(old) {
		error = verify_area(VERIFY_WRITE, old, sizeof(*old));
		if(error)
			goto out;
	}
	error = 0;
out:
	unlock_kernel();
	return error;
}

struct irix_sigaltstack { unsigned long sp; int size; int status; };

asmlinkage int irix_sigaltstack(struct irix_sigaltstack *new,
				struct irix_sigaltstack *old)
{
	int error;

	lock_kernel();
#ifdef DEBUG_SIG
	printk("[%s:%d] irix_sigaltstack(%p,%p)\n",
	       current->comm, current->pid, new, old);
#endif
	if(new) {
		error = verify_area(VERIFY_READ, new, sizeof(*new));
		if(error)
			goto out;
	}

	if(old) {
		error = verify_area(VERIFY_WRITE, old, sizeof(*old));
		if(error)
			goto out;
	}
	error = 0;

out:
	error = 0;
	unlock_kernel();

	return error;
}

struct irix_procset {
	int cmd, ltype, lid, rtype, rid;
};

asmlinkage int irix_sigsendset(struct irix_procset *pset, int sig)
{
	int error;

	lock_kernel();
	error = verify_area(VERIFY_READ, pset, sizeof(*pset));
	if(error)
		goto out;
#ifdef DEBUG_SIG
	printk("[%s:%d] irix_sigsendset([%d,%d,%d,%d,%d],%d)\n",
	       current->comm, current->pid,
	       pset->cmd, pset->ltype, pset->lid, pset->rtype, pset->rid,
	       sig);
#endif

	error = -EINVAL;

out:
	unlock_kernel();
	return error;
}

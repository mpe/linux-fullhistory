/*
 *  linux/arch/m68k/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * 680x0 support by Hamish Macdonald
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
#include <asm/pgtable.h>
#include <asm/traps.h>
#include <asm/bootinfo.h>

#define offsetof(type, member)  ((size_t)(&((type *)0)->member))

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

asmlinkage int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options);
asmlinkage int do_signal(unsigned long oldmask, struct pt_regs *regs);

static const int extra_sizes[16] = {
  0,
  -1, /* sizeof(((struct frame *)0)->un.fmt1), */
  sizeof(((struct frame *)0)->un.fmt2),
  sizeof(((struct frame *)0)->un.fmt3),
  sizeof(((struct frame *)0)->un.fmt4),
  -1, /* sizeof(((struct frame *)0)->un.fmt5), */
  -1, /* sizeof(((struct frame *)0)->un.fmt6), */
  sizeof(((struct frame *)0)->un.fmt7),
  -1, /* sizeof(((struct frame *)0)->un.fmt8), */
  sizeof(((struct frame *)0)->un.fmt9),
  sizeof(((struct frame *)0)->un.fmta),
  sizeof(((struct frame *)0)->un.fmtb),
  -1, /* sizeof(((struct frame *)0)->un.fmtc), */
  -1, /* sizeof(((struct frame *)0)->un.fmtd), */
  -1, /* sizeof(((struct frame *)0)->un.fmte), */
  -1, /* sizeof(((struct frame *)0)->un.fmtf), */
};

/*
 * atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int do_sigsuspend(struct pt_regs *regs)
{
	unsigned long oldmask = current->blocked;
	unsigned long newmask = regs->d3;

	current->blocked = newmask & _BLOCKABLE;
	regs->d0 = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(oldmask, regs))
			return -EINTR;
	}
}

static unsigned char fpu_version = 0;	/* version number of fpu, set by setup_frame */

/*
 * This sets regs->usp even though we don't actually use sigstacks yet..
 */
asmlinkage int do_sigreturn(unsigned long __unused)
{
	struct sigcontext_struct context;
	struct frame * regs;
	struct switch_stack *sw;
	int fsize = 0;
	int formatvec = 0;
	unsigned long fp;
	unsigned long usp = rdusp();

#if 0
	printk("sys_sigreturn, usp=%08x\n", (unsigned) usp);
#endif

	/* get stack frame pointer */
	sw = (struct switch_stack *) &__unused;
	regs = (struct frame *) (sw + 1);

	/* get previous context (including pointer to possible extra junk) */
        if (verify_area(VERIFY_READ, (void *)usp, sizeof(context)))
                goto badframe;

	memcpy_fromfs(&context,(void *)usp, sizeof(context));

	fp = usp + sizeof (context);

	/* restore signal mask */
	current->blocked = context.sc_mask & _BLOCKABLE;

	/* restore passed registers */
	regs->ptregs.d0 = context.sc_d0;
	regs->ptregs.d1 = context.sc_d1;
	regs->ptregs.a0 = context.sc_a0;
	regs->ptregs.a1 = context.sc_a1;
	regs->ptregs.sr = (regs->ptregs.sr & 0xff00)|(context.sc_sr & 0xff);
	regs->ptregs.pc = context.sc_pc;

	wrusp(context.sc_usp);
	formatvec = context.sc_formatvec;
	regs->ptregs.format = formatvec >> 12;
	regs->ptregs.vector = formatvec & 0xfff;
	if (context.sc_fpstate[0])
	  {
	    /* Verify the frame format.  */
	    if (context.sc_fpstate[0] != fpu_version){
#if DEBUG
	    printk("fpregs=%08x fpcntl=%08x\n", context.sc_fpregs,
		   context.sc_fpcntl); 
	    printk("Wrong fpu: sc_fpstate[0]=%02x fpu_version=%02x\n",
		   (unsigned) context.sc_fpstate[0], (unsigned) fpu_version);
	    {
	      int i;
	      printk("Saved fp_state: ");
	      for (i = 0; i < 216; i++){
		printk("%02x ", context.sc_fpstate[i]);
	      }
	      printk("\n");
	    }
#endif
	      goto badframe;
	    }
	    if (boot_info.cputype & FPU_68881)
	      {
		if (context.sc_fpstate[1] != 0x18
		    && context.sc_fpstate[1] != 0xb4)
		  goto badframe;
	      }
	    else if (boot_info.cputype & FPU_68882)
	      {
		if (context.sc_fpstate[1] != 0x38
		    && context.sc_fpstate[1] != 0xd4){
#if 0
		  printk("Wrong 68882 fpu-state\n");
#endif
		  goto badframe;
		}
	      }
	    else if (boot_info.cputype & FPU_68040)
	      {
		if (!((context.sc_fpstate[1] == 0x00)|| \
                      (context.sc_fpstate[1] == 0x28)|| \
                      (context.sc_fpstate[1] == 0x60))){
#if 0
		  printk("Wrong 68040 fpu-state\n");
#endif
		  goto badframe;
		}
	      }
	    else if (boot_info.cputype & FPU_68060)
	      {
		if (!((context.sc_fpstate[1] == 0x00)|| \
                      (context.sc_fpstate[1] == 0x60)|| \
                      (context.sc_fpstate[1] == 0xe0))){
#if 0
		  printk("Wrong 68060 fpu-state\n");
#endif
		  goto badframe;
		}
	      }
	    __asm__ volatile ("fmovemx %0,%/fp0-%/fp1\n\t"
			      "fmoveml %1,%/fpcr/%/fpsr/%/fpiar"
			      : /* no outputs */
			      : "m" (*context.sc_fpregs),
				"m" (*context.sc_fpcntl));
	  }
	__asm__ volatile ("frestore %0" : : "m" (*context.sc_fpstate));

	fsize = extra_sizes[regs->ptregs.format];
	if (fsize < 0) {
		/*
		 * user process trying to return with weird frame format
		 */
#if DEBUG
	      printk("user process returning with weird frame format\n");
#endif
		goto badframe;
	}

	/* OK.	Make room on the supervisor stack for the extra junk,
	 * if necessary.
	 */

	if (fsize) {
		if (verify_area(VERIFY_READ, (void *)fp, fsize))
                        goto badframe;

#define frame_offset (sizeof(struct pt_regs)+sizeof(struct switch_stack))
		__asm__ __volatile__
			("movel %0,%/a0\n\t"
			 "subl %1,%/a0\n\t"     /* make room on stack */
			 "movel %/a0,%/sp\n\t"  /* set stack pointer */
			 /* move switch_stack and pt_regs */
			 "1: movel %0@+,%/a0@+\n\t"
			 "   dbra %2,1b\n\t"
			 "lea %/sp@(%c3),%/a0\n\t" /* add offset of fmt stuff */
			 "lsrl  #2,%1\n\t"
			 "subql #1,%1\n\t"
			 "2: movesl %4@+,%2\n\t"
			 "   movel %2,%/a0@+\n\t"
			 "   dbra %1,2b\n\t"
			 "bral " SYMBOL_NAME_STR(ret_from_signal)
			 : /* no outputs, it doesn't ever return */
			 : "a" (sw), "d" (fsize), "d" (frame_offset/4-1),
			   "n" (frame_offset), "a" (fp)
			 : "a0");
#undef frame_offset
		goto badframe;
		/* NOTREACHED */
	}

	return regs->ptregs.d0;
badframe:
        do_exit(SIGSEGV);
}

/*
 * Set up a signal frame...
 *
 * This routine is somewhat complicated by the fact that if the
 * kernel may be entered by an exception other than a system call;
 * e.g. a bus error or other "bad" exception.  If this is the case,
 * then *all* the context on the kernel stack frame must be saved.
 *
 * For a large number of exceptions, the stack frame format is the same
 * as that which will be created when the process traps back to the kernel
 * when finished executing the signal handler.	In this case, nothing
 * must be done.  This is exception frame format "0".  For exception frame
 * formats "2", "9", "A" and "B", the extra information on the frame must
 * be saved.  This information is saved on the user stack and restored
 * when the signal handler is returned.
 *
 * The format of the user stack when executing the signal handler is:
 *
 *     usp ->  RETADDR (points to code below)
 *	       signum  (parm #1)
 *	       sigcode (parm #2 ; vector number)
 *	       scp     (parm #3 ; sigcontext pointer, pointer to #1 below)
 *	       code1   (addaw #20,sp) ; pop parms and code off stack
 *	       code2   (moveq #119,d0; trap #0) ; sigreturn syscall
 *     #1|     oldmask
 *	 |     old usp
 *	 |     d0      (first saved reg)
 *	 |     d1
 *	 |     a0
 *	 |     a1
 *	 |     sr      (saved status register)
 *	 |     pc      (old pc; one to return to)
 *	 |     forvec  (format and vector word of old supervisor stack frame)
 *	 |     floating point context
 *
 * These are optionally followed by some extra stuff, depending on the
 * stack frame interrupted. This is 1 longword for format "2", 3
 * longwords for format "9", 6 longwords for format "A", and 21
 * longwords for format "B".
 */

#define UFRAME_SIZE(fs) (sizeof(struct sigcontext_struct)/4 + 6 + fs/4)

static void setup_frame (struct sigaction * sa, unsigned long **fp,
			 unsigned long pc, struct frame *regs, int
			 signr, unsigned long oldmask)
{
	struct sigcontext_struct context;
	unsigned long *frame, *tframe;
	int fsize = extra_sizes[regs->ptregs.format];

	if (fsize < 0) {
		printk ("setup_frame: Unknown frame format %#x\n",
			regs->ptregs.format);
		do_exit(SIGSEGV);
	}
	frame = *fp - UFRAME_SIZE(fsize);
	if (verify_area(VERIFY_WRITE,frame,UFRAME_SIZE(fsize)*4))
		do_exit(SIGSEGV);
	if (fsize) {
		memcpy_tofs (frame + UFRAME_SIZE(0), &regs->un, fsize);
		regs->ptregs.stkadj = fsize;
	}

/* set up the "normal" stack seen by the signal handler */
	tframe = frame;

	/* return address points to code on stack */
	put_user((ulong)(frame+4), tframe); tframe++;
	if (current->exec_domain && current->exec_domain->signal_invmap)
	    put_user(current->exec_domain->signal_invmap[signr], tframe);
	else
	    put_user(signr, tframe);
	tframe++;

	put_user(regs->ptregs.vector, tframe); tframe++;
	/* "scp" parameter.  points to sigcontext */
	put_user((ulong)(frame+6), tframe); tframe++;

/* set up the return code... */
	put_user(0xdefc0014,tframe); tframe++; /* addaw #20,sp */
	put_user(0x70774e40,tframe); tframe++; /* moveq #119,d0; trap #0 */

/* Flush caches so the instructions will be correctly executed. (MA) */
	cache_push_v ((unsigned long)frame, (int)tframe - (int)frame);

/* setup and copy the sigcontext structure */
	context.sc_mask       = oldmask;
	context.sc_usp	      = (unsigned long)*fp;
	context.sc_d0	      = regs->ptregs.d0;
	context.sc_d1	      = regs->ptregs.d1;
	context.sc_a0	      = regs->ptregs.a0;
	context.sc_a1	      = regs->ptregs.a1;
	context.sc_sr	      = regs->ptregs.sr;
	context.sc_pc	      = pc;
	context.sc_formatvec  = (regs->ptregs.format << 12 |
				 regs->ptregs.vector);
#if DEBUG
	printk("formatvec: %02x\n", (unsigned) context.sc_formatvec);
#endif
	__asm__ volatile ("fsave %0" : : "m" (*context.sc_fpstate) : "memory");
	if (context.sc_fpstate[0])
	  {
	    fpu_version = context.sc_fpstate[0];
#if DEBUG
	    {
	      int i;
	      printk("Saved fp_state: ");
	      for (i = 0; i < 216; i++){
		printk("%02x ", context.sc_fpstate[i]);
	      }
	      printk("\n");
	    }
	    printk("fpregs=%08x fpcntl=%08x\n", context.sc_fpregs,
		   context.sc_fpcntl); 
#endif
	    __asm__ volatile ("fmovemx %/fp0-%/fp1,%0\n\t"
			      "fmoveml %/fpcr/%/fpsr/%/fpiar,%1"
			      : /* no outputs */
			      : "m" (*context.sc_fpregs),
				"m" (*context.sc_fpcntl)
			      : "memory");
	  }
#if DEBUG
	{
	  int i;
	  printk("Saved fp_state: ");
	  for (i = 0; i < 216; i++){
	    printk("%02x ", context.sc_fpstate[i]);
	  }
	  printk("\n");
	}
#endif
	memcpy_tofs (tframe, &context, sizeof(context));
	/*
	 * no matter what frame format we were using before, we
	 * will do the "RTE" using a normal 4 word frame.
	 */
	regs->ptregs.format = 0;

	/* "return" new usp to caller */
	*fp = frame;
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals
 * that the kernel can handle, and then we build all the user-level signal
 * handling stack-frames in one go after that.
 */
asmlinkage int do_signal(unsigned long oldmask, struct pt_regs *regs_in)
{
	unsigned long mask = ~current->blocked;
	unsigned long handler_signal = 0;
	unsigned long *frame = NULL;
	unsigned long pc = 0;
	unsigned long signr;
	struct frame *regs = (struct frame *)regs_in;
	struct sigaction * sa;

	current->tss.esp0 = (unsigned long) regs;

	while ((signr = current->signal & mask)) {
		__asm__("bfffo  %2,#0,#0,%1\n\t"
			"bfclr  %0,%1,#1\n\t"
			"eorw   #31,%1"
			:"=m" (current->signal),"=r" (signr)
			:"1" (signr));
		sa = current->sig->action + signr;
		signr++;

		if ((current->flags & PF_PTRACED) && signr != SIGKILL) {
			current->exit_code = signr;
			current->state = TASK_STOPPED;
			notify_parent(current);
			schedule();
			if (!(signr = current->exit_code)) {
			discard_frame:
				/* Make sure that a faulted bus cycle
				   isn't restarted.  */
				switch (regs->ptregs.format) {
				case 7:
				case 9:
				case 10:
				case 11:
				  regs->ptregs.stkadj = extra_sizes[regs->ptregs.format];
				  regs->ptregs.format = 0;
				  break;
				}
				continue;
			}
			current->exit_code = 0;
			if (signr == SIGSTOP)
				goto discard_frame;
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
			    case SIGIOT: case SIGFPE: case SIGSEGV:
				if (current->binfmt && current->binfmt->core_dump) {
				    if (current->binfmt->core_dump(signr, (struct pt_regs *)regs))
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
		if (regs->ptregs.orig_d0 >= 0) {
		  if (regs->ptregs.d0 == -ERESTARTNOHAND ||
		      (regs->ptregs.d0 == -ERESTARTSYS &&
		       !(sa->sa_flags & SA_RESTART)))
		    regs->ptregs.d0 = -EINTR;
		}
		handler_signal |= 1 << (signr-1);
		mask &= ~sa->sa_mask;
	}
	if (regs->ptregs.orig_d0 >= 0 &&
	    (regs->ptregs.d0 == -ERESTARTNOHAND ||
	     regs->ptregs.d0 == -ERESTARTSYS ||
	     regs->ptregs.d0 == -ERESTARTNOINTR)) {
		regs->ptregs.d0 = regs->ptregs.orig_d0;
		regs->ptregs.pc -= 2;
	}
	if (!handler_signal)    /* no handler will be called - return 0 */
	  {
	    /* If we are about to discard some frame stuff we must
	       copy over the remaining frame. */
	    if (regs->ptregs.stkadj)
	      {
		struct frame *tregs =
		  (struct frame *) ((ulong) regs + regs->ptregs.stkadj);

		/* This must be copied with decreasing addresses to
		   handle overlaps.  */
		tregs->ptregs.vector = regs->ptregs.vector;
		tregs->ptregs.format = regs->ptregs.format;
		tregs->ptregs.pc = regs->ptregs.pc;
		tregs->ptregs.sr = regs->ptregs.sr;
	      }
	    return 0;
	  }
	pc = regs->ptregs.pc;
	frame = (unsigned long *)rdusp();
	signr = 1;
	sa = current->sig->action;
	for (mask = 1 ; mask ; sa++,signr++,mask += mask) {
		if (mask > handler_signal)
			break;
		if (!(mask & handler_signal))
			continue;
		setup_frame(sa,&frame,pc,regs,signr,oldmask);
		pc = (unsigned long) sa->sa_handler;
		if (sa->sa_flags & SA_ONESHOT)
			sa->sa_handler = NULL;
/* force a supervisor-mode page-in of the signal handler to reduce races */
		__asm__ __volatile__("movesb %0,%/d0": :"m" (*(char *)pc):"d0");
		current->blocked |= sa->sa_mask;
		oldmask |= sa->sa_mask;
	}
	wrusp((unsigned long)frame);
	regs->ptregs.pc = pc;

	/*
	 * if setup_frame saved some extra frame junk, we need to
	 * skip over that stuff when doing the RTE.  This means we have
	 * to move the machine portion of the stack frame to where the
	 * "RTE" instruction expects it. The signal that we need to
	 * do this is that regs->stkadj is nonzero.
	 */
	if (regs->ptregs.stkadj) {
		struct frame *tregs =
			(struct frame *)((ulong)regs + regs->ptregs.stkadj);
#if DEBUG
	  printk("Performing stackadjust=%04x\n", (unsigned)
		 regs->ptregs.stkadj);
#endif
		/* This must be copied with decreasing addresses to
                   handle overlaps.  */
		tregs->ptregs.vector = regs->ptregs.vector;
		tregs->ptregs.format = regs->ptregs.format;
		tregs->ptregs.pc = regs->ptregs.pc;
		tregs->ptregs.sr = regs->ptregs.sr;
	}

	return 1;
}

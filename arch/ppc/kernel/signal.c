/*
 *  linux/arch/ppc/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Adapted for PowerPC by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu) 
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <asm/uaccess.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

asmlinkage int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options);

/*
 * atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int sys_sigsuspend(unsigned long set, int p2, int p3, int p4, int p6, int p7, struct pt_regs *regs)
{
	unsigned long mask;

	mask = current->blocked;
	current->blocked = set & _BLOCKABLE;
	regs->gpr[3] = -EINTR;
#if 0
printk("Task: %x[%d] - SIGSUSPEND at %x, Mask: %x\n", current, current->pid, regs->nip, set);	
#endif
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(mask,regs))
			return -EINTR;
	}
}

asmlinkage int sys_sigreturn(struct pt_regs *regs)
{
	struct sigcontext_struct *sc;
	struct pt_regs *int_regs;
	int signo;
#if 1 	
	if (verify_area(VERIFY_READ, (void *) regs->gpr[1], sizeof(sc))
	    || (regs->gpr[1] >=KERNELBASE))
		goto badframe;
#endif	
	sc = (struct sigcontext_struct *)regs->gpr[1];
	current->blocked = sc->oldmask & _BLOCKABLE;
	int_regs = sc->regs;
	signo = sc->signal;
	sc++;  /* Pop signal 'context' */
	if (sc == (struct sigcontext_struct *)(int_regs))
	{ /* Last stacked signal */
#if 0	
		/* This doesn't work - it blows away the return address! */
		memcpy(regs, int_regs, sizeof(*regs));
#else
		/* Don't mess up 'my' stack frame */
		memcpy(&regs->gpr, &int_regs->gpr, sizeof(*regs)-sizeof(regs->_overhead));
#endif		
		if ((int)regs->orig_gpr3 >= 0 &&
		    ((int)regs->result == -ERESTARTNOHAND ||
		     (int)regs->result == -ERESTARTSYS ||
		     (int)regs->result == -ERESTARTNOINTR))
		{
			regs->gpr[3] = regs->orig_gpr3;
			regs->nip -= 4; /* Back up & retry system call */
			regs->result = 0;
		}
		return (regs->result);
	} else
	{ /* More signals to go */
		regs->gpr[1] = (unsigned long)sc;
		regs->gpr[3] = sc->signal;
		regs->gpr[4] = sc->regs;
		regs->link = (unsigned long)((sc->regs)+1);
		regs->nip = sc->handler;
		return (sc->signal);
	}
badframe:
	/*printk("sys_sigreturn(): badstack regs %x cur %s/%d\n",
	 regs,current->comm,current->pid);*/
  do_exit(SIGSEGV);
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
  unsigned long *trampoline;
  unsigned long *regs_ptr;
  unsigned long nip = 0;
  unsigned long signr;
  int bitno;
  struct sigcontext_struct *sc;
  struct sigaction * sa;
  int s;

  while ((signr = current->signal & mask)) {
    for (bitno = 0;  bitno < 32;  bitno++)
    {
      if (signr & (1<<bitno)) break;
    }
    signr = bitno;

    current->signal &= ~(1<<signr);  /* Clear bit */
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
    /* handle signal */
    
    if ((int)regs->orig_gpr3 >= 0) {
      if ((int)regs->result == -ERESTARTNOHAND ||
	  ((int)regs->result == -ERESTARTSYS && !(sa->sa_flags & SA_RESTART)))
	(int)regs->result = -EINTR;
    }
    handler_signal |= 1 << (signr-1);
    mask &= ~sa->sa_mask;
  }
  if (!handler_signal)		/* no handler will be called - return 0 */
  {
    return 0;
  }


  nip = regs->nip;
  frame = (unsigned long *) regs->gpr[1];
  /* Build trampoline code on stack */
  frame -= 2;
  trampoline = frame;
#if 1
  /* verify stack is valid for writing regs struct */
  if (verify_area(VERIFY_WRITE,(void *)frame, sizeof(long)*2+sizeof(*regs))
      || (frame >= KERNELBASE ))
    goto badframe;
#endif
  trampoline[0] = 0x38007777;  /* li r0,0x7777 */
  trampoline[1] = 0x44000002;  /* sc           */
  frame -= sizeof(*regs) / sizeof(long);
  regs_ptr = frame;
  memcpy(regs_ptr, regs, sizeof(*regs));
  signr = 1;
  sa = current->sig->action;
  
  
  for (mask = 1 ; mask ; sa++,signr++,mask += mask) {
    if (mask > handler_signal)
      break;
    if (!(mask & handler_signal))
      continue;

    frame -= sizeof(struct sigcontext_struct) / sizeof(long);
#if 1
    if (verify_area(VERIFY_WRITE,(void *)frame,
		    sizeof(struct sigcontext_struct)/sizeof(long)))
      goto badframe;
#endif    
    sc = (struct sigcontext_struct *)frame;
    nip = (unsigned long) sa->sa_handler;
#if 0 /* Old compiler */		
    nip = *(unsigned long *)nip;
#endif		
    if (sa->sa_flags & SA_ONESHOT)
      sa->sa_handler = NULL;
    sc->handler = nip;
    sc->oldmask = current->blocked;
    sc->regs = (unsigned long)regs_ptr;
    sc->signal = signr;
    current->blocked |= sa->sa_mask;
    regs->gpr[3] = signr;
    regs->gpr[4] = (unsigned long)regs_ptr;
  }
  regs->link = (unsigned long)trampoline;
  regs->nip = nip;
  regs->gpr[1] = (unsigned long)sc;
  /* The DATA cache must be flushed here to insure coherency */
  /* between the DATA & INSTRUCTION caches.  Since we just */
  /* created an instruction stream using the DATA [cache] space */
  /* and since the instruction cache will not look in the DATA */
  /* cache for new data, we have to force the data to go on to */
  /* memory and flush the instruction cache to force it to look */
  /* there.  The following function performs this magic */
  flush_instruction_cache();
  return 1;
badframe:
  /*  printk("do_signal(): badstack signr %d frame %x regs %x cur %s/%d\n",
	 signr,frame,regs,current->comm,current->pid);*/
  do_exit(SIGSEGV);
}

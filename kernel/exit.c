/*
 *  linux/kernel/exit.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#undef DEBUG_PROC_TREE

#include <linux/wait.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/resource.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>

#include <asm/segment.h>
#include <asm/pgtable.h>

extern void sem_exit (void);
extern void acct_process (long exitcode);
extern void kerneld_exit(void);

int getrusage(struct task_struct *, int, struct rusage *);

static inline void generate(unsigned long sig, struct task_struct * p)
{
	unsigned long mask = 1 << (sig-1);
	struct sigaction * sa = sig + p->sig->action - 1;

	/*
	 * Optimize away the signal, if it's a signal that can
	 * be handled immediately (ie non-blocked and untraced)
	 * and that is ignored (either explicitly or by default)
	 */
	if (!(mask & p->blocked) && !(p->flags & PF_PTRACED)) {
		/* don't bother with ignored signals (but SIGCHLD is special) */
		if (sa->sa_handler == SIG_IGN && sig != SIGCHLD)
			return;
		/* some signals are ignored by default.. (but SIGCONT already did its deed) */
		if ((sa->sa_handler == SIG_DFL) &&
		    (sig == SIGCONT || sig == SIGCHLD || sig == SIGWINCH || sig == SIGURG))
			return;
	}
	p->signal |= mask;
	if (p->state == TASK_INTERRUPTIBLE && (p->signal & ~p->blocked))
		wake_up_process(p);
}

/*
 * Force a signal that the process can't ignore: if necessary
 * we unblock the signal and change any SIG_IGN to SIG_DFL.
 */
void force_sig(unsigned long sig, struct task_struct * p)
{
	sig--;
	if (p->sig) {
		unsigned long mask = 1UL << sig;
		struct sigaction *sa = p->sig->action + sig;
		p->signal |= mask;
		p->blocked &= ~mask;
		if (sa->sa_handler == SIG_IGN)
			sa->sa_handler = SIG_DFL;
		if (p->state == TASK_INTERRUPTIBLE)
			wake_up_process(p);
	}
}
		

int send_sig(unsigned long sig,struct task_struct * p,int priv)
{
	if (!p || sig > 32)
		return -EINVAL;
	if (!priv && ((sig != SIGCONT) || (current->session != p->session)) &&
	    (current->euid ^ p->suid) && (current->euid ^ p->uid) &&
	    (current->uid ^ p->suid) && (current->uid ^ p->uid) &&
	    !suser())
		return -EPERM;
	if (!sig)
		return 0;
	/*
	 * Forget it if the process is already zombie'd.
	 */
	if (!p->sig)
		return 0;
	if ((sig == SIGKILL) || (sig == SIGCONT)) {
		if (p->state == TASK_STOPPED)
			wake_up_process(p);
		p->exit_code = 0;
		p->signal &= ~( (1<<(SIGSTOP-1)) | (1<<(SIGTSTP-1)) |
				(1<<(SIGTTIN-1)) | (1<<(SIGTTOU-1)) );
	}
	if (sig == SIGSTOP || sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU)
		p->signal &= ~(1<<(SIGCONT-1));
	/* Actually generate the signal */
	generate(sig,p);
	return 0;
}

void notify_parent(struct task_struct * tsk)
{
	if (tsk->p_pptr == task[smp_num_cpus])		/* Init */
		tsk->exit_signal = SIGCHLD;
	send_sig(tsk->exit_signal, tsk->p_pptr, 1);
	wake_up_interruptible(&tsk->p_pptr->wait_chldexit);
}

void release(struct task_struct * p)
{
	int i;

	if (!p)
		return;
	if (p == current) {
		printk("task releasing itself\n");
		return;
	}
	for (i=1 ; i<NR_TASKS ; i++)
		if (task[i] == p) {
			nr_tasks--;
			task[i] = NULL;
			REMOVE_LINKS(p);
			release_thread(p);
			if (STACK_MAGIC != *(unsigned long *)p->kernel_stack_page)
				printk(KERN_ALERT "release: %s kernel stack corruption. Aiee\n", p->comm);
			free_kernel_stack(p->kernel_stack_page);
			current->cmin_flt += p->min_flt + p->cmin_flt;
			current->cmaj_flt += p->maj_flt + p->cmaj_flt;
			current->cnswap += p->nswap + p->cnswap;
			kfree(p);
			return;
		}
	panic("trying to release non-existent task");
}

#ifdef DEBUG_PROC_TREE
/*
 * Check to see if a task_struct pointer is present in the task[] array
 * Return 0 if found, and 1 if not found.
 */
int bad_task_ptr(struct task_struct *p)
{
	int 	i;

	if (!p)
		return 0;
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] == p)
			return 0;
	return 1;
}
	
/*
 * This routine scans the pid tree and makes sure the rep invariant still
 * holds.  Used for debugging only, since it's very slow....
 *
 * It looks a lot scarier than it really is.... we're doing nothing more
 * than verifying the doubly-linked list found in p_ysptr and p_osptr, 
 * and checking it corresponds with the process tree defined by p_cptr and 
 * p_pptr;
 */
void audit_ptree(void)
{
	int	i;

	for (i=1 ; i<NR_TASKS ; i++) {
		if (!task[i])
			continue;
		if (bad_task_ptr(task[i]->p_pptr))
			printk("Warning, pid %d's parent link is bad\n",
				task[i]->pid);
		if (bad_task_ptr(task[i]->p_cptr))
			printk("Warning, pid %d's child link is bad\n",
				task[i]->pid);
		if (bad_task_ptr(task[i]->p_ysptr))
			printk("Warning, pid %d's ys link is bad\n",
				task[i]->pid);
		if (bad_task_ptr(task[i]->p_osptr))
			printk("Warning, pid %d's os link is bad\n",
				task[i]->pid);
		if (task[i]->p_pptr == task[i])
			printk("Warning, pid %d parent link points to self\n",
				task[i]->pid);
		if (task[i]->p_cptr == task[i])
			printk("Warning, pid %d child link points to self\n",
				task[i]->pid);
		if (task[i]->p_ysptr == task[i])
			printk("Warning, pid %d ys link points to self\n",
				task[i]->pid);
		if (task[i]->p_osptr == task[i])
			printk("Warning, pid %d os link points to self\n",
				task[i]->pid);
		if (task[i]->p_osptr) {
			if (task[i]->p_pptr != task[i]->p_osptr->p_pptr)
				printk(
			"Warning, pid %d older sibling %d parent is %d\n",
				task[i]->pid, task[i]->p_osptr->pid,
				task[i]->p_osptr->p_pptr->pid);
			if (task[i]->p_osptr->p_ysptr != task[i])
				printk(
		"Warning, pid %d older sibling %d has mismatched ys link\n",
				task[i]->pid, task[i]->p_osptr->pid);
		}
		if (task[i]->p_ysptr) {
			if (task[i]->p_pptr != task[i]->p_ysptr->p_pptr)
				printk(
			"Warning, pid %d younger sibling %d parent is %d\n",
				task[i]->pid, task[i]->p_osptr->pid,
				task[i]->p_osptr->p_pptr->pid);
			if (task[i]->p_ysptr->p_osptr != task[i])
				printk(
		"Warning, pid %d younger sibling %d has mismatched os link\n",
				task[i]->pid, task[i]->p_ysptr->pid);
		}
		if (task[i]->p_cptr) {
			if (task[i]->p_cptr->p_pptr != task[i])
				printk(
			"Warning, pid %d youngest child %d has mismatched parent link\n",
				task[i]->pid, task[i]->p_cptr->pid);
			if (task[i]->p_cptr->p_ysptr)
				printk(
			"Warning, pid %d youngest child %d has non-NULL ys link\n",
				task[i]->pid, task[i]->p_cptr->pid);
		}
	}
}
#endif /* DEBUG_PROC_TREE */

/*
 * This checks not only the pgrp, but falls back on the pid if no
 * satisfactory pgrp is found. I dunno - gdb doesn't work correctly
 * without this...
 */
int session_of_pgrp(int pgrp)
{
	struct task_struct *p;
	int fallback;

	fallback = -1;
	for_each_task(p) {
 		if (p->session <= 0)
 			continue;
		if (p->pgrp == pgrp)
			return p->session;
		if (p->pid == pgrp)
			fallback = p->session;
	}
	return fallback;
}

/*
 * kill_pg() sends a signal to a process group: this is what the tty
 * control characters do (^C, ^Z etc)
 */
int kill_pg(int pgrp, int sig, int priv)
{
	struct task_struct *p;
	int err,retval = -ESRCH;
	int found = 0;

	if (sig<0 || sig>32 || pgrp<=0)
		return -EINVAL;
	for_each_task(p) {
		if (p->pgrp == pgrp) {
			if ((err = send_sig(sig,p,priv)) != 0)
				retval = err;
			else
				found++;
		}
	}
	return(found ? 0 : retval);
}

/*
 * kill_sl() sends a signal to the session leader: this is used
 * to send SIGHUP to the controlling process of a terminal when
 * the connection is lost.
 */
int kill_sl(int sess, int sig, int priv)
{
	struct task_struct *p;
	int err,retval = -ESRCH;
	int found = 0;

	if (sig<0 || sig>32 || sess<=0)
		return -EINVAL;
	for_each_task(p) {
		if (p->session == sess && p->leader) {
			if ((err = send_sig(sig,p,priv)) != 0)
				retval = err;
			else
				found++;
		}
	}
	return(found ? 0 : retval);
}

int kill_proc(int pid, int sig, int priv)
{
 	struct task_struct *p;

	if (sig<0 || sig>32)
		return -EINVAL;
	for_each_task(p) {
		if (p && p->pid == pid)
			return send_sig(sig,p,priv);
	}
	return(-ESRCH);
}

/*
 * POSIX specifies that kill(-1,sig) is unspecified, but what we have
 * is probably wrong.  Should make it like BSD or SYSV.
 */
asmlinkage int sys_kill(int pid,int sig)
{
	int err, retval = 0, count = 0;

	if (!pid)
		return(kill_pg(current->pgrp,sig,0));
	if (pid == -1) {
		struct task_struct * p;
		for_each_task(p) {
			if (p->pid > 1 && p != current) {
				++count;
				if ((err = send_sig(sig,p,0)) != -EPERM)
					retval = err;
			}
		}
		return(count ? retval : -ESRCH);
	}
	if (pid < 0) 
		return(kill_pg(-pid,sig,0));
	/* Normal kill */
	return(kill_proc(pid,sig,0));
}

/*
 * Determine if a process group is "orphaned", according to the POSIX
 * definition in 2.2.2.52.  Orphaned process groups are not to be affected
 * by terminal-generated stop signals.  Newly orphaned process groups are 
 * to receive a SIGHUP and a SIGCONT.
 * 
 * "I ask you, have you ever known what it is to be an orphan?"
 */
static int will_become_orphaned_pgrp(int pgrp, struct task_struct * ignored_task)
{
	struct task_struct *p;

	for_each_task(p) {
		if ((p == ignored_task) || (p->pgrp != pgrp) || 
		    (p->state == TASK_ZOMBIE) ||
		    (p->p_pptr->pid == 1))
			continue;
		if ((p->p_pptr->pgrp != pgrp) &&
		    (p->p_pptr->session == p->session))
			return 0;
	}
	return(1);	/* (sighing) "Often!" */
}

int is_orphaned_pgrp(int pgrp)
{
	return will_become_orphaned_pgrp(pgrp, 0);
}

static inline int has_stopped_jobs(int pgrp)
{
	struct task_struct * p;

	for_each_task(p) {
		if (p->pgrp != pgrp)
			continue;
		if (p->state == TASK_STOPPED)
			return(1);
	}
	return(0);
}

static inline void forget_original_parent(struct task_struct * father)
{
	struct task_struct * p;

	for_each_task(p) {
		if (p->p_opptr == father)
			if (task[smp_num_cpus])	/* init */
				p->p_opptr = task[smp_num_cpus];
			else
				p->p_opptr = task[0];
	}
}

static inline void __exit_files(struct task_struct *tsk)
{
	struct files_struct * files = tsk->files;

	if (files) {
		tsk->files = NULL;
		if (!--files->count) {
			int i;
			for (i=0 ; i<NR_OPEN ; i++) {
				struct file * filp = files->fd[i];
				if (!filp)
					continue;
				files->fd[i] = NULL;
				close_fp(filp);
			}
			kfree(files);
		}
	}
}

void exit_files(struct task_struct *tsk)
{
  __exit_files(tsk);
}

static inline void __exit_fs(struct task_struct *tsk)
{
	struct fs_struct * fs = tsk->fs;

	if (fs) {
		tsk->fs = NULL;
		if (!--fs->count) {
			iput(fs->root);
			iput(fs->pwd);
			kfree(fs);
		}
	}
}

void exit_fs(struct task_struct *tsk)
{
  __exit_fs(tsk);
}

static inline void __exit_sighand(struct task_struct *tsk)
{
	struct signal_struct * sig = tsk->sig;

	if (sig) {
		tsk->sig = NULL;
		if (!--sig->count) {
			kfree(sig);
		}
	}
}

void exit_sighand(struct task_struct *tsk)
{
	__exit_sighand(tsk);
}

static inline void __exit_mm(struct task_struct * tsk)
{
	struct mm_struct * mm = tsk->mm;

	/* Set us up to use the kernel mm state */
	if (mm != &init_mm) {
		flush_cache_mm(mm);
		flush_tlb_mm(mm);
		tsk->mm = &init_mm;
		tsk->swappable = 0;
		SET_PAGE_DIR(tsk, swapper_pg_dir);

		/* free the old state - not used any more */
		if (!--mm->count) {
			exit_mmap(mm);
			free_page_tables(mm);
			kfree(mm);
		}
	}
}

void exit_mm(struct task_struct *tsk)
{
	__exit_mm(tsk);
}

/* 
 * Send signals to all our closest relatives so that they know
 * to properly mourn us..
 */
static void exit_notify(void)
{
	struct task_struct * p;

	forget_original_parent(current);
	/* 
	 * Check to see if any process groups have become orphaned
	 * as a result of our exiting, and if they have any stopped
	 * jobs, send them a SIGHUP and then a SIGCONT.  (POSIX 3.2.2.2)
	 *
	 * Case i: Our father is in a different pgrp than we are
	 * and we were the only connection outside, so our pgrp
	 * is about to become orphaned.
	 */
	if ((current->p_pptr->pgrp != current->pgrp) &&
	    (current->p_pptr->session == current->session) &&
	    will_become_orphaned_pgrp(current->pgrp, current) &&
	    has_stopped_jobs(current->pgrp)) {
		kill_pg(current->pgrp,SIGHUP,1);
		kill_pg(current->pgrp,SIGCONT,1);
	}
	/* Let father know we died */
	notify_parent(current);
	
	/*
	 * This loop does two things:
	 * 
  	 * A.  Make init inherit all the child processes
	 * B.  Check to see if any process groups have become orphaned
	 *	as a result of our exiting, and if they have any stopped
	 *	jobs, send them a SIGHUP and then a SIGCONT.  (POSIX 3.2.2.2)
	 */
	while ((p = current->p_cptr) != NULL) {
		current->p_cptr = p->p_osptr;
		p->p_ysptr = NULL;
		p->flags &= ~(PF_PTRACED|PF_TRACESYS);
		if (task[smp_num_cpus] && task[smp_num_cpus] != current) /* init */
			p->p_pptr = task[smp_num_cpus];
		else
			p->p_pptr = task[0];
		p->p_osptr = p->p_pptr->p_cptr;
		p->p_osptr->p_ysptr = p;
		p->p_pptr->p_cptr = p;
		if (p->state == TASK_ZOMBIE)
			notify_parent(p);
		/*
		 * process group orphan check
		 * Case ii: Our child is in a different pgrp 
		 * than we are, and it was the only connection
		 * outside, so the child pgrp is now orphaned.
		 */
		if ((p->pgrp != current->pgrp) &&
		    (p->session == current->session) &&
		    is_orphaned_pgrp(p->pgrp) &&
		    has_stopped_jobs(p->pgrp)) {
			kill_pg(p->pgrp,SIGHUP,1);
			kill_pg(p->pgrp,SIGCONT,1);
		}
	}
	if (current->leader)
		disassociate_ctty(1);
}

NORET_TYPE void do_exit(long code)
{
	if (intr_count) {
		printk("Aiee, killing interrupt handler\n");
		intr_count = 0;
	}
fake_volatile:
	acct_process(code);
	current->flags |= PF_EXITING;
	del_timer(&current->real_timer);
	sem_exit();
	kerneld_exit();
	__exit_mm(current);
	__exit_files(current);
	__exit_fs(current);
	__exit_sighand(current);
	exit_thread();
	current->state = TASK_ZOMBIE;
	current->exit_code = code;
	exit_notify();
#ifdef DEBUG_PROC_TREE
	audit_ptree();
#endif
	if (current->exec_domain && current->exec_domain->use_count)
		(*current->exec_domain->use_count)--;
	if (current->binfmt && current->binfmt->use_count)
		(*current->binfmt->use_count)--;
	schedule();
/*
 * In order to get rid of the "volatile function does return" message
 * I did this little loop that confuses gcc to think do_exit really
 * is volatile. In fact it's schedule() that is volatile in some
 * circumstances: when current->state = ZOMBIE, schedule() never
 * returns.
 *
 * In fact the natural way to do all this is to have the label and the
 * goto right after each other, but I put the fake_volatile label at
 * the start of the function just in case something /really/ bad
 * happens, and the schedule returns. This way we can try again. I'm
 * not paranoid: it's just that everybody is out to get me.
 */
	goto fake_volatile;
}

asmlinkage int sys_exit(int error_code)
{
	do_exit((error_code&0xff)<<8);
}

asmlinkage int sys_wait4(pid_t pid,unsigned int * stat_addr, int options, struct rusage * ru)
{
	int flag, retval;
	struct wait_queue wait = { current, NULL };
	struct task_struct *p;

	if (stat_addr) {
		flag = verify_area(VERIFY_WRITE, stat_addr, sizeof(*stat_addr));
		if (flag)
			return flag;
	}
	if (ru) {
		flag = verify_area(VERIFY_WRITE, ru, sizeof(*ru));
		if (flag)
			return flag;
	}
	if (options & ~(WNOHANG|WUNTRACED|__WCLONE))
	    return -EINVAL;

	add_wait_queue(&current->wait_chldexit,&wait);
repeat:
	flag=0;
 	for (p = current->p_cptr ; p ; p = p->p_osptr) {
		if (pid>0) {
			if (p->pid != pid)
				continue;
		} else if (!pid) {
			if (p->pgrp != current->pgrp)
				continue;
		} else if (pid != -1) {
			if (p->pgrp != -pid)
				continue;
		}
		/* wait for cloned processes iff the __WCLONE flag is set */
		if ((p->exit_signal != SIGCHLD) ^ ((options & __WCLONE) != 0))
			continue;
		flag = 1;
		switch (p->state) {
			case TASK_STOPPED:
				if (!p->exit_code)
					continue;
				if (!(options & WUNTRACED) && !(p->flags & PF_PTRACED))
					continue;
				if (ru != NULL)
					getrusage(p, RUSAGE_BOTH, ru);
				if (stat_addr)
					put_user((p->exit_code << 8) | 0x7f,
						stat_addr);
				p->exit_code = 0;
				retval = p->pid;
				goto end_wait4;
			case TASK_ZOMBIE:
				current->cutime += p->utime + p->cutime;
				current->cstime += p->stime + p->cstime;
				if (ru != NULL)
					getrusage(p, RUSAGE_BOTH, ru);
				if (stat_addr)
					put_user(p->exit_code, stat_addr);
				retval = p->pid;
				if (p->p_opptr != p->p_pptr) {
					REMOVE_LINKS(p);
					p->p_pptr = p->p_opptr;
					SET_LINKS(p);
					notify_parent(p);
				} else
					release(p);
#ifdef DEBUG_PROC_TREE
				audit_ptree();
#endif
				goto end_wait4;
			default:
				continue;
		}
	}
	if (flag) {
		retval = 0;
		if (options & WNOHANG)
			goto end_wait4;
		retval = -ERESTARTSYS;
		if (current->signal & ~current->blocked)
			goto end_wait4;
		current->state=TASK_INTERRUPTIBLE;
		schedule();
		goto repeat;
	}
	retval = -ECHILD;
end_wait4:
	remove_wait_queue(&current->wait_chldexit,&wait);
	return retval;
}

#ifndef __alpha__

/*
 * sys_waitpid() remains for compatibility. waitpid() should be
 * implemented by calling sys_wait4() from libc.a.
 */
asmlinkage int sys_waitpid(pid_t pid,unsigned int * stat_addr, int options)
{
	return sys_wait4(pid, stat_addr, options, NULL);
}

#endif

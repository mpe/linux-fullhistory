/*
 *  linux/kernel/exit.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/wait.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/resource.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/acct.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>

extern void sem_exit (void);

int getrusage(struct task_struct *, int, struct rusage *);

static void release(struct task_struct * p)
{
	if (p != current) {
#ifdef __SMP__
		/* FIXME! Cheesy, but kills the window... -DaveM */
		do {
			barrier();
		} while (p->has_cpu);
		spin_unlock_wait(&scheduler_lock);
#endif
		charge_uid(p, -1);
		nr_tasks--;
		add_free_taskslot(p->tarray_ptr);
		{
			unsigned long flags;

			write_lock_irqsave(&tasklist_lock, flags);
			unhash_pid(p);
			REMOVE_LINKS(p);
			write_unlock_irqrestore(&tasklist_lock, flags);
		}
		release_thread(p);
		current->cmin_flt += p->min_flt + p->cmin_flt;
		current->cmaj_flt += p->maj_flt + p->cmaj_flt;
		current->cnswap += p->nswap + p->cnswap;
		free_task_struct(p);
	} else {
		printk("task releasing itself\n");
	}
}

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
	read_lock(&tasklist_lock);
	for_each_task(p) {
 		if (p->session <= 0)
 			continue;
		if (p->pgrp == pgrp) {
			fallback = p->session;
			break;
		}
		if (p->pid == pgrp)
			fallback = p->session;
	}
	read_unlock(&tasklist_lock);
	return fallback;
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

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if ((p == ignored_task) || (p->pgrp != pgrp) ||
		    (p->state == TASK_ZOMBIE) ||
		    (p->p_pptr->pid == 1))
			continue;
		if ((p->p_pptr->pgrp != pgrp) &&
		    (p->p_pptr->session == p->session)) {
			read_unlock(&tasklist_lock);
 			return 0;
		}
	}
	read_unlock(&tasklist_lock);
	return 1;	/* (sighing) "Often!" */
}

int is_orphaned_pgrp(int pgrp)
{
	return will_become_orphaned_pgrp(pgrp, 0);
}

static inline int has_stopped_jobs(int pgrp)
{
	int retval = 0;
	struct task_struct * p;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if (p->pgrp != pgrp)
			continue;
		if (p->state != TASK_STOPPED)
			continue;
		retval = 1;
		break;
	}
	read_unlock(&tasklist_lock);
	return retval;
}

static inline void forget_original_parent(struct task_struct * father)
{
	struct task_struct * p;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if (p->p_opptr == father) {
			p->exit_signal = SIGCHLD;
			p->p_opptr = task[smp_num_cpus] ? : task[0]; /* init */
			if (p->pdeath_signal) send_sig(p->pdeath_signal, p, 0);
		}
	}
	read_unlock(&tasklist_lock);
}

static inline void close_files(struct files_struct * files)
{
	int i, j;

	j = 0;
	for (;;) {
		unsigned long set = files->open_fds.fds_bits[j];
		i = j * __NFDBITS;
		j++;
		if (i >= files->max_fds)
			break;
		while (set) {
			if (set & 1) {
				struct file * file = files->fd[i];
				if (file) {
					files->fd[i] = NULL;
					close_fp(file, files);
				}
			}
			i++;
			set >>= 1;
		}
	}
}

extern kmem_cache_t *files_cachep;  

static inline void __exit_files(struct task_struct *tsk)
{
	struct files_struct * files = tsk->files;

	if (files) {
		tsk->files = NULL;
		if (!--files->count) {
			close_files(files);
			/*
			 * Free the fd array as appropriate ...
			 */
			if (NR_OPEN * sizeof(struct file *) == PAGE_SIZE)
				free_page((unsigned long) files->fd);
			else
				kfree(files->fd);
			kmem_cache_free(files_cachep, files);
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
			dput(fs->root);
			dput(fs->pwd);
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
		if (atomic_dec_and_test(&sig->count))
			kfree(sig);
	}

	flush_signals(tsk);
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
		destroy_context(mm);
		tsk->mm = &init_mm;
		tsk->swappable = 0;
		SET_PAGE_DIR(tsk, swapper_pg_dir);
		mmput(mm);
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
	notify_parent(current, current->exit_signal);

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

		p->p_pptr = p->p_opptr;
		p->p_osptr = p->p_pptr->p_cptr;
		if (p->p_osptr)
			p->p_osptr->p_ysptr = p;
		p->p_pptr->p_cptr = p;
		if (p->state == TASK_ZOMBIE)
			notify_parent(p, p->exit_signal);
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
	if (in_interrupt())
		printk("Aiee, killing interrupt handler\n");
	if (current == task[0])
		panic("Attempted to kill the idle task!");
fake_volatile:
	current->flags |= PF_EXITING;
	acct_process(code);
	del_timer(&current->real_timer);
	sem_exit();
	__exit_mm(current);
#if CONFIG_AP1000
	exit_msc(current);
#endif
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
	if (current->exec_domain && current->exec_domain->module)
		__MOD_DEC_USE_COUNT(current->exec_domain->module);
	if (current->binfmt && current->binfmt->module)
		__MOD_DEC_USE_COUNT(current->binfmt->module);
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
	lock_kernel();
	do_exit((error_code&0xff)<<8);
	unlock_kernel();
}

asmlinkage int sys_wait4(pid_t pid,unsigned int * stat_addr, int options, struct rusage * ru)
{
	int flag, retval;
	struct wait_queue wait = { current, NULL };
	struct task_struct *p;

	if (stat_addr) {
		if(verify_area(VERIFY_WRITE, stat_addr, sizeof(*stat_addr)))
			return -EFAULT;
	}
	if (ru) {
		if(verify_area(VERIFY_WRITE, ru, sizeof(*ru)))
			return -EFAULT;
	}

	if (options & ~(WNOHANG|WUNTRACED|__WCLONE))
		return -EINVAL;

	add_wait_queue(&current->wait_chldexit,&wait);
repeat:
	flag = 0;
	read_lock(&tasklist_lock);
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
				read_unlock(&tasklist_lock);
				if (ru != NULL)
					getrusage(p, RUSAGE_BOTH, ru);
				if (stat_addr)
					__put_user((p->exit_code << 8) | 0x7f, stat_addr);
				p->exit_code = 0;
				retval = p->pid;
				goto end_wait4;
			case TASK_ZOMBIE:
				current->times.tms_cutime += p->times.tms_utime + p->times.tms_cutime;
				current->times.tms_cstime += p->times.tms_stime + p->times.tms_cstime;
				read_unlock(&tasklist_lock);
				if (ru != NULL)
					getrusage(p, RUSAGE_BOTH, ru);
				if (stat_addr)
					__put_user(p->exit_code, stat_addr);
				retval = p->pid;
				if (p->p_opptr != p->p_pptr) {
					/* Note this grabs tasklist_lock
					 * as a writer... (twice!)
					 */
					REMOVE_LINKS(p);
					p->p_pptr = p->p_opptr;
					SET_LINKS(p);
					notify_parent(p, SIGCHLD);
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
	read_unlock(&tasklist_lock);
	if (flag) {
		retval = 0;
		if (options & WNOHANG)
			goto end_wait4;
		retval = -ERESTARTSYS;
		if (signal_pending(current))
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

/*
 *  linux/kernel/fork.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s).
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/module.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/uaccess.h>

int nr_tasks=1;
int nr_running=1;
unsigned long int total_forks=0;	/* Handle normal Linux uptimes. */
int last_pid=0;

static inline int find_empty_process(void)
{
	int i;

	if (nr_tasks >= NR_TASKS - MIN_TASKS_LEFT_FOR_ROOT) {
		if (current->uid)
			return -EAGAIN;
	}
	if (current->uid) {
		long max_tasks = current->rlim[RLIMIT_NPROC].rlim_cur;

		max_tasks--;	/* count the new process.. */
		if (max_tasks < nr_tasks) {
			struct task_struct *p;
			read_lock(&tasklist_lock);
			for_each_task (p) {
				if (p->uid == current->uid)
					if (--max_tasks < 0) {
						read_unlock(&tasklist_lock);
						return -EAGAIN;
					}
			}
			read_unlock(&tasklist_lock);
		}
	}
	for (i = 0 ; i < NR_TASKS ; i++) {
		if (!task[i])
			return i;
	}
	return -EAGAIN;
}

static int get_pid(unsigned long flags)
{
	struct task_struct *p;

	if (flags & CLONE_PID)
		return current->pid;

	read_lock(&tasklist_lock);
repeat:
	if ((++last_pid) & 0xffff8000)
		last_pid=1;
	for_each_task (p) {
		if (p->pid == last_pid ||
		    p->pgrp == last_pid ||
		    p->session == last_pid)
			goto repeat;
	}
	read_unlock(&tasklist_lock);

	return last_pid;
}

static inline int dup_mmap(struct mm_struct * mm)
{
	struct vm_area_struct * mpnt, **p, *tmp;

	mm->mmap = NULL;
	p = &mm->mmap;
	flush_cache_mm(current->mm);
	for (mpnt = current->mm->mmap ; mpnt ; mpnt = mpnt->vm_next) {
		tmp = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
		if (!tmp) {
			exit_mmap(mm);
			flush_tlb_mm(current->mm);
			return -ENOMEM;
		}
		*tmp = *mpnt;
		tmp->vm_flags &= ~VM_LOCKED;
		tmp->vm_mm = mm;
		tmp->vm_next = NULL;
		if (tmp->vm_inode) {
			tmp->vm_inode->i_count++;
			/* insert tmp into the share list, just after mpnt */
			tmp->vm_next_share->vm_prev_share = tmp;
			mpnt->vm_next_share = tmp;
			tmp->vm_prev_share = mpnt;
		}
		if (copy_page_range(mm, current->mm, tmp)) {
			exit_mmap(mm);
			flush_tlb_mm(current->mm);
			return -ENOMEM;
		}
		if (tmp->vm_ops && tmp->vm_ops->open)
			tmp->vm_ops->open(tmp);
		*p = tmp;
		p = &tmp->vm_next;
	}
	flush_tlb_mm(current->mm);
	build_mmap_avl(mm);
	return 0;
}

static inline int copy_mm(unsigned long clone_flags, struct task_struct * tsk)
{
	if (!(clone_flags & CLONE_VM)) {
		struct mm_struct * mm = kmalloc(sizeof(*tsk->mm), GFP_KERNEL);
		if (!mm)
			return -1;
		*mm = *current->mm;
		init_new_context(mm);
		mm->count = 1;
		mm->def_flags = 0;
		tsk->mm = mm;
		tsk->min_flt = tsk->maj_flt = 0;
		tsk->cmin_flt = tsk->cmaj_flt = 0;
		tsk->nswap = tsk->cnswap = 0;
		if (new_page_tables(tsk))
			goto free_mm;
		if (dup_mmap(mm)) {
			free_page_tables(mm);
free_mm:
			kfree(mm);
			return -1;
		}
		return 0;
	}
	current->mm->count++;
	SET_PAGE_DIR(tsk, current->mm->pgd);
	return 0;
}

static inline int copy_fs(unsigned long clone_flags, struct task_struct * tsk)
{
	if (clone_flags & CLONE_FS) {
		current->fs->count++;
		return 0;
	}
	tsk->fs = kmalloc(sizeof(*tsk->fs), GFP_KERNEL);
	if (!tsk->fs)
		return -1;
	tsk->fs->count = 1;
	tsk->fs->umask = current->fs->umask;
	if ((tsk->fs->root = current->fs->root))
		tsk->fs->root->i_count++;
	if ((tsk->fs->pwd = current->fs->pwd))
		tsk->fs->pwd->i_count++;
	return 0;
}

static inline int copy_files(unsigned long clone_flags, struct task_struct * tsk)
{
	int i;
	struct files_struct *oldf, *newf;
	struct file **old_fds, **new_fds;

	oldf = current->files;
	if (clone_flags & CLONE_FILES) {
		oldf->count++;
		return 0;
	}

	newf = kmalloc(sizeof(*newf), GFP_KERNEL);
	tsk->files = newf;
	if (!newf)
		return -1;

	newf->count = 1;
	newf->close_on_exec = oldf->close_on_exec;
	newf->open_fds = oldf->open_fds;

	old_fds = oldf->fd;
	new_fds = newf->fd;
	for (i = NR_OPEN; i != 0; i--) {
		struct file * f = *old_fds;
		old_fds++;
		*new_fds = f;
		new_fds++;
		if (f)
			f->f_count++;
	}
	return 0;
}

static inline int copy_sighand(unsigned long clone_flags, struct task_struct * tsk)
{
	if (clone_flags & CLONE_SIGHAND) {
		atomic_inc(&current->sig->count);
		return 0;
	}
	tsk->sig = kmalloc(sizeof(*tsk->sig), GFP_KERNEL);
	if (!tsk->sig)
		return -1;
	spin_lock_init(&tsk->sig->siglock);
	atomic_set(&tsk->sig->count, 1);
	memcpy(tsk->sig->action, current->sig->action, sizeof(tsk->sig->action));
	return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in its entirety.
 */
int do_fork(unsigned long clone_flags, unsigned long usp, struct pt_regs *regs)
{
	int nr;
	int error = -ENOMEM;
	unsigned long new_stack;
	struct task_struct *p;

	lock_kernel();
	p = alloc_task_struct();
	if (!p)
		goto bad_fork;
	new_stack = alloc_kernel_stack(p);
	if (!new_stack)
		goto bad_fork_free_p;
	error = -EAGAIN;
	nr = find_empty_process();
	if (nr < 0)
		goto bad_fork_free_stack;

	*p = *current;

	if (p->exec_domain && p->exec_domain->module)
		__MOD_INC_USE_COUNT(p->exec_domain->module);
	if (p->binfmt && p->binfmt->module)
		__MOD_INC_USE_COUNT(p->binfmt->module);

	p->did_exec = 0;
	p->swappable = 0;
	p->kernel_stack_page = new_stack;
	*(unsigned long *) p->kernel_stack_page = STACK_MAGIC;
	p->state = TASK_UNINTERRUPTIBLE;
	p->flags &= ~(PF_PTRACED|PF_TRACESYS|PF_SUPERPRIV);
	p->flags |= PF_FORKNOEXEC;
	p->pid = get_pid(clone_flags);
	p->next_run = NULL;
	p->prev_run = NULL;
	p->p_pptr = p->p_opptr = current;
	p->p_cptr = NULL;
	init_waitqueue(&p->wait_chldexit);
	p->signal = 0;
	p->it_real_value = p->it_virt_value = p->it_prof_value = 0;
	p->it_real_incr = p->it_virt_incr = p->it_prof_incr = 0;
	init_timer(&p->real_timer);
	p->real_timer.data = (unsigned long) p;
	p->leader = 0;		/* session leadership doesn't inherit */
	p->tty_old_pgrp = 0;
	p->utime = p->stime = 0;
	p->cutime = p->cstime = 0;
#ifdef __SMP__
	p->processor = NO_PROC_ID;
#endif
	p->lock_depth = 0;
	p->start_time = jiffies;
	task[nr] = p;
	SET_LINKS(p);
	nr_tasks++;

	error = -ENOMEM;
	/* copy all the process information */
	if (copy_files(clone_flags, p))
		goto bad_fork_cleanup;
	if (copy_fs(clone_flags, p))
		goto bad_fork_cleanup_files;
	if (copy_sighand(clone_flags, p))
		goto bad_fork_cleanup_fs;
	if (copy_mm(clone_flags, p))
		goto bad_fork_cleanup_sighand;
	error = copy_thread(nr, clone_flags, usp, p, regs);
	if (error)
		goto bad_fork_cleanup_sighand;
	p->semundo = NULL;

	/* ok, now we should be set up.. */
	p->swappable = 1;
	p->exit_signal = clone_flags & CSIGNAL;

	/*
	 * "share" dynamic priority between parent and child, thus the
	 * total amount of dynamic priorities in the system doesnt change,
	 * more scheduling fairness. This is only important in the first
	 * timeslice, on the long run the scheduling behaviour is unchanged.
	 */
	current->counter >>= 1;
	p->counter = current->counter;

	if(p->pid) {
		wake_up_process(p);		/* do this last, just in case */
	} else {
		p->state = TASK_RUNNING;
		p->next_run = p->prev_run = p;
	}
	++total_forks;
	error = p->pid;
	goto fork_out;

bad_fork_cleanup_sighand:
	exit_sighand(p);
bad_fork_cleanup_fs:
	exit_fs(p);
bad_fork_cleanup_files:
	exit_files(p);
bad_fork_cleanup:
	if (p->exec_domain && p->exec_domain->module)
		__MOD_DEC_USE_COUNT(p->exec_domain->module);
	if (p->binfmt && p->binfmt->module)
		__MOD_DEC_USE_COUNT(p->binfmt->module);
	task[nr] = NULL;
	REMOVE_LINKS(p);
	nr_tasks--;
bad_fork_free_stack:
	free_kernel_stack(new_stack);
bad_fork_free_p:
	free_task_struct(p);
bad_fork:
fork_out:
	unlock_kernel();
	return error;
}

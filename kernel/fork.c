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
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/ldt.h>

#include <asm/segment.h>
#include <asm/system.h>

int nr_tasks=1;
int nr_running=1;
long last_pid=0;

static int find_empty_process(void)
{
	int free_task;
	int i, tasks_free;
	int this_user_tasks;

repeat:
	if ((++last_pid) & 0xffff8000)
		last_pid=1;
	this_user_tasks = 0;
	tasks_free = 0;
	free_task = -EAGAIN;
	i = NR_TASKS;
	while (--i > 0) {
		if (!task[i]) {
			free_task = i;
			tasks_free++;
			continue;
		}
		if (task[i]->uid == current->uid)
			this_user_tasks++;
		if (task[i]->pid == last_pid || task[i]->pgrp == last_pid ||
		    task[i]->session == last_pid)
			goto repeat;
	}
	if (tasks_free <= MIN_TASKS_LEFT_FOR_ROOT ||
	    this_user_tasks > current->rlim[RLIMIT_NPROC].rlim_cur)
		if (current->uid)
			return -EAGAIN;
	return free_task;
}

static struct file * copy_fd(struct file * old_file)
{
	struct file * new_file = get_empty_filp();
	int error;

	if (new_file) {
		memcpy(new_file,old_file,sizeof(struct file));
		new_file->f_count = 1;
		if (new_file->f_inode)
			new_file->f_inode->i_count++;
		if (new_file->f_op && new_file->f_op->open) {
			error = new_file->f_op->open(new_file->f_inode,new_file);
			if (error) {
				iput(new_file->f_inode);
				new_file->f_count = 0;
				new_file = NULL;
			}
		}
	}
	return new_file;
}

static int dup_mmap(struct task_struct * tsk)
{
	struct vm_area_struct * mpnt, **p, *tmp;

	tsk->mm->mmap = NULL;
	p = &tsk->mm->mmap;
	for (mpnt = current->mm->mmap ; mpnt ; mpnt = mpnt->vm_next) {
		tmp = (struct vm_area_struct *) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
		if (!tmp) {
			exit_mmap(tsk);
			return -ENOMEM;
		}
		*tmp = *mpnt;
		tmp->vm_task = tsk;
		tmp->vm_next = NULL;
		if (tmp->vm_inode) {
			tmp->vm_inode->i_count++;
			/* insert tmp into the share list, just after mpnt */
			tmp->vm_next_share->vm_prev_share = tmp;
			mpnt->vm_next_share = tmp;
			tmp->vm_prev_share = mpnt;
		}
		if (tmp->vm_ops && tmp->vm_ops->open)
			tmp->vm_ops->open(tmp);
		*p = tmp;
		p = &tmp->vm_next;
	}
	build_mmap_avl(tsk);
	return 0;
}

/*
 * SHAREFD not yet implemented..
 */
static void copy_files(unsigned long clone_flags, struct task_struct * p)
{
	int i;
	struct file * f;

	if (clone_flags & COPYFD) {
		for (i=0; i<NR_OPEN;i++)
			if ((f = p->files->fd[i]) != NULL)
				p->files->fd[i] = copy_fd(f);
	} else {
		for (i=0; i<NR_OPEN;i++)
			if ((f = p->files->fd[i]) != NULL)
				f->f_count++;
	}
}

/*
 * CLONEVM not yet correctly implemented: needs to clone the mmap
 * instead of duplicating it..
 */
static int copy_mm(unsigned long clone_flags, struct task_struct * p)
{
	if (clone_flags & COPYVM) {
		p->mm->min_flt = p->mm->maj_flt = 0;
		p->mm->cmin_flt = p->mm->cmaj_flt = 0;
		if (copy_page_tables(p))
			return 1;
		return dup_mmap(p);
	} else {
		if (clone_page_tables(p))
			return 1;
		return dup_mmap(p);		/* wrong.. */
	}
}

static void copy_fs(unsigned long clone_flags, struct task_struct * p)
{
	if (current->fs->pwd)
		current->fs->pwd->i_count++;
	if (current->fs->root)
		current->fs->root->i_count++;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in its entirety.
 */
int do_fork(unsigned long clone_flags, unsigned long usp, struct pt_regs *regs)
{
	int nr;
	unsigned long new_stack;
	struct task_struct *p;

	if(!(p = (struct task_struct*)__get_free_page(GFP_KERNEL)))
		goto bad_fork;
	new_stack = get_free_page(GFP_KERNEL);
	if (!new_stack)
		goto bad_fork_free;
	nr = find_empty_process();
	if (nr < 0)
		goto bad_fork_free;

	*p = *current;

	if (p->exec_domain && p->exec_domain->use_count)
		(*p->exec_domain->use_count)++;
	if (p->binfmt && p->binfmt->use_count)
		(*p->binfmt->use_count)++;

	p->did_exec = 0;
	p->kernel_stack_page = new_stack;
	*(unsigned long *) p->kernel_stack_page = STACK_MAGIC;
	p->state = TASK_UNINTERRUPTIBLE;
	p->flags &= ~(PF_PTRACED|PF_TRACESYS);
	p->pid = last_pid;
	p->p_pptr = p->p_opptr = current;
	p->p_cptr = NULL;
	p->signal = 0;
	p->it_real_value = p->it_virt_value = p->it_prof_value = 0;
	p->it_real_incr = p->it_virt_incr = p->it_prof_incr = 0;
	p->leader = 0;		/* process leadership doesn't inherit */
	p->tty_old_pgrp = 0;
	p->utime = p->stime = 0;
	p->cutime = p->cstime = 0;
	p->start_time = jiffies;
	p->mm->swappable = 0;	/* don't try to swap it out before it's set up */
	task[nr] = p;
	SET_LINKS(p);
	nr_tasks++;

	/* copy all the process information */
	copy_thread(nr, clone_flags, usp, p, regs);
	if (copy_mm(clone_flags, p))
		goto bad_fork_cleanup;
	p->semundo = NULL;
	copy_files(clone_flags, p);
	copy_fs(clone_flags, p);

	/* ok, now we should be set up.. */
	p->mm->swappable = 1;
	p->exit_signal = clone_flags & CSIGNAL;
	p->counter = current->counter >> 1;
	p->state = TASK_RUNNING;	/* do this last, just in case */
	return p->pid;
bad_fork_cleanup:
	task[nr] = NULL;
	REMOVE_LINKS(p);
	nr_tasks--;
bad_fork_free:
	free_page(new_stack);
	free_page((long) p);
bad_fork:
	return -EAGAIN;
}

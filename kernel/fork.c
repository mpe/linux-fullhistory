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

/*
 * This is how a process data structure is allocated. In most
 * cases, the "tsk" pointers point to the same allocation unit
 * substructures, but if the new process shares part (or all)
 * of the sub-units with the parent process, the tsk pointers
 * may point to the parent instead.
 *
 * Regardless, we always allocate the full allocation unit, as
 * the normal fork() semantics require all of them and doing
 * suballocations would be wasteful.
 */
struct allocation_struct {
	struct task_struct tsk;
	struct sigaction sigaction[32];
	struct fs_struct fs;
	struct files_struct files;
	struct mm_struct mm;
};

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

static int dup_mmap(struct mm_struct * mm)
{
	struct vm_area_struct * mpnt, **p, *tmp;

	mm->mmap = NULL;
	p = &mm->mmap;
	for (mpnt = current->mm->mmap ; mpnt ; mpnt = mpnt->vm_next) {
		tmp = (struct vm_area_struct *) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
		if (!tmp) {
			exit_mmap(mm);
			return -ENOMEM;
		}
		*tmp = *mpnt;
		tmp->vm_mm = mm;
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
	build_mmap_avl(mm);
	return 0;
}

static int copy_mm(unsigned long clone_flags, struct allocation_struct * u)
{
	if (clone_flags & CLONE_VM) {
		if (clone_page_tables(&u->tsk))
			return -1;
		current->mm->count++;
		mem_map[MAP_NR(current->mm)]++;
		return 0;
	}
	u->tsk.mm = &u->mm;
	u->mm = *current->mm;
	u->mm.count = 1;
	u->mm.min_flt = u->mm.maj_flt = 0;
	u->mm.cmin_flt = u->mm.cmaj_flt = 0;
	if (copy_page_tables(&u->tsk))
		return -1;
	if (dup_mmap(&u->mm))
		return -1;
	mem_map[MAP_NR(u)]++;
	return 0;
}

static void copy_fs(unsigned long clone_flags, struct allocation_struct * u)
{
	if (clone_flags & CLONE_FS) {
		current->fs->count++;
		mem_map[MAP_NR(current->fs)]++;
		return;
	}
	u->tsk.fs = &u->fs;
	u->fs = *current->fs;
	u->fs.count = 1;
	if (u->fs.pwd)
		u->fs.pwd->i_count++;
	if (u->fs.root)
		u->fs.root->i_count++;
	mem_map[MAP_NR(u)]++;
}

static void copy_files(unsigned long clone_flags, struct allocation_struct * u)
{
	int i;

	if (clone_flags & CLONE_FILES) {
		current->files->count++;
		mem_map[MAP_NR(current->files)]++;
		return;
	}
	u->tsk.files = &u->files;
	u->files = *current->files;
	u->files.count = 1;
	for (i = 0; i < NR_OPEN; i++) {
		struct file * f = u->files.fd[i];
		if (f)
			f->f_count++;
	}
	mem_map[MAP_NR(u)]++;
}

static void copy_sighand(unsigned long clone_flags, struct allocation_struct * u)
{
	if (clone_flags & CLONE_SIGHAND) {
		mem_map[MAP_NR(current->sigaction)]++;
		return;
	}
	u->tsk.sigaction = u->sigaction;
	memcpy(u->sigaction, current->sigaction, sizeof(u->sigaction));
	mem_map[MAP_NR(u)]++;
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
	struct allocation_struct *alloc;

	alloc = (struct allocation_struct *) __get_free_page(GFP_KERNEL);
	if (!alloc)
		goto bad_fork;
	p = &alloc->tsk;
	new_stack = get_free_page(GFP_KERNEL);
	if (!new_stack)
		goto bad_fork_free;
	error = -EAGAIN;
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
	p->next_run = NULL;
	p->prev_run = NULL;
	p->p_pptr = p->p_opptr = current;
	p->p_cptr = NULL;
	p->signal = 0;
	p->it_real_value = p->it_virt_value = p->it_prof_value = 0;
	p->it_real_incr = p->it_virt_incr = p->it_prof_incr = 0;
	init_timer(&p->real_timer);
	p->real_timer.data = (unsigned long) p;
	p->leader = 0;		/* process leadership doesn't inherit */
	p->tty_old_pgrp = 0;
	p->utime = p->stime = 0;
	p->cutime = p->cstime = 0;
	p->start_time = jiffies;
	task[nr] = p;
	SET_LINKS(p);
	nr_tasks++;

	error = -ENOMEM;
	/* copy all the process information */
	copy_thread(nr, clone_flags, usp, p, regs);
	if (copy_mm(clone_flags, alloc))
		goto bad_fork_cleanup;
	p->semundo = NULL;
	copy_files(clone_flags, alloc);
	copy_fs(clone_flags, alloc);
	copy_sighand(clone_flags, alloc);

	/* ok, now we should be set up.. */
	p->mm->swappable = 1;
	p->exit_signal = clone_flags & CSIGNAL;
	p->counter = current->counter >> 1;
	wake_up_process(p);			/* do this last, just in case */
	return p->pid;

bad_fork_cleanup:
	if (p->exec_domain && p->exec_domain->use_count)
		(*p->exec_domain->use_count)--;
	if (p->binfmt && p->binfmt->use_count)
		(*p->binfmt->use_count)--;
	task[nr] = NULL;
	REMOVE_LINKS(p);
	nr_tasks--;
bad_fork_free:
	free_page(new_stack);
	free_page((long) p);
bad_fork:
	return error;
}

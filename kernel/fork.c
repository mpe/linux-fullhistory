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

#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/unistd.h>
#include <linux/smp_lock.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/uaccess.h>

/* The idle tasks do not count.. */
int nr_tasks=0;
int nr_running=0;

unsigned long int total_forks=0;	/* Handle normal Linux uptimes. */
int last_pid=0;

/* SLAB cache for mm_struct's. */
kmem_cache_t *mm_cachep;

/* SLAB cache for files structs */
kmem_cache_t *files_cachep; 

struct task_struct *pidhash[PIDHASH_SZ];

struct task_struct **tarray_freelist = NULL;
spinlock_t taskslot_lock = SPIN_LOCK_UNLOCKED;

/* UID task count cache, to prevent walking entire process list every
 * single fork() operation.
 */
#define UIDHASH_SZ	(PIDHASH_SZ >> 2)

static struct user_struct {
	atomic_t count;
	struct user_struct *next, **pprev;
	unsigned int uid;
} *uidhash[UIDHASH_SZ];

spinlock_t uidhash_lock = SPIN_LOCK_UNLOCKED;

kmem_cache_t *uid_cachep;

#define uidhashfn(uid)	(((uid >> 8) ^ uid) & (UIDHASH_SZ - 1))

static inline void uid_hash_insert(struct user_struct *up, unsigned int hashent)
{
	spin_lock(&uidhash_lock);
	if((up->next = uidhash[hashent]) != NULL)
		uidhash[hashent]->pprev = &up->next;
	up->pprev = &uidhash[hashent];
	uidhash[hashent] = up;
	spin_unlock(&uidhash_lock);
}

static inline void uid_hash_remove(struct user_struct *up)
{
	spin_lock(&uidhash_lock);
	if(up->next)
		up->next->pprev = up->pprev;
	*up->pprev = up->next;
	spin_unlock(&uidhash_lock);
}

static inline struct user_struct *uid_find(unsigned short uid, unsigned int hashent)
{
	struct user_struct *up;

	spin_lock(&uidhash_lock);
	for(up = uidhash[hashent]; (up && up->uid != uid); up = up->next)
		;
	spin_unlock(&uidhash_lock);
	return up;
}

void free_uid(struct task_struct *p)
{
	struct user_struct *up = p->user;

	if (up) {
		p->user = NULL;
		if (atomic_dec_and_test(&up->count)) {
			uid_hash_remove(up);
			kmem_cache_free(uid_cachep, up);
		}
	}
}

int alloc_uid(struct task_struct *p)
{
	unsigned int hashent = uidhashfn(p->uid);
	struct user_struct *up = uid_find(p->uid, hashent);

	p->user = up;
	if (!up) {
		up = kmem_cache_alloc(uid_cachep, SLAB_KERNEL);
		if (!up)
			return -EAGAIN;
		p->user = up;
		up->uid = p->uid;
		atomic_set(&up->count, 0);
		uid_hash_insert(up, hashent);
	}

	atomic_inc(&up->count);
	return 0;
}

void __init uidcache_init(void)
{
	int i;

	uid_cachep = kmem_cache_create("uid_cache", sizeof(struct user_struct),
				       0,
				       SLAB_HWCACHE_ALIGN, NULL, NULL);
	if(!uid_cachep)
		panic("Cannot create uid taskcount SLAB cache\n");

	for(i = 0; i < UIDHASH_SZ; i++)
		uidhash[i] = 0;
}

static inline struct task_struct ** find_empty_process(void)
{
	struct task_struct **tslot = NULL;

	if ((nr_tasks < NR_TASKS - MIN_TASKS_LEFT_FOR_ROOT) || !current->uid)
		tslot = get_free_taskslot();
	return tslot;
}

/* Protects next_safe and last_pid. */
spinlock_t lastpid_lock = SPIN_LOCK_UNLOCKED;

static int get_pid(unsigned long flags)
{
	static int next_safe = PID_MAX;
	struct task_struct *p;

	if (flags & CLONE_PID)
		return current->pid;

	spin_lock(&lastpid_lock);
	if((++last_pid) & 0xffff8000) {
		last_pid = 300;		/* Skip daemons etc. */
		goto inside;
	}
	if(last_pid >= next_safe) {
inside:
		next_safe = PID_MAX;
		read_lock(&tasklist_lock);
	repeat:
		for_each_task(p) {
			if(p->pid == last_pid	||
			   p->pgrp == last_pid	||
			   p->session == last_pid) {
				if(++last_pid >= next_safe) {
					if(last_pid & 0xffff8000)
						last_pid = 300;
					next_safe = PID_MAX;
					goto repeat;
				}
			}
			if(p->pid > last_pid && next_safe > p->pid)
				next_safe = p->pid;
			if(p->pgrp > last_pid && next_safe > p->pgrp)
				next_safe = p->pgrp;
			if(p->session > last_pid && next_safe > p->session)
				next_safe = p->session;
		}
		read_unlock(&tasklist_lock);
	}
	spin_unlock(&lastpid_lock);

	return last_pid;
}

static inline int dup_mmap(struct mm_struct * mm)
{
	struct vm_area_struct * mpnt, *tmp, **pprev;
	int retval;

	flush_cache_mm(current->mm);
	pprev = &mm->mmap;
	for (mpnt = current->mm->mmap ; mpnt ; mpnt = mpnt->vm_next) {
		struct file *file;

		retval = -ENOMEM;
		tmp = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
		if (!tmp)
			goto fail_nomem;
		*tmp = *mpnt;
		tmp->vm_flags &= ~VM_LOCKED;
		tmp->vm_mm = mm;
		mm->map_count++;
		tmp->vm_next = NULL;
		file = tmp->vm_file;
		if (file) {
			file->f_count++;
			if (tmp->vm_flags & VM_DENYWRITE)
				file->f_dentry->d_inode->i_writecount--;
      
			/* insert tmp into the share list, just after mpnt */
			if((tmp->vm_next_share = mpnt->vm_next_share) != NULL)
				mpnt->vm_next_share->vm_pprev_share =
					&tmp->vm_next_share;
			mpnt->vm_next_share = tmp;
			tmp->vm_pprev_share = &mpnt->vm_next_share;
		}

		/* Copy the pages, but defer checking for errors */
		retval = copy_page_range(mm, current->mm, tmp);
		if (!retval && tmp->vm_ops && tmp->vm_ops->open)
			tmp->vm_ops->open(tmp);

		/*
		 * Link in the new vma even if an error occurred,
		 * so that exit_mmap() can clean up the mess.
		 */
		tmp->vm_next = *pprev;
		*pprev = tmp;

		pprev = &tmp->vm_next;
		if (retval)
			goto fail_nomem;
	}
	retval = 0;
	if (mm->map_count >= AVL_MIN_MAP_COUNT)
		build_mmap_avl(mm);

fail_nomem:
	flush_tlb_mm(current->mm);
	return retval;
}

/*
 * Allocate and initialize an mm_struct.
 *
 * NOTE! The mm mutex will be locked until the
 * caller decides that all systems are go..
 */
struct mm_struct * mm_alloc(void)
{
	struct mm_struct * mm;

	mm = kmem_cache_alloc(mm_cachep, SLAB_KERNEL);
	if (mm) {
		*mm = *current->mm;
		init_new_context(mm);
		atomic_set(&mm->count, 1);
		mm->map_count = 0;
		mm->def_flags = 0;
		mm->mmap_sem = MUTEX_LOCKED;
		/*
		 * Leave mm->pgd set to the parent's pgd
		 * so that pgd_offset() is always valid.
		 */
		mm->mmap = mm->mmap_avl = mm->mmap_cache = NULL;

		/* It has not run yet, so cannot be present in anyone's
		 * cache or tlb.
		 */
		mm->cpu_vm_mask = 0;
	}
	return mm;
}

/* Please note the differences between mmput and mm_release.
 * mmput is called whenever we stop holding onto a mm_struct,
 * error success whatever.
 *
 * mm_release is called after a mm_struct has been removed
 * from the current process.
 *
 * This difference is important for error handling, when we
 * only half set up a mm_struct for a new process and need to restore
 * the old one.  Because we mmput the new mm_struct before
 * restoring the old one. . .
 * Eric Biederman 10 January 1998
 */
void mm_release(void)
{
	struct task_struct *tsk = current;
	forget_segments();
	/* notify parent sleeping on vfork() */
	if (tsk->flags & PF_VFORK) {
		tsk->flags &= ~PF_VFORK;
		up(tsk->p_opptr->vfork_sem);
	}
}

/*
 * Decrement the use count and release all resources for an mm.
 */
void mmput(struct mm_struct *mm)
{
	if (atomic_dec_and_test(&mm->count)) {
		release_segments(mm);
		exit_mmap(mm);
		free_page_tables(mm);
		kmem_cache_free(mm_cachep, mm);
	}
}

static inline int copy_mm(int nr, unsigned long clone_flags, struct task_struct * tsk)
{
	struct mm_struct * mm;
	int retval;

	if (clone_flags & CLONE_VM) {
		mmget(current->mm);
		/*
		 * Set up the LDT descriptor for the clone task.
		 */
		copy_segments(nr, tsk, NULL);
		SET_PAGE_DIR(tsk, current->mm->pgd);
		return 0;
	}

	retval = -ENOMEM;
	mm = mm_alloc();
	if (!mm)
		goto fail_nomem;

	tsk->mm = mm;
	tsk->min_flt = tsk->maj_flt = 0;
	tsk->cmin_flt = tsk->cmaj_flt = 0;
	tsk->nswap = tsk->cnswap = 0;
	copy_segments(nr, tsk, mm);
	retval = new_page_tables(tsk);
	if (retval)
		goto free_mm;
	retval = dup_mmap(mm);
	if (retval)
		goto free_pt;
	up(&mm->mmap_sem);
	return 0;

free_mm:
	mm->pgd = NULL;
free_pt:
	tsk->mm = NULL;
	mmput(mm);
fail_nomem:
	return retval;
}

static inline int copy_fs(unsigned long clone_flags, struct task_struct * tsk)
{
	if (clone_flags & CLONE_FS) {
		atomic_inc(&current->fs->count);
		return 0;
	}
	tsk->fs = kmalloc(sizeof(*tsk->fs), GFP_KERNEL);
	if (!tsk->fs)
		return -1;
	atomic_set(&tsk->fs->count, 1);
	tsk->fs->umask = current->fs->umask;
	tsk->fs->root = dget(current->fs->root);
	tsk->fs->pwd = dget(current->fs->pwd);
	return 0;
}

/*
 * Copy a fd_set and compute the maximum fd it contains. 
 */
static inline int __copy_fdset(unsigned long *d, unsigned long *src)
{
	int i; 
	unsigned long *p = src; 
	unsigned long *max = src; 

	for (i = __FDSET_LONGS; i; --i) {
		if ((*d++ = *p++) != 0) 
			max = p; 
	}
	return (max - src)*sizeof(long)*8; 
}

static inline int copy_fdset(fd_set *dst, fd_set *src)
{
	return __copy_fdset(dst->fds_bits, src->fds_bits);  
}

static int copy_files(unsigned long clone_flags, struct task_struct * tsk)
{
	struct files_struct *oldf, *newf;
	struct file **old_fds, **new_fds;
	int size, i, error = 0;

	/*
	 * A background process may not have any files ...
	 */
	oldf = current->files;
	if (!oldf)
		goto out;

	if (clone_flags & CLONE_FILES) {
		atomic_inc(&oldf->count);
		goto out;
	}

	tsk->files = NULL;
	error = -ENOMEM;
	newf = kmem_cache_alloc(files_cachep, SLAB_KERNEL);
	if (!newf) 
		goto out;

	/*
	 * Allocate the fd array, using get_free_page() if possible.
	 * Eventually we want to make the array size variable ...
	 */
	size = NR_OPEN * sizeof(struct file *);
	if (size == PAGE_SIZE)
		new_fds = (struct file **) __get_free_page(GFP_KERNEL);
	else
		new_fds = (struct file **) kmalloc(size, GFP_KERNEL);
	if (!new_fds)
		goto out_release;

	atomic_set(&newf->count, 1);
	newf->max_fds = NR_OPEN;
	newf->fd = new_fds;
	newf->close_on_exec = oldf->close_on_exec;
	i = copy_fdset(&newf->open_fds, &oldf->open_fds);

	old_fds = oldf->fd;
	for (; i != 0; i--) {
		struct file *f = *old_fds++;
		*new_fds = f;
		if (f)
			f->f_count++;
		new_fds++;
	}
	/* This is long word aligned thus could use a optimized version */ 
	memset(new_fds, 0, (char *)newf->fd + size - (char *)new_fds); 
      
	tsk->files = newf;
	error = 0;
out:
	return error;

out_release:
	kmem_cache_free(files_cachep, newf);
	goto out;
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

static inline void copy_flags(unsigned long clone_flags, struct task_struct *p)
{
	unsigned long new_flags = p->flags;

	new_flags &= ~(PF_SUPERPRIV | PF_USEDFPU | PF_VFORK);
	new_flags |= PF_FORKNOEXEC;
	if (!(clone_flags & CLONE_PTRACE))
		new_flags &= ~(PF_PTRACED|PF_TRACESYS);
	if (clone_flags & CLONE_VFORK)
		new_flags |= PF_VFORK;
	p->flags = new_flags;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in its entirety.
 */
int do_fork(unsigned long clone_flags, unsigned long usp, struct pt_regs *regs)
{
	int nr;
	int retval = -ENOMEM;
	struct task_struct *p;
	struct semaphore sem = MUTEX_LOCKED;

	current->vfork_sem = &sem;

	p = alloc_task_struct();
	if (!p)
		goto fork_out;

	*p = *current;

	down(&current->mm->mmap_sem);
	lock_kernel();

	if (p->user) {
		if (atomic_read(&p->user->count) >= p->rlim[RLIMIT_NPROC].rlim_cur)
			goto bad_fork_free;
	}

	{
		struct task_struct **tslot;
		tslot = find_empty_process();
		retval = -EAGAIN;
		if (!tslot)
			goto bad_fork_free;
		p->tarray_ptr = tslot;
		*tslot = p;
		nr = tslot - &task[0];
	}

	if (p->exec_domain && p->exec_domain->module)
		__MOD_INC_USE_COUNT(p->exec_domain->module);
	if (p->binfmt && p->binfmt->module)
		__MOD_INC_USE_COUNT(p->binfmt->module);

	p->did_exec = 0;
	p->swappable = 0;
	p->state = TASK_UNINTERRUPTIBLE;

	copy_flags(clone_flags, p);
	p->pid = get_pid(clone_flags);

	/*
	 * This is a "shadow run" state. The process
	 * is marked runnable, but isn't actually on
	 * any run queue yet.. (that happens at the
	 * very end).
	 */
	p->state = TASK_RUNNING;
	p->next_run = p;
	p->prev_run = p;

	p->p_pptr = p->p_opptr = current;
	p->p_cptr = NULL;
	init_waitqueue(&p->wait_chldexit);
	p->vfork_sem = NULL;

	p->sigpending = 0;
	sigemptyset(&p->signal);
	p->sigqueue = NULL;
	p->sigqueue_tail = &p->sigqueue;

	p->it_real_value = p->it_virt_value = p->it_prof_value = 0;
	p->it_real_incr = p->it_virt_incr = p->it_prof_incr = 0;
	init_timer(&p->real_timer);
	p->real_timer.data = (unsigned long) p;

	p->leader = 0;		/* session leadership doesn't inherit */
	p->tty_old_pgrp = 0;
	p->times.tms_utime = p->times.tms_stime = 0;
	p->times.tms_cutime = p->times.tms_cstime = 0;
#ifdef __SMP__
	{
		int i;
		p->has_cpu = 0;
		p->processor = NO_PROC_ID;
		/* ?? should we just memset this ?? */
		for(i = 0; i < smp_num_cpus; i++)
			p->per_cpu_utime[i] = p->per_cpu_stime[i] = 0;
		spin_lock_init(&p->sigmask_lock);
	}
#endif
	p->lock_depth = -1;		/* -1 = no lock */
	p->start_time = jiffies;

	retval = -ENOMEM;
	/* copy all the process information */
	if (copy_files(clone_flags, p))
		goto bad_fork_cleanup;
	if (copy_fs(clone_flags, p))
		goto bad_fork_cleanup_files;
	if (copy_sighand(clone_flags, p))
		goto bad_fork_cleanup_fs;
	if (copy_mm(nr, clone_flags, p))
		goto bad_fork_cleanup_sighand;
	retval = copy_thread(nr, clone_flags, usp, p, regs);
	if (retval)
		goto bad_fork_cleanup_sighand;
	p->semundo = NULL;

	/* ok, now we should be set up.. */
	p->swappable = 1;
	p->exit_signal = clone_flags & CSIGNAL;
	p->pdeath_signal = 0;

	/*
	 * "share" dynamic priority between parent and child, thus the
	 * total amount of dynamic priorities in the system doesnt change,
	 * more scheduling fairness. This is only important in the first
	 * timeslice, on the long run the scheduling behaviour is unchanged.
	 */
	current->counter >>= 1;
	p->counter = current->counter;

	/*
	 * Ok, add it to the run-queues and make it
	 * visible to the rest of the system.
	 *
	 * Let it rip!
	 */
	retval = p->pid;
	if (retval) {
		write_lock_irq(&tasklist_lock);
		SET_LINKS(p);
		hash_pid(p);
		write_unlock_irq(&tasklist_lock);

		nr_tasks++;
		if (p->user)
			atomic_inc(&p->user->count);

		p->next_run = NULL;
		p->prev_run = NULL;
		wake_up_process(p);		/* do this last */
	}
	++total_forks;
bad_fork:
	unlock_kernel();
	up(&current->mm->mmap_sem);
fork_out:
	if ((clone_flags & CLONE_VFORK) && (retval > 0)) 
		down(&sem);
	return retval;

bad_fork_cleanup_sighand:
	exit_sighand(p);
bad_fork_cleanup_fs:
	exit_fs(p); /* blocking */
bad_fork_cleanup_files:
	exit_files(p); /* blocking */
bad_fork_cleanup:
	if (p->exec_domain && p->exec_domain->module)
		__MOD_DEC_USE_COUNT(p->exec_domain->module);
	if (p->binfmt && p->binfmt->module)
		__MOD_DEC_USE_COUNT(p->binfmt->module);

	add_free_taskslot(p->tarray_ptr);
bad_fork_free:
	free_task_struct(p);
	goto bad_fork;
}

void __init filescache_init(void)
{
	files_cachep = kmem_cache_create("files_cache", 
					 sizeof(struct files_struct),
					 0, 
					 SLAB_HWCACHE_ALIGN,
					 NULL, NULL);
	if (!files_cachep) 
		panic("Cannot create files cache"); 
}

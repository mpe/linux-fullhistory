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

#include <linux/init.h>
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

/* SLAB cache for mm_struct's. */
kmem_cache_t *mm_cachep;

/* SLAB cache for files structs */
kmem_cache_t *files_cachep; 

struct task_struct *pidhash[PIDHASH_SZ];
spinlock_t pidhash_lock = SPIN_LOCK_UNLOCKED;

struct task_struct **tarray_freelist = NULL;
spinlock_t taskslot_lock = SPIN_LOCK_UNLOCKED;

/* UID task count cache, to prevent walking entire process list every
 * single fork() operation.
 */
#define UIDHASH_SZ	(PIDHASH_SZ >> 2)

static struct uid_taskcount {
	struct uid_taskcount *next, **pprev;
	unsigned short uid;
	int task_count;
} *uidhash[UIDHASH_SZ];

#ifdef __SMP__
static spinlock_t uidhash_lock = SPIN_LOCK_UNLOCKED;
#endif

kmem_cache_t *uid_cachep;

#define uidhashfn(uid)	(((uid >> 8) ^ uid) & (UIDHASH_SZ - 1))

static inline void uid_hash_insert(struct uid_taskcount *up, unsigned int hashent)
{
	spin_lock(&uidhash_lock);
	if((up->next = uidhash[hashent]) != NULL)
		uidhash[hashent]->pprev = &up->next;
	up->pprev = &uidhash[hashent];
	uidhash[hashent] = up;
	spin_unlock(&uidhash_lock);
}

static inline void uid_hash_remove(struct uid_taskcount *up)
{
	spin_lock(&uidhash_lock);
	if(up->next)
		up->next->pprev = up->pprev;
	*up->pprev = up->next;
	spin_unlock(&uidhash_lock);
}

static inline struct uid_taskcount *uid_find(unsigned short uid, unsigned int hashent)
{
	struct uid_taskcount *up;

	spin_lock(&uidhash_lock);
	for(up = uidhash[hashent]; (up && up->uid != uid); up = up->next)
		;
	spin_unlock(&uidhash_lock);
	return up;
}

int charge_uid(struct task_struct *p, int count)
{
	unsigned int hashent = uidhashfn(p->uid);
	struct uid_taskcount *up = uid_find(p->uid, hashent);

	if(up) {
		int limit = p->rlim[RLIMIT_NPROC].rlim_cur;
		int newcnt = up->task_count + count;

		if(newcnt > limit)
			return -EAGAIN;
		else if(newcnt == 0) {
			uid_hash_remove(up);
			kmem_cache_free(uid_cachep, up);
			return 0;
		}
	} else {
		up = kmem_cache_alloc(uid_cachep, SLAB_KERNEL);
		if(!up)
			return -EAGAIN;
		up->uid = p->uid;
		up->task_count = 0;
		uid_hash_insert(up, hashent);
	}
	up->task_count += count;
	return 0;
}

__initfunc(void uidcache_init(void))
{
	int i;

	uid_cachep = kmem_cache_create("uid_cache", sizeof(struct uid_taskcount),
				       0,
				       SLAB_HWCACHE_ALIGN, NULL, NULL);
	if(!uid_cachep)
		panic("Cannot create uid taskcount SLAB cache\n");

	for(i = 0; i < UIDHASH_SZ; i++)
		uidhash[i] = 0;
}

static inline int find_empty_process(void)
{
	struct task_struct **tslot;

	if(current->uid) {
		int error;

		if(nr_tasks >= NR_TASKS - MIN_TASKS_LEFT_FOR_ROOT)
			return -EAGAIN;
		if((error = charge_uid(current, 1)) < 0)
			return error;
	}
	tslot = get_free_taskslot();
	if(tslot)
		return tslot - &task[0];
	return -EAGAIN;
}

#ifdef __SMP__
/* Protects next_safe and last_pid. */
static spinlock_t lastpid_lock = SPIN_LOCK_UNLOCKED;
#endif

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
		struct dentry *dentry;

		retval = -ENOMEM;
		tmp = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
		if (!tmp)
			goto fail_nomem;
		*tmp = *mpnt;
		tmp->vm_flags &= ~VM_LOCKED;
		tmp->vm_mm = mm;
		tmp->vm_next = NULL;
		dentry = tmp->vm_dentry;
		if (dentry) {
			dget(dentry);
			if (tmp->vm_flags & VM_DENYWRITE)
				dentry->d_inode->i_writecount--;
      
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
		if((tmp->vm_next = *pprev) != NULL)
			(*pprev)->vm_pprev = &tmp->vm_next;
		*pprev = tmp;
		tmp->vm_pprev = pprev;

		pprev = &tmp->vm_next;
		if (retval)
			goto fail_nomem;
	}
	retval = 0;

fail_nomem:
	flush_tlb_mm(current->mm);
	return retval;
}

/*
 * Allocate and initialize an mm_struct.
 */
struct mm_struct * mm_alloc(void)
{
	struct mm_struct * mm;

	mm = kmem_cache_alloc(mm_cachep, SLAB_KERNEL);
	if (mm) {
		*mm = *current->mm;
		init_new_context(mm);
		mm->count = 1;
		mm->def_flags = 0;
		mm->mmap_sem = MUTEX;
		/*
		 * Leave mm->pgd set to the parent's pgd
		 * so that pgd_offset() is always valid.
		 */
		mm->mmap = mm->mmap_cache = NULL;

		/* It has not run yet, so cannot be present in anyone's
		 * cache or tlb.
		 */
		mm->cpu_vm_mask = 0;
	}
	return mm;
}

/*
 * Decrement the use count and release all resources for an mm.
 */
void mmput(struct mm_struct *mm)
{
	if (!--mm->count) {
		exit_mmap(mm);
		free_page_tables(mm);
		kmem_cache_free(mm_cachep, mm);
	}
}

static inline int copy_mm(unsigned long clone_flags, struct task_struct * tsk)
{
	struct mm_struct * mm;
	int retval;

	if (clone_flags & CLONE_VM) {
		mmget(current->mm);
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
	retval = new_page_tables(tsk);
	if (retval)
		goto free_mm;
	retval = dup_mmap(mm);
	if (retval)
		goto free_pt;
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
		current->fs->count++;
		return 0;
	}
	tsk->fs = kmalloc(sizeof(*tsk->fs), GFP_KERNEL);
	if (!tsk->fs)
		return -1;
	tsk->fs->count = 1;
	tsk->fs->umask = current->fs->umask;
	tsk->fs->root = dget(current->fs->root);
	tsk->fs->pwd = dget(current->fs->pwd);
	return 0;
}

/* return value is only accurate by +-sizeof(long)*8 fds */ 
/* XXX make this architecture specific */
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

static inline int copy_files(unsigned long clone_flags, struct task_struct * tsk)
{
	int i;  
	struct files_struct *oldf, *newf;
	struct file **old_fds, **new_fds;

	/*
	 * A background process may not have any files ...
	 */
	oldf = current->files;
	if (!oldf)
		return 0;

	if (clone_flags & CLONE_FILES) {
		oldf->count++;
		return 0;
	}

	newf = kmem_cache_alloc(files_cachep, SLAB_KERNEL);
	tsk->files = newf;
	if (!newf) 
		return -1;

	newf->count = 1;
	newf->close_on_exec = oldf->close_on_exec;
	i = copy_fdset(&newf->open_fds,&oldf->open_fds);

	old_fds = oldf->fd;
	new_fds = newf->fd;
	for (; i != 0; i--) {
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
        int i;
	int nr;
	int error = -ENOMEM;
	struct task_struct *p;

	lock_kernel();
	p = alloc_task_struct();
	if (!p)
		goto bad_fork;

	error = -EAGAIN;
	nr = find_empty_process();
	if (nr < 0)
		goto bad_fork_free;

	*p = *current;

	if (p->exec_domain && p->exec_domain->module)
		__MOD_INC_USE_COUNT(p->exec_domain->module);
	if (p->binfmt && p->binfmt->module)
		__MOD_INC_USE_COUNT(p->binfmt->module);

	p->did_exec = 0;
	p->swappable = 0;
	p->state = TASK_UNINTERRUPTIBLE;
	p->flags &= ~(PF_PTRACED|PF_TRACESYS|PF_SUPERPRIV);
	p->sigpending = 0;
	p->flags |= PF_FORKNOEXEC;
	p->pid = get_pid(clone_flags);
	p->next_run = NULL;
	p->prev_run = NULL;
	p->p_pptr = p->p_opptr = current;
	p->p_cptr = NULL;
	init_waitqueue(&p->wait_chldexit);
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
	p->has_cpu = 0;
	p->processor = NO_PROC_ID;
	/* ?? should we just memset this ?? */
	for(i = 0; i < smp_num_cpus; i++)
		p->per_cpu_utime[i] = p->per_cpu_stime[i] = 0;
#endif
	p->lock_depth = 0;
	p->start_time = jiffies;
	p->tarray_ptr = &task[nr];
	*p->tarray_ptr = p;
	SET_LINKS(p);
	hash_pid(p);
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
	p->pdeath_signal = 0;

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
bad_fork:
	unlock_kernel();
	return error;

bad_fork_cleanup_sighand:
	exit_sighand(p);
bad_fork_cleanup_fs:
	exit_fs(p); /* blocking */
bad_fork_cleanup_files:
	exit_files(p); /* blocking */
bad_fork_cleanup:
	charge_uid(current, -1);
	if (p->exec_domain && p->exec_domain->module)
		__MOD_DEC_USE_COUNT(p->exec_domain->module);
	if (p->binfmt && p->binfmt->module)
		__MOD_DEC_USE_COUNT(p->binfmt->module);
	add_free_taskslot(p->tarray_ptr);
	unhash_pid(p);
	REMOVE_LINKS(p);
	nr_tasks--;
bad_fork_free:
	free_task_struct(p);
	goto bad_fork;
}

static void files_ctor(void *fp, kmem_cache_t *cachep, unsigned long flags)
{
	struct files_struct *f = fp;

	memset(f, 0, sizeof(*f));
}

__initfunc(void filescache_init(void))
{
	files_cachep = kmem_cache_create("files_cache", 
					 sizeof(struct files_struct),
					 0, 
					 SLAB_HWCACHE_ALIGN,
					 files_ctor, NULL);
	if (!files_cachep) 
		panic("Cannot create files cache"); 
}

/*
 *  linux/fs/locks.c
 *
 *  Provide support for fcntl()'s F_GETLK, F_SETLK, and F_SETLKW calls.
 *  Doug Evans (dje@spiff.uucp), August 07, 1992
 *
 *  Deadlock detection added.
 *  FIXME: one thing isn't handled yet:
 *	- mandatory locks (requires lots of changes elsewhere)
 *  Kelly Carmichael (kelly@[142.24.8.65]), September 17, 1994.
 *
 *  Miscellaneous edits, and a total rewrite of posix_lock_file() code.
 *  Kai Petzke (wpp@marie.physik.tu-berlin.de), 1994
 *  
 *  Converted file_lock_table to a linked list from an array, which eliminates
 *  the limits on how many active file locks are open.
 *  Chad Page (pageone@netcom.com), November 27, 1994
 * 
 *  Removed dependency on file descriptors. dup()'ed file descriptors now
 *  get the same locks as the original file descriptors, and a close() on
 *  any file descriptor removes ALL the locks on the file for the current
 *  process. Since locks still depend on the process id, locks are inherited
 *  after an exec() but not after a fork(). This agrees with POSIX, and both
 *  BSD and SVR4 practice.
 *  Andy Walker (andy@lysaker.kvaerner.no), February 14, 1995
 *
 *  Scrapped free list which is redundant now that we allocate locks
 *  dynamically with kmalloc()/kfree().
 *  Andy Walker (andy@lysaker.kvaerner.no), February 21, 1995
 *
 *  Implemented two lock personalities - F_FLOCK and F_POSIX.
 *
 *  F_POSIX locks are created with calls to fcntl() and lockf() through the
 *  fcntl() system call. They have the semantics described above.
 *
 *  F_FLOCK locks are created with calls to flock(), through the flock()
 *  system call, which is new. Old C libraries implement flock() via fcntl()
 *  and will continue to use the old, broken implementation.
 *
 *  F_FLOCK locks follow the 4.4 BSD flock() semantics. They are associated
 *  with a file pointer (filp). As a result they can be shared by a parent
 *  process and its children after a fork(). They are removed when the last
 *  file descriptor referring to the file pointer is closed (unless explicitly
 *  unlocked). 
 *
 *  F_FLOCK locks never deadlock, an existing lock is always removed before
 *  upgrading from shared to exclusive (or vice versa). When this happens
 *  any processes blocked by the current lock are woken up and allowed to
 *  run before the new lock is applied.
 *  Andy Walker (andy@lysaker.kvaerner.no), June 09, 1995
 *
 *  Removed some race conditions in flock_lock_file(), marked other possible
 *  races. Just grep for FIXME to see them. 
 *  Dmitry Gorodchanin (begemot@bgm.rosprint.net), February 09, 1996.
 *
 *  Addressed Dmitry's concerns. Deadlock checking no longer recursive.
 *  Lock allocation changed to GFP_ATOMIC as we can't afford to sleep
 *  once we've checked for blocking and deadlocking.
 *  Andy Walker (andy@lysaker.kvaerner.no), April 03, 1996.
 *
 *  Initial implementation of mandatory locks. SunOS turned out to be
 *  a rotten model, so I implemented the "obvious" semantics.
 *  See 'linux/Documentation/mandatory.txt' for details.
 *  Andy Walker (andy@lysaker.kvaerner.no), April 06, 1996.
 *
 *  Don't allow mandatory locks on mmap()'ed files. Added simple functions to
 *  check if a file has mandatory locks, used by mmap(), open() and creat() to
 *  see if system call should be rejected. Ref. HP-UX/SunOS/Solaris Reference
 *  Manual, Section 2.
 *  Andy Walker (andy@lysaker.kvaerner.no), April 09, 1996.
 *
 *  Tidied up block list handling. Added '/proc/locks' interface.
 *  Andy Walker (andy@lysaker.kvaerner.no), April 24, 1996.
 *
 *  Fixed deadlock condition for pathological code that mixes calls to
 *  flock() and fcntl().
 *  Andy Walker (andy@lysaker.kvaerner.no), April 29, 1996.
 *
 *  Allow only one type of locking scheme (F_POSIX or F_FLOCK) to be in use
 *  for a given file at a time. Changed the CONFIG_MANDATORY_OPTION scheme to
 *  guarantee sensible behaviour in the case where file system modules might
 *  be compiled with different options than the kernel itself.
 *  Andy Walker (andy@lysaker.kvaerner.no), May 15, 1996.
 *
 *  Added a couple of missing wake_up() calls.
 *  Andy Walker (andy@lysaker.kvaerner.no), May 15, 1996.
 *
 *  TODO: Do not honour mandatory locks on remote file systems. This matches
 *        the SVR4 semantics and neatly sidesteps a pile of awkward issues that
 *        would otherwise have to be addressed.
 */

#include <linux/config.h>

#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/fcntl.h>

#include <asm/segment.h>

#define OFFSET_MAX	((off_t)0x7fffffff)	/* FIXME: move elsewhere? */

static int flock_make_lock(struct file *filp, struct file_lock *fl,
			       unsigned int cmd);
static int posix_make_lock(struct file *filp, struct file_lock *fl,
			       struct flock *l);
static int flock_locks_conflict(struct file_lock *caller_fl,
				struct file_lock *sys_fl);
static int posix_locks_conflict(struct file_lock *caller_fl,
				struct file_lock *sys_fl);
static int locks_conflict(struct file_lock *caller_fl, struct file_lock *sys_fl);
static int flock_lock_file(struct file *filp, struct file_lock *caller,
			   unsigned int wait);
static int posix_lock_file(struct file *filp, struct file_lock *caller,
			   unsigned int wait);
static int posix_locks_deadlock(struct task_struct *my_task,
				struct task_struct *blocked_task);
static int locks_overlap(struct file_lock *fl1, struct file_lock *fl2);
static void posix_remove_locks(struct file_lock **before, struct task_struct *task);
static void flock_remove_locks(struct file_lock **before, struct file *filp);

static struct file_lock *locks_alloc_lock(struct file_lock *fl);
static void locks_insert_lock(struct file_lock **pos, struct file_lock *fl);
static void locks_delete_lock(struct file_lock **fl, unsigned int wait);
static char *lock_get_status(struct file_lock *fl, char *p, int id, char *pfx);

static struct file_lock *file_lock_table = NULL;

/* Free lock not inserted in any queue */
static inline void locks_free_lock(struct file_lock *fl)
{
	kfree(fl);
	return;
}

/* Add lock fl to the blocked list pointed to by block.
 * We search to the end of the existing list and insert the the new
 * struct. This ensures processes will be woken up in the order they
 * blocked.
 * NOTE: nowhere does the documentation insist that processes be woken
 * up in this order, but it seems like the reasonable thing to do.
 * If the blocked list gets long then this search could get expensive,
 * in which case we could consider waking the processes up in reverse
 * order, or making the blocked list a doubly linked circular list.
 * 
 * This functions are called only from one place (flock_lock_file)
 * so they are inlined now. -- Dmitry Gorodchanin 02/09/96.
 */

static inline void locks_insert_block(struct file_lock *bfl, 
				      struct file_lock *fl)
{
	while (bfl->fl_block != NULL) {
		bfl = bfl->fl_block;
	}

	bfl->fl_block = fl;
	fl->fl_block = NULL;
	
	return;
}

static inline void locks_delete_block(struct file_lock *bfl,
				      struct file_lock *fl)
{
	struct file_lock *tfl;
	
	while ((tfl = bfl->fl_block) != NULL) {
		if (tfl == fl) {
			bfl->fl_block = fl->fl_block;
			fl->fl_block = NULL;
			return;
		}
		bfl = tfl;
	}
	return;
}

/* flock() system call entry point. Apply a FLOCK style lock to
 * an open file descriptor.
 */
asmlinkage int sys_flock(unsigned int fd, unsigned int cmd)
{
	struct file_lock file_lock;
	struct file *filp;

	if ((fd >= NR_OPEN) || !(filp = current->files->fd[fd]))
		return (-EBADF);

	if (!flock_make_lock(filp, &file_lock, cmd))
		return (-EINVAL);
	
	if ((file_lock.fl_type != F_UNLCK) && !(filp->f_mode & 3))
		return (-EBADF);
	
	return (flock_lock_file(filp, &file_lock, cmd & LOCK_UN ? 0 : cmd & LOCK_NB ? 0 : 1));
}

/* Report the first existing lock that would conflict with l.
 * This implements the F_GETLK command of fcntl().
 */
int fcntl_getlk(unsigned int fd, struct flock *l)
{
	int error;
	struct flock flock;
	struct file *filp;
	struct file_lock *fl,file_lock;

	if ((fd >= NR_OPEN) || !(filp = current->files->fd[fd]))
		return (-EBADF);
	error = verify_area(VERIFY_WRITE, l, sizeof(*l));
	if (error)
		return (error);

	memcpy_fromfs(&flock, l, sizeof(flock));
	if ((flock.l_type == F_UNLCK) || (flock.l_type == F_EXLCK) ||
	    (flock.l_type == F_SHLCK))
		return (-EINVAL);

	if (!filp->f_inode || !posix_make_lock(filp, &file_lock, &flock))
		return (-EINVAL);

	if ((fl = filp->f_inode->i_flock) && (fl->fl_flags & F_POSIX)) { 
		while (fl != NULL) {
			if (posix_locks_conflict(&file_lock, fl)) {
				flock.l_pid = fl->fl_owner->pid;
				flock.l_start = fl->fl_start;
				flock.l_len = fl->fl_end == OFFSET_MAX ? 0 :
					fl->fl_end - fl->fl_start + 1;
				flock.l_whence = 0;
				flock.l_type = fl->fl_type;
				memcpy_tofs(l, &flock, sizeof(flock));
				return (0);
			}
			fl = fl->fl_next;
		}
	}

	flock.l_type = F_UNLCK;			/* no conflict found */
	memcpy_tofs(l, &flock, sizeof(flock));
	return (0);
}

/* Apply the lock described by l to an open file descriptor.
 * This implements both the F_SETLK and F_SETLKW commands of fcntl().
 * It also emulates flock() in a pretty broken way for older C
 * libraries.
 */
int fcntl_setlk(unsigned int fd, unsigned int cmd, struct flock *l)
{
	int error;
	struct file *filp;
	struct file_lock file_lock;
	struct flock flock;
	struct inode *inode;

	/*
	 * Get arguments and validate them ...
	 */

	if ((fd >= NR_OPEN) || !(filp = current->files->fd[fd]))
		return (-EBADF);
	
	error = verify_area(VERIFY_READ, l, sizeof(*l));
	if (error)
		return (error);
	
	if (!(inode = filp->f_inode))
		return (-EINVAL);
	
#ifdef CONFIG_LOCK_MANDATORY
	/* Don't allow mandatory locks on files that may be memory mapped
	 * and shared.
	 */
	if ((inode->i_mode & (S_ISGID | S_IXGRP)) == S_ISGID && inode->i_mmap) {
		struct vm_area_struct *vma = inode->i_mmap;
		do {
			if (vma->vm_flags & VM_MAYSHARE)
				return (-EAGAIN);
			vma = vma->vm_next_share;
		} while (vma != inode->i_mmap);
	}
#endif

	memcpy_fromfs(&flock, l, sizeof(flock));
	if (!posix_make_lock(filp, &file_lock, &flock))
		return (-EINVAL);
	
	switch (flock.l_type) {
	case F_RDLCK :
		if (!(filp->f_mode & 1))
			return (-EBADF);
		break;
	case F_WRLCK :
		if (!(filp->f_mode & 2))
			return (-EBADF);
		break;
	case F_SHLCK :
	case F_EXLCK :
#if 1
/* warn a bit for now, but don't overdo it */
{
	static int count = 0;
	if (!count) {
		count=1;
		printk(KERN_WARNING
		       "fcntl_setlk() called by process %d (%s) with broken flock() emulation\n",
		       current->pid, current->comm);
	}
}
#endif
		if (!(filp->f_mode & 3))
			return (-EBADF);
		break;
	case F_UNLCK :
		break;
	default:
		return -EINVAL;
	}
	
	return (posix_lock_file(filp, &file_lock, cmd == F_SETLKW));
}

/* This function is called when the file is closed.
 */
void locks_remove_locks(struct task_struct *task, struct file *filp)
{
	struct file_lock *fl;

	/* For POSIX locks we free all locks on this file for the given task.
	 * For FLOCK we only free locks on this *open* file if it is the last
	 * close on that file.
	 */
	if ((fl = filp->f_inode->i_flock) != NULL) {
		if (fl->fl_flags & F_POSIX)
			posix_remove_locks(&filp->f_inode->i_flock, task);
		else
			flock_remove_locks(&filp->f_inode->i_flock, filp);
	}

	return;
}

static void posix_remove_locks(struct file_lock **before, struct task_struct *task)
{
	struct file_lock *fl;

	while ((fl = *before) != NULL) {
		if (fl->fl_owner == task)
			locks_delete_lock(before, 0);
		else
			before = &fl->fl_next;
	}

	return;
}

static void flock_remove_locks(struct file_lock **before, struct file *filp)
{
	struct file_lock *fl;

	while ((fl = *before) != NULL) {
		if ((fl->fl_file == filp) && (filp->f_count == 1))
			locks_delete_lock(before, 0);
		else
			before = &fl->fl_next;
	}

	return;
}

int locks_verify_locked(struct inode *inode)
{
#ifdef CONFIG_LOCK_MANDATORY
	/* Candidates for mandatory locking have the setgid bit set
	 * but no group execute bit -  an otherwise meaningless combination.
	 */
	if ((inode->i_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return (locks_mandatory_locked(inode));
#endif
	return (0);
}

int locks_verify_area(int read_write, struct inode *inode, struct file *filp,
		      unsigned int offset, unsigned int count)
{
#ifdef CONFIG_LOCK_MANDATORY	 
	/* Candidates for mandatory locking have the setgid bit set
	 * but no group execute bit -  an otherwise meaningless combination.
	 */
	if ((inode->i_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return (locks_mandatory_area(read_write, inode, filp, offset,
					     count));
#endif					     
	return (0);
}

int locks_mandatory_locked(struct inode *inode)
{
#ifdef CONFIG_LOCK_MANDATORY	
	struct file_lock *fl;

	/* Search the lock list for this inode for any POSIX locks.
	 */
	if ((fl = inode->i_flock) && (fl->fl_flags & F_FLOCK))
		return (0);
	
	while (fl != NULL) {
		if (fl->fl_owner != current)
			return (-EAGAIN);
		fl = fl->fl_next;
	}
#endif
	return (0);
}

int locks_mandatory_area(int read_write, struct inode *inode,
			 struct file *filp, unsigned int offset,
			 unsigned int count)
{
#ifdef CONFIG_LOCK_MANDATORY	
	struct file_lock *fl;

repeat:
	/* Check that there are locks, and that they're not F_FLOCK locks.
	 */
	if ((fl = inode->i_flock) && (fl->fl_flags & F_FLOCK))
		return (0);
	
	/*
	 * Search the lock list for this inode for locks that conflict with
	 * the proposed read/write.
	 */
	while (fl != NULL) {
		if (fl->fl_owner == current ||
		    fl->fl_end < offset || fl->fl_start >= offset + count)
			goto next_lock;

		/*
		 * Block for writes against a "read" lock,
		 * and both reads and writes against a "write" lock.
		 */
		if ((read_write == FLOCK_VERIFY_WRITE) ||
		    (fl->fl_type == F_WRLCK)) {
			if (filp && (filp->f_flags & O_NONBLOCK))
				return (-EAGAIN);
			if (current->signal & ~current->blocked)
				return (-ERESTARTSYS);
			if (posix_locks_deadlock(current, fl->fl_owner))
				return (-EDEADLK);
			interruptible_sleep_on(&fl->fl_wait);
			if (current->signal & ~current->blocked)
				return (-ERESTARTSYS);
			/*
			 * If we've been sleeping someone might have
			 * changed the permissions behind our back.
			 */
			if ((inode->i_mode & (S_ISGID | S_IXGRP)) != S_ISGID)
				break;
			goto repeat;
		}
	next_lock:
		fl = fl->fl_next;
	}
#endif
	return (0);
}

/* Verify a "struct flock" and copy it to a "struct file_lock" as a POSIX
 * style lock.
 */
static int posix_make_lock(struct file *filp, struct file_lock *fl,
			   struct flock *l)
{
	off_t start;

	fl->fl_flags = F_POSIX;

	switch (l->l_type) {
	case F_RDLCK :
	case F_WRLCK :
	case F_UNLCK :
		fl->fl_type = l->l_type;
		break;
	case F_SHLCK :
		fl->fl_type = F_RDLCK;
		fl->fl_flags |= F_BROKEN;
		break;
	case F_EXLCK :
		fl->fl_type = F_WRLCK;
		fl->fl_flags |= F_BROKEN;
		break;
	default :
		return (0);
	}

	switch (l->l_whence) {
	case 0 : /*SEEK_SET*/
		start = 0;
		break;
	case 1 : /*SEEK_CUR*/
		start = filp->f_pos;
		break;
	case 2 : /*SEEK_END*/
		start = filp->f_inode->i_size;
		break;
	default :
		return (0);
	}

	if (((start += l->l_start) < 0) || (l->l_len < 0))
		return (0);
	fl->fl_start = start;	/* we record the absolute position */
	if ((l->l_len == 0) || ((fl->fl_end = start + l->l_len - 1) < 0))
		fl->fl_end = OFFSET_MAX;
	
	fl->fl_file = filp;
	fl->fl_owner = current;
	fl->fl_wait = NULL;		/* just for cleanliness */
	
	return (1);
}

/* Verify a call to flock() and fill in a file_lock structure with
 * an appropriate FLOCK lock.
 */
static int flock_make_lock(struct file *filp, struct file_lock *fl,
			   unsigned int cmd)
{
	if (!filp->f_inode)	/* just in case */
		return (0);

	switch (cmd & ~LOCK_NB) {
	case LOCK_SH :
		fl->fl_type = F_RDLCK;
		break;
	case LOCK_EX :
		fl->fl_type = F_WRLCK;
		break;
	case LOCK_UN :
		fl->fl_type = F_UNLCK;
		break;
	default :
		return (0);
	}

	fl->fl_flags = F_FLOCK;
	fl->fl_start = 0;
	fl->fl_end = OFFSET_MAX;
	fl->fl_file = filp;
	fl->fl_owner = NULL;
	fl->fl_wait = NULL;		/* just for cleanliness */
	
	return (1);
}

/* Determine if lock sys_fl blocks lock caller_fl. POSIX specific
 * checking before calling the locks_conflict().
 */
static int posix_locks_conflict(struct file_lock *caller_fl, struct file_lock *sys_fl)
{
	/* POSIX locks owned by the same process do not conflict with
	 * each other.
	 */
	if (caller_fl->fl_owner == sys_fl->fl_owner)
		return (0);

	return (locks_conflict(caller_fl, sys_fl));
}

/* Determine if lock sys_fl blocks lock caller_fl. FLOCK specific
 * checking before calling the locks_conflict().
 */
static int flock_locks_conflict(struct file_lock *caller_fl, struct file_lock *sys_fl)
{
	/* FLOCK locks referring to the same filp do not conflict with
	 * each other.
	 */
	if (caller_fl->fl_file == sys_fl->fl_file)
		return (0);

	return (locks_conflict(caller_fl, sys_fl));
}

/* Determine if lock sys_fl blocks lock caller_fl. Common functionality
 * checks for overlapping locks and shared/exclusive status.
 */
static int locks_conflict(struct file_lock *caller_fl, struct file_lock *sys_fl)
{
	if (!locks_overlap(caller_fl, sys_fl))
		return (0);

	switch (caller_fl->fl_type) {
	case F_RDLCK :
		return (sys_fl->fl_type == F_WRLCK);
		
	case F_WRLCK :
		return (1);

	default:
		printk("locks_conflict(): impossible lock type - %d\n",
		       caller_fl->fl_type);
		break;
	}
	return (0);	/* This should never happen */
}

/* Check if two locks overlap each other.
 */
static int locks_overlap(struct file_lock *fl1, struct file_lock *fl2)
{
	return ((fl1->fl_end >= fl2->fl_start) &&
		(fl2->fl_end >= fl1->fl_start));
}

/* This function tests for deadlock condition before putting a process to
 * sleep. The detection scheme is no longer recursive. Recursive was neat,
 * but dangerous - we risked stack corruption if the lock data was bad, or
 * if the recursion was too deep for any other reason.
 *
 * We rely on the fact that a task can only be on one lock's wait queue
 * at a time. When we find blocked_task on a wait queue we can re-search
 * with blocked_task equal to that queue's owner, until either blocked_task
 * isn't found, or blocked_task is found on a queue owned by my_task.
 */
static int posix_locks_deadlock(struct task_struct *my_task,
				struct task_struct *blocked_task)
{
	struct wait_queue *dlock_wait;
	struct file_lock *fl;

next_task:
	if (my_task == blocked_task)
		return (1);
	for (fl = file_lock_table; fl != NULL; fl = fl->fl_nextlink) {
		if (fl->fl_owner == NULL || fl->fl_wait == NULL)
			continue;
		dlock_wait = fl->fl_wait;
		do {
			if (dlock_wait->task == blocked_task) {
				if (fl->fl_owner == my_task) {
					return (1);
				}
				blocked_task = fl->fl_owner;
				goto next_task;
			}
			dlock_wait = dlock_wait->next;
		} while (dlock_wait != fl->fl_wait);
	}
	return (0);
}

/* Try to create a FLOCK lock on filp. We always insert new locks at
 * the head of the list.
 */
static int flock_lock_file(struct file *filp, struct file_lock *caller,
			   unsigned int wait)
{
	struct file_lock *fl;
	struct file_lock *new_fl;
	struct file_lock **before;
	int change = 0;

	before = &filp->f_inode->i_flock;

	if ((fl = *before) && (fl->fl_flags & F_POSIX))
		return (-EBUSY);

	while ((fl = *before) != NULL) {
		if (caller->fl_file == fl->fl_file) {
			if (caller->fl_type == fl->fl_type)
				return (0);
			change = 1;
			break;
		}
		before = &fl->fl_next;
	}
	/* change means that we are changing the type of an existing lock, or
	 * or else unlocking it.
	 */
	if (change)
		locks_delete_lock(before, caller->fl_type != F_UNLCK);
	if (caller->fl_type == F_UNLCK)
		return (0);
	if ((new_fl = locks_alloc_lock(caller)) == NULL)
		return (-ENOLCK);
repeat:
	if ((fl = filp->f_inode->i_flock) && (fl->fl_flags & F_POSIX)) {
		locks_free_lock(new_fl);
		return (-EBUSY);
	}

	while (fl != NULL) {
		if (flock_locks_conflict(new_fl, fl)) {
			if (!wait) {
				locks_free_lock(new_fl);
				return (-EAGAIN);
			}
			if (current->signal & ~current->blocked) {
				/* Note: new_fl is not in any queue at this
				 * point, so we must use locks_free_lock()
				 * instead of locks_delete_lock()
				 * 	Dmitry Gorodchanin 09/02/96.
				 */
				locks_free_lock(new_fl);
				return (-ERESTARTSYS);
			}
			locks_insert_block(fl, new_fl);
			interruptible_sleep_on(&new_fl->fl_wait);
			wake_up(&new_fl->fl_wait);
			if (current->signal & ~current->blocked) {
				/* If we are here, than we were awakened
				 * by a signal, so new_fl is still in the
				 * block queue of fl. We need to remove 
				 * new_fl and then free it.
				 * 	Dmitry Gorodchanin 09/02/96.
				 */
				locks_delete_block(fl, new_fl);
				locks_free_lock(new_fl);
				return (-ERESTARTSYS);
			}
			goto repeat;
		}
		fl = fl->fl_next;
	}
	locks_insert_lock(&filp->f_inode->i_flock, new_fl);
	return (0);
}

/* Add a POSIX style lock to a file.
 * We merge adjacent locks whenever possible. POSIX locks come after FLOCK
 * locks in the list and are sorted by owner task, then by starting address
 *
 * Kai Petzke writes:
 * To make freeing a lock much faster, we keep a pointer to the lock before the
 * actual one. But the real gain of the new coding was, that lock_it() and
 * unlock_it() became one function.
 *
 * To all purists: Yes, I use a few goto's. Just pass on to the next function.
 */

static int posix_lock_file(struct file *filp, struct file_lock *caller,
			   unsigned int wait)
{
	struct file_lock *fl;
	struct file_lock *new_fl;
	struct file_lock *left = NULL;
	struct file_lock *right = NULL;
	struct file_lock **before;
	int added = 0;

repeat:
	if ((fl = filp->f_inode->i_flock) && (fl->fl_flags & F_FLOCK))
		return (-EBUSY);

	if (caller->fl_type != F_UNLCK) {
		while (fl != NULL) {
			if (posix_locks_conflict(caller, fl)) {
				if (!wait)
					return (-EAGAIN);
				if (current->signal & ~current->blocked)
					return (-ERESTARTSYS);
				if (posix_locks_deadlock(caller->fl_owner, fl->fl_owner))
					return (-EDEADLK);
				interruptible_sleep_on(&fl->fl_wait);
				if (current->signal & ~current->blocked)
					return (-ERESTARTSYS);
				goto repeat;
			}
			fl = fl->fl_next;
  		}
  	}
	/*
	 * Find the first old lock with the same owner as the new lock.
	 */
	
	before = &filp->f_inode->i_flock;

	/* First skip FLOCK locks and locks owned by other processes.
	 */
	while ((fl = *before) && (caller->fl_owner != fl->fl_owner)) {
		before = &fl->fl_next;
	}
	

	/* Process locks with this owner.
	 */
	while ((fl = *before) && (caller->fl_owner == fl->fl_owner)) {
		/* Detect adjacent or overlapping regions (if same lock type)
		 */
		if (caller->fl_type == fl->fl_type) {
			if (fl->fl_end < caller->fl_start - 1)
				goto next_lock;
			/* If the next lock in the list has entirely bigger
			 * addresses than the new one, insert the lock here.
			 */
			if (fl->fl_start > caller->fl_end + 1)
				break;

			/* If we come here, the new and old lock are of the
			 * same type and adjacent or overlapping. Make one
			 * lock yielding from the lower start address of both
			 * locks to the higher end address.
			 */
			if (fl->fl_start > caller->fl_start)
				fl->fl_start = caller->fl_start;
			else
				caller->fl_start = fl->fl_start;
			if (fl->fl_end < caller->fl_end)
				fl->fl_end = caller->fl_end;
			else
				caller->fl_end = fl->fl_end;
			if (added) {
				locks_delete_lock(before, 0);
				continue;
			}
			caller = fl;
			added = 1;
		}
		else {
			/* Processing for different lock types is a bit
			 * more complex.
			 */
			if (fl->fl_end < caller->fl_start)
				goto next_lock;
			if (fl->fl_start > caller->fl_end)
				break;
			if (caller->fl_type == F_UNLCK)
				added = 1;
			if (fl->fl_start < caller->fl_start)
				left = fl;
			/* If the next lock in the list has a higher end
			 * address than the new one, insert the new one here.
			 */
			if (fl->fl_end > caller->fl_end) {
				right = fl;
				break;
			}
			if (fl->fl_start >= caller->fl_start) {
				/* The new lock completely replaces an old
				 * one (This may happen several times).
				 */
				if (added) {
					locks_delete_lock(before, 0);
					continue;
				}
				/* Replace the old lock with the new one.
				 * Wake up anybody waiting for the old one,
				 * as the change in lock type might satisfy
				 * their needs.
				 */
				wake_up(&fl->fl_wait);
				fl->fl_start = caller->fl_start;
				fl->fl_end = caller->fl_end;
				fl->fl_type = caller->fl_type;
				caller = fl;
				added = 1;
			}
		}
		/* Go on to next lock.
		 */
	next_lock:
		before = &fl->fl_next;
	}

	if (!added) {
		if (caller->fl_type == F_UNLCK)
			return (0);
		if ((new_fl = locks_alloc_lock(caller)) == NULL)
			return (-ENOLCK);
		locks_insert_lock(before, new_fl);
	}
	if (right) {
		if (left == right) {
			/* The new lock breaks the old one in two pieces, so we
			 * have to allocate one more lock (in this case, even
			 * F_UNLCK may fail!).
			 */
			if ((left = locks_alloc_lock(right)) == NULL) {
				if (!added)
					locks_delete_lock(before, 0);
				return (-ENOLCK);
			}
			locks_insert_lock(before, left);
		}
		right->fl_start = caller->fl_end + 1;
		wake_up(&right->fl_wait);
	}
	if (left) {
		left->fl_end = caller->fl_start - 1;
		wake_up(&left->fl_wait);
	}
	return (0);
}

/* Allocate memory for a new lock and initialize its fields from
 * fl. The lock is not inserted into any lists until locks_insert_lock()
 * or locks_insert_block() are called.
 */

static struct file_lock *locks_alloc_lock(struct file_lock *fl)
{
	struct file_lock *tmp;

	/* Okay, let's make a new file_lock structure... */
	if ((tmp = (struct file_lock *)kmalloc(sizeof(struct file_lock),
					       GFP_ATOMIC)) == NULL)
		return (tmp);

	tmp->fl_nextlink = NULL;
	tmp->fl_prevlink = NULL;
	tmp->fl_next = NULL;
	tmp->fl_block = NULL;
	tmp->fl_flags = fl->fl_flags;
	tmp->fl_owner = fl->fl_owner;
	tmp->fl_file = fl->fl_file;
	tmp->fl_wait = NULL;
	tmp->fl_type = fl->fl_type;
	tmp->fl_start = fl->fl_start;
	tmp->fl_end = fl->fl_end;

	return (tmp);
}

/* Insert file lock fl into an inode's lock list at the position indicated
 * by pos. At the same time add the lock to the global file lock list.
 */

static void locks_insert_lock(struct file_lock **pos, struct file_lock *fl)
{
	fl->fl_nextlink = file_lock_table;
	fl->fl_prevlink = NULL;
	if (file_lock_table != NULL)
		file_lock_table->fl_prevlink = fl;
	file_lock_table = fl;
	fl->fl_next = *pos;	/* insert into file's list */
	*pos = fl;

	return;
}

/* Delete a lock and free it.
 * First remove our lock from the lock lists. Then remove all the blocked
 * locks from our blocked list, waking up the processes that own them. If
 * told to wait, then sleep on each of these lock's wait queues. Each
 * blocked process will wake up and immediately wake up its own wait queue
 * allowing us to be scheduled again. Lastly, wake up our own wait queue
 * before freeing the file_lock structure.
 */

static void locks_delete_lock(struct file_lock **fl_p, unsigned int wait)
{
	struct file_lock *fl;
	struct file_lock *pfl;
	struct file_lock *nfl;
	
	fl = *fl_p;
	*fl_p = fl->fl_next;
	pfl = fl->fl_prevlink;
	nfl = fl->fl_nextlink;

	if (nfl != NULL)
		nfl->fl_prevlink = pfl;

	if (pfl != NULL)
		pfl->fl_nextlink = nfl;
	else
		file_lock_table = nfl;
	
	while ((nfl = fl->fl_block) != NULL) {
		fl->fl_block = nfl->fl_block;
		nfl->fl_block = NULL;
		wake_up(&nfl->fl_wait);
		if (wait)
			sleep_on(&nfl->fl_wait);
	}

	wake_up(&fl->fl_wait);
	kfree(fl);

	return;
}


static char *lock_get_status(struct file_lock *fl, char *p, int id, char *pfx)
{
	struct wait_queue *wt;

	p += sprintf(p, "%d:%s ", id, pfx);
	if (fl->fl_flags & F_POSIX) {
#ifdef CONFIG_LOCK_MANDATORY	 
		p += sprintf(p, "%s %s ",
			     (fl->fl_flags & F_BROKEN) ? "BROKEN" : "POSIX ",
			     ((fl->fl_file->f_inode->i_mode & (S_IXGRP | S_ISGID))
			      == S_ISGID) ? "MANDATORY" : "ADVISORY ");
#else
		p += sprintf(p, "%s ADVISORY ",
			     (fl->fl_flags & F_BROKEN) ? "BROKEN" : "POSIX ");
#endif
	}
	else {
		p += sprintf(p, "FLOCK  ADVISORY  ");
	}
	p += sprintf(p, "%s ", (fl->fl_type == F_RDLCK) ? "READ " : "WRITE");
	p += sprintf(p, "%d %s:%ld %ld %ld ",
		     fl->fl_owner ? fl->fl_owner->pid : 0,
		     kdevname(fl->fl_file->f_inode->i_dev),
		     fl->fl_file->f_inode->i_ino, fl->fl_start,
		     fl->fl_end);
	p += sprintf(p, "%08lx %08lx %08lx %08lx %08lx\n%d:%s",
		     (long)fl, (long)fl->fl_prevlink, (long)fl->fl_nextlink,
		     (long)fl->fl_next, (long)fl->fl_block, id, pfx);
	if ((wt = fl->fl_wait) != NULL) {
		do {
			p += sprintf(p, " %d", wt->task->pid);
			wt = wt->next;
		} while (wt != fl->fl_wait);
	}
	p += sprintf(p, "\n");
	return (p);
}

int get_locks_status(char *buf)
{
	struct file_lock *fl;
	struct file_lock *bfl;
	char *p;
	int i;

	p = buf;
	for (fl = file_lock_table, i = 1; fl != NULL; fl = fl->fl_nextlink, i++) {
		p = lock_get_status(fl, p, i, "");
		for (bfl = fl; bfl->fl_block != NULL; bfl = bfl->fl_block)
			p = lock_get_status(bfl->fl_block, p, i, " ->");
	}
	return  (p - buf);
}


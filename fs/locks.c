/*
 *  linux/fs/locks.c
 *
 *  Provide support for fcntl()'s F_GETLK, F_SETLK, and F_SETLKW calls.
 *  Doug Evans, 92Aug07, dje@sspiff.uucp.
 *
 *  Deadlock Detection added by Kelly Carmichael, kelly@[142.24.8.65]
 *  September 17, 1994.
 *
 *  FIXME: one thing isn't handled yet:
 *	- mandatory locks (requires lots of changes elsewhere)
 *
 *  Edited by Kai Petzke, wpp@marie.physik.tu-berlin.de
 *
 *  Converted file_lock_table to a linked list from an array, which eliminates
 *  the limits on how many active file locks are open - Chad Page
 *  (pageone@netcom.com), November 27, 1994 
 * 
 *  Removed dependency on file descriptors. dup()'ed file descriptors now
 *  get the same locks as the original file descriptors, and a close() on
 *  any file descriptor removes ALL the locks on the file for the current
 *  process. Since locks still depend on the process id, locks are inherited
 *  after an exec() but not after a fork(). This agrees with POSIX, and both
 *  BSD and SVR4 practice.
 *  Andy Walker (andy@keo.kvaerner.no), February 14, 1995
 *
 *  Scrapped free list which is redundant now that we allocate locks
 *  dynamically with kmalloc()/kfree().
 *  Andy Walker (andy@keo.kvaerner.no), February 21, 1995
 *
 */

#define DEADLOCK_DETECTION

#include <asm/segment.h>

#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/fcntl.h>

#define OFFSET_MAX	((off_t)0x7fffffff)	/* FIXME: move elsewhere? */

static int copy_flock(struct file *filp, struct file_lock *fl, struct flock *l);
static int conflict(struct file_lock *caller_fl, struct file_lock *sys_fl);
static int overlap(struct file_lock *fl1, struct file_lock *fl2);
static int lock_it(struct file *filp, struct file_lock *caller);
static struct file_lock *alloc_lock(struct file_lock **pos, struct file_lock *fl);
static void free_lock(struct file_lock **fl);
#ifdef DEADLOCK_DETECTION
int locks_deadlocked(int my_pid,int blocked_pid);
#endif

static struct file_lock *file_lock_table = NULL;

int fcntl_getlk(unsigned int fd, struct flock *l)
{
	int error;
	struct flock flock;
	struct file *filp;
	struct file_lock *fl,file_lock;

	if (fd >= NR_OPEN || !(filp = current->files->fd[fd]))
		return -EBADF;
	error = verify_area(VERIFY_WRITE,l, sizeof(*l));
	if (error)
		return error;
	memcpy_fromfs(&flock, l, sizeof(flock));
	if (flock.l_type == F_UNLCK)
		return -EINVAL;
	if (!copy_flock(filp, &file_lock, &flock))
		return -EINVAL;

	for (fl = filp->f_inode->i_flock; fl != NULL; fl = fl->fl_next) {
		if (conflict(&file_lock, fl)) {
			flock.l_pid = fl->fl_owner->pid;
			flock.l_start = fl->fl_start;
			flock.l_len = fl->fl_end == OFFSET_MAX ? 0 :
				fl->fl_end - fl->fl_start + 1;
			flock.l_whence = fl->fl_whence;
			flock.l_type = fl->fl_type;
			memcpy_tofs(l, &flock, sizeof(flock));
			return 0;
		}
	}

	flock.l_type = F_UNLCK;			/* no conflict found */
	memcpy_tofs(l, &flock, sizeof(flock));
	return 0;
}

/*
 * This function implements both F_SETLK and F_SETLKW.
 */

int fcntl_setlk(unsigned int fd, unsigned int cmd, struct flock *l)
{
	int error;
	struct file *filp;
	struct file_lock *fl,file_lock;
	struct flock flock;

	/*
	 * Get arguments and validate them ...
	 */

	if (fd >= NR_OPEN || !(filp = current->files->fd[fd]))
		return -EBADF;
	error = verify_area(VERIFY_READ, l, sizeof(*l));
	if (error)
		return error;
	memcpy_fromfs(&flock, l, sizeof(flock));
	if (!copy_flock(filp, &file_lock, &flock))
		return -EINVAL;
	switch (file_lock.fl_type) {
	case F_RDLCK :
		if (!(filp->f_mode & 1))
			return -EBADF;
		break;
	case F_WRLCK :
		if (!(filp->f_mode & 2))
			return -EBADF;
		break;
	case F_SHLCK :
		if (!(filp->f_mode & 3))
			return -EBADF;
		file_lock.fl_type = F_RDLCK;
		break;
	case F_EXLCK :
		if (!(filp->f_mode & 3))
			return -EBADF;
		file_lock.fl_type = F_WRLCK;
		break;
	case F_UNLCK :
		break;
	}

  	/*
  	 * Scan for a conflicting lock ...
  	 */
  
	if (file_lock.fl_type != F_UNLCK) {
repeat:
		for (fl = filp->f_inode->i_flock; fl != NULL; fl = fl->fl_next) {
			if (!conflict(&file_lock, fl))
				continue;
			/*
			 * File is locked by another process. If this is
			 * F_SETLKW wait for the lock to be released.
			 */
			if (cmd == F_SETLKW) {
				if (current->signal & ~current->blocked)
					return -ERESTARTSYS;
#ifdef DEADLOCK_DETECTION
				if (locks_deadlocked(file_lock.fl_owner->pid,fl->fl_owner->pid))
					return -EDEADLOCK;
#endif
				interruptible_sleep_on(&fl->fl_wait);
				if (current->signal & ~current->blocked)
					return -ERESTARTSYS;
				goto repeat;
			}
			return -EAGAIN;
  		}
  	}

	/*
	 * Lock doesn't conflict with any other lock ...
	 */

	return lock_it(filp, &file_lock);
}

#ifdef DEADLOCK_DETECTION
/*
 * This function tests for deadlock condition before putting a process to sleep
 * this detection scheme is recursive... we may need some test as to make it
 * exit if the function gets stuck due to bad lock data.
 */

int locks_deadlocked(int my_pid,int blocked_pid)
{
	int ret_val;
	struct wait_queue *dlock_wait;
	struct file_lock *fl;
	for (fl = file_lock_table; fl != NULL; fl = fl->fl_nextlink) {
		if (fl->fl_owner == NULL) continue;	/* not a used lock */
		if (fl->fl_owner->pid != my_pid) continue;
		if (fl->fl_wait == NULL) continue;	/* no queues */
		dlock_wait = fl->fl_wait;
		do {
			if (dlock_wait->task != NULL) {
				if (dlock_wait->task->pid == blocked_pid)
					return -EDEADLOCK;
				ret_val = locks_deadlocked(dlock_wait->task->pid,blocked_pid);
				if (ret_val)
					return -EDEADLOCK;
			}
			dlock_wait = dlock_wait->next;
		} while (dlock_wait != fl->fl_wait);
	}
	return 0;
}
#endif

/*
 * This function is called when the file is closed.
 */

void fcntl_remove_locks(struct task_struct *task, struct file *filp)
{
	struct file_lock *fl;
	struct file_lock **before;

	/* Find first lock owned by caller ... */

	before = &filp->f_inode->i_flock;
	while ((fl = *before) && task != fl->fl_owner)
		before = &fl->fl_next;

	/* The list is sorted by owner and fd ... */

	while ((fl = *before) && task == fl->fl_owner)
		free_lock(before);
}

/*
 * Verify a "struct flock" and copy it to a "struct file_lock" ...
 * Result is a boolean indicating success.
 */

static int copy_flock(struct file *filp, struct file_lock *fl, struct flock *l)
{
	off_t start;

	if (!filp->f_inode)	/* just in case */
		return 0;
	if (l->l_type != F_UNLCK && l->l_type != F_RDLCK && l->l_type != F_WRLCK
	 && l->l_type != F_SHLCK && l->l_type != F_EXLCK)
		return 0;
	switch (l->l_whence) {
	case 0 /*SEEK_SET*/ : start = 0; break;
	case 1 /*SEEK_CUR*/ : start = filp->f_pos; break;
	case 2 /*SEEK_END*/ : start = filp->f_inode->i_size; break;
	default : return 0;
	}
	if ((start += l->l_start) < 0 || l->l_len < 0)
		return 0;
	fl->fl_type = l->l_type;
	fl->fl_start = start;	/* we record the absolute position */
	fl->fl_whence = 0;	/* FIXME: do we record {l_start} as passed? */
	if (l->l_len == 0 || (fl->fl_end = start + l->l_len - 1) < 0)
		fl->fl_end = OFFSET_MAX;
	fl->fl_owner = current;
	fl->fl_wait = NULL;		/* just for cleanliness */
	return 1;
}

/*
 * Determine if lock {sys_fl} blocks lock {caller_fl} ...
 */

static int conflict(struct file_lock *caller_fl, struct file_lock *sys_fl)
{
	if (caller_fl->fl_owner == sys_fl->fl_owner)
		return 0;
	if (!overlap(caller_fl, sys_fl))
		return 0;
	switch (caller_fl->fl_type) {
	case F_RDLCK :
		return sys_fl->fl_type != F_RDLCK;
	case F_WRLCK :
		return 1;	/* overlapping region not owned by caller */
	}
	return 0;	/* shouldn't get here, but just in case */
}

static int overlap(struct file_lock *fl1, struct file_lock *fl2)
{
	return fl1->fl_end >= fl2->fl_start && fl2->fl_end >= fl1->fl_start;
}

/*
 * Add a lock to a file ...
 * Result is 0 for success or -ENOLCK.
 *
 * We merge adjacent locks whenever possible.
 *
 * WARNING: We assume the lock doesn't conflict with any other lock.
 */
  
/*
 * Rewritten by Kai Petzke:
 * We sort the lock list first by owner, then by the starting address.
 *
 * To make freeing a lock much faster, we keep a pointer to the lock before the
 * actual one. But the real gain of the new coding was, that lock_it() and
 * unlock_it() became one function.
 *
 * To all purists: Yes, I use a few goto's. Just pass on to the next function.
 */

static int lock_it(struct file *filp, struct file_lock *caller)
{
	struct file_lock *fl;
	struct file_lock *left = 0;
	struct file_lock *right = 0;
	struct file_lock **before;
	int added = 0;

	/*
	 * Find the first old lock with the same owner as the new lock.
	 */

	before = &filp->f_inode->i_flock;
	while ((fl = *before) && caller->fl_owner != fl->fl_owner)
		before = &fl->fl_next;

	/*
	 * Look up all locks of this owner.
	 */

	while ((fl = *before) && caller->fl_owner == fl->fl_owner) {
		/*
		 * Detect adjacent or overlapping regions (if same lock type)
		 */
		if (caller->fl_type == fl->fl_type) {
			if (fl->fl_end < caller->fl_start - 1)
				goto next_lock;
			/*
			 * If the next lock in the list has entirely bigger
			 * addresses than the new one, insert the lock here.
			 */
			if (fl->fl_start > caller->fl_end + 1)
				break;

			/*
			 * If we come here, the new and old lock are of the
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
				free_lock(before);
				continue;
			}
			caller = fl;
			added = 1;
			goto next_lock;
		}
		/*
		 * Processing for different lock types is a bit more complex.
		 */
		if (fl->fl_end < caller->fl_start)
			goto next_lock;
		if (fl->fl_start > caller->fl_end)
			break;
		if (caller->fl_type == F_UNLCK)
			added = 1;
		if (fl->fl_start < caller->fl_start)
			left = fl;
		/*
		 * If the next lock in the list has a higher end address than
		 * the new one, insert the new one here.
		 */
		if (fl->fl_end > caller->fl_end) {
			right = fl;
			break;
		}
		if (fl->fl_start >= caller->fl_start) {
			/*
			 * The new lock completely replaces an old one (This may
			 * happen several times).
			 */
			if (added) {
				free_lock(before);
				continue;
			}
			/*
			 * Replace the old lock with the new one. Wake up
			 * anybody waiting for the old one, as the change in
			 * lock type might satisfy his needs.
			 */
			wake_up(&fl->fl_wait);
			fl->fl_start = caller->fl_start;
			fl->fl_end   = caller->fl_end;
			fl->fl_type  = caller->fl_type;
			caller = fl;
			added = 1;
		}
		/*
		 * Go on to next lock.
		 */
next_lock:
		before = &(*before)->fl_next;
	}

	if (! added) {
		if (caller->fl_type == F_UNLCK) {
/*
 * XXX - under iBCS-2, attempting to unlock a not-locked region is 
 * 	not considered an error condition, although I'm not sure if this 
 * 	should be a default behavior (it makes porting to native Linux easy)
 * 	or a personality option.
 *
 *	Does Xopen/1170 say anything about this?
 *	- drew@Colorado.EDU
 */
#if 0
			return -EINVAL;
#else
			return 0;
#endif
		}
		if (! (caller = alloc_lock(before, caller)))
			return -ENOLCK;
	}
	if (right) {
		if (left == right) {
			/*
			 * The new lock breaks the old one in two pieces, so we
			 * have to allocate one more lock (in this case, even
			 * F_UNLCK may fail!).
			 */
			if (! (left = alloc_lock(before, right))) {
				if (! added)
					free_lock(before);
				return -ENOLCK;
			}
		}
		right->fl_start = caller->fl_end + 1;
	}
	if (left)
		left->fl_end = caller->fl_start - 1;
	return 0;
}

/*
 * File_lock() inserts a lock at the position pos of the linked list.
 */
static struct file_lock *alloc_lock(struct file_lock **pos,
				    struct file_lock *fl)
{
	struct file_lock *tmp;

	/* Okay, let's make a new file_lock structure... */
	tmp = (struct file_lock *)kmalloc(sizeof(struct file_lock), GFP_KERNEL);
	if (!tmp)
		return tmp;
	tmp->fl_nextlink = file_lock_table;
	tmp->fl_prevlink = NULL;
	if (file_lock_table != NULL)
		file_lock_table->fl_prevlink = tmp;
	file_lock_table = tmp;

	tmp->fl_next = *pos;	/* insert into file's list */
	*pos = tmp;

	tmp->fl_owner = current;
	tmp->fl_wait = NULL;

	tmp->fl_type = fl->fl_type;
	tmp->fl_whence = fl->fl_whence;
	tmp->fl_start = fl->fl_start;
	tmp->fl_end = fl->fl_end;

	return tmp;
}

/*
 * Free up a lock...
 */

static void free_lock(struct file_lock **fl_p)
{
	struct file_lock *fl;

	fl = *fl_p;
	*fl_p = (*fl_p)->fl_next;

	if (fl->fl_nextlink != NULL)
		fl->fl_nextlink->fl_prevlink = fl->fl_prevlink;

	if (fl->fl_prevlink != NULL)
		fl->fl_prevlink->fl_nextlink = fl->fl_nextlink;
	else
		file_lock_table = fl->fl_nextlink;

	wake_up(&fl->fl_wait);

	kfree(fl);

	return;
}

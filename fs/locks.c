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
 *  Andy Walker (andy@keo.kvaerner.no), February 14, 1995
 *
 *  Scrapped free list which is redundant now that we allocate locks
 *  dynamically with kmalloc()/kfree().
 *  Andy Walker (andy@keo.kvaerner.no), February 21, 1995
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
 *
 *  NOTE:
 *  I do not intend to implement mandatory locks unless demand is *HUGE*.
 *  They are not in BSD, and POSIX.1 does not require them. I have never
 *  seen any public code that relied on them. As Kelly Carmichael suggests
 *  above, mandatory locks requires lots of changes elsewhere and I am
 *  reluctant to start something so drastic for so little gain.
 *  Andy Walker (andy@keo.kvaerner.no), June 09, 1995
 * 
 *  Removed some race conditions in flock_lock_file(), marked other possible
 *  races. Just grep for FIXME to see them. 
 *  Dmitry Gorodchanin (begemot@bgm.rosprint.net), Feb 09, 1996.
 */

#include <asm/segment.h>

#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/fcntl.h>


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

static struct file_lock *locks_alloc_lock(struct file_lock *fl);
static void locks_insert_lock(struct file_lock **pos, struct file_lock *fl);
static void locks_delete_lock(struct file_lock **fl, unsigned int wait);

static struct file_lock *file_lock_table = NULL;

/* Free lock not inserted in any queue */
static inline void locks_free_lock(struct file_lock **fl)
{
	kfree(*fl);
	*fl = NULL;		       /* Just in case */
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

static inline void locks_insert_block(struct file_lock **block, 
				      struct file_lock *fl)
{
	struct file_lock *bfl;

	while ((bfl = *block) != NULL) {
		block = &bfl->fl_block;
	}

	*block = fl;
	fl->fl_block = NULL;
	
	return;
}

static inline void locks_delete_block(struct file_lock **block,
				      struct file_lock *fl)
{
	struct file_lock *bfl;
	
	while ((bfl = *block) != NULL) {
		if (bfl == fl) {
			*block = fl->fl_block;
			fl->fl_block = NULL;
			return;
		}
		block = &bfl->fl_block;
	}
}

/* flock() system call entry point. Apply a FLOCK style locks to
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

/* Report the first existing locks that would conflict with l. This implements
 * the F_GETLK command of fcntl().
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

	if (!posix_make_lock(filp, &file_lock, &flock))
		return (-EINVAL);

	for (fl = filp->f_inode->i_flock; fl != NULL; fl = fl->fl_next) {
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
	}

	flock.l_type = F_UNLCK;			/* no conflict found */
	memcpy_tofs(l, &flock, sizeof(flock));
	return (0);
}

/* Apply the lock described by l to an open file descriptor. This implements
 * both the F_SETLK and F_SETLKW commands of fcntl(). It also emulates flock()
 * in a pretty broken way for older C libraries.
 */
int fcntl_setlk(unsigned int fd, unsigned int cmd, struct flock *l)
{
	int error;
	struct file *filp;
	struct file_lock file_lock;
	struct flock flock;

	/*
	 * Get arguments and validate them ...
	 */

	if ((fd >= NR_OPEN) || !(filp = current->files->fd[fd]))
		return (-EBADF);
	
	error = verify_area(VERIFY_READ, l, sizeof(*l));
	if (error)
		return (error);
	
	memcpy_fromfs(&flock, l, sizeof(flock));
	if (!posix_make_lock(filp, &file_lock, &flock))
		return (-EINVAL);
	
	switch (flock.l_type) {
	case F_RDLCK :
		if (!(filp->f_mode & 1))
			return -EBADF;
		break;
	case F_WRLCK :
		if (!(filp->f_mode & 2))
			return -EBADF;
		break;
	case F_SHLCK :
	case F_EXLCK :
		if (!(filp->f_mode & 3))
			return -EBADF;
		break;
	case F_UNLCK :
		break;
	}
	
	return (posix_lock_file(filp, &file_lock, cmd == F_SETLKW));
}

/* This function is called when the file is closed.
 */
void locks_remove_locks(struct task_struct *task, struct file *filp)
{
	struct file_lock *fl;
	struct file_lock **before;

	/* For POSIX locks we free all locks on this file for the given task.
	 * For FLOCK we only free locks on this *open* file if it is the last
	 * close on that file.
	 */
	before = &filp->f_inode->i_flock;
	while ((fl = *before) != NULL) {
		if (((fl->fl_flags == F_POSIX) && (fl->fl_owner == task)) ||
		    ((fl->fl_flags == F_FLOCK) && (fl->fl_file == filp) &&
		     (filp->f_count == 1)))
			locks_delete_lock(before, 0);
		else
			before = &fl->fl_next;
	}

	return;
}

/* Verify a "struct flock" and copy it to a "struct file_lock" as a POSIX
 * style lock.
 */
static int posix_make_lock(struct file *filp, struct file_lock *fl,
			   struct flock *l)
{
	off_t start;

	if (!filp->f_inode)	/* just in case */
		return (0);

	switch (l->l_type) {
	case F_RDLCK :
	case F_WRLCK :
	case F_UNLCK :
		fl->fl_type = l->l_type;
		break;
	case F_SHLCK :
		fl->fl_type = F_RDLCK;
		break;
	case F_EXLCK :
		fl->fl_type = F_WRLCK;
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
	
	fl->fl_flags = F_POSIX;
	fl->fl_file = filp;
	fl->fl_owner = current;
	fl->fl_wait = NULL;		/* just for cleanliness */
	
	return (1);
}

/* Verify a call to flock() and fill in a file_lock structure with an appropriate
 * FLOCK lock.
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
	fl->fl_owner = current;
	fl->fl_wait = NULL;		/* just for cleanliness */
	
	return (1);
}

/* Determine if lock sys_fl blocks lock caller_fl. POSIX specific checking
 * before calling the locks_conflict().
 */
static int posix_locks_conflict(struct file_lock *caller_fl, struct file_lock *sys_fl)
{
	/* POSIX locks owned by the same process do not conflict with
	 * each other.
	 */
	if ((sys_fl->fl_flags == F_POSIX) &&
	    (caller_fl->fl_owner == sys_fl->fl_owner))
		return (0);

	return (locks_conflict(caller_fl, sys_fl));
}

/* Determine if lock sys_fl blocks lock caller_fl. FLOCK specific checking
 * before calling the locks_conflict().
 */
static int flock_locks_conflict(struct file_lock *caller_fl, struct file_lock *sys_fl)
{
	/* FLOCK locks referring to the same filp do not conflict with
	 * each other.
	 */
	if ((sys_fl->fl_flags == F_FLOCK) &&
	    (caller_fl->fl_file == sys_fl->fl_file))
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

/* This function tests for deadlock condition before putting a process to sleep.
 * The detection scheme is recursive... we may need a test to make it exit if the
 * function gets stuck due to bad lock data. 4.4 BSD uses a maximum depth of 50
 * for this.
 * 
 * FIXME: 
 * IMHO this function is dangerous, deep recursion may result in kernel stack
 * corruption. Perhaps we need to limit depth here. 
 *		Dmitry Gorodchanin 09/02/96
 */
static int posix_locks_deadlock(struct task_struct *my_task,
				struct task_struct *blocked_task)
{
	struct wait_queue *dlock_wait;
	struct file_lock *fl;

	for (fl = file_lock_table; fl != NULL; fl = fl->fl_nextlink) {
		if (fl->fl_owner == NULL)
			continue;	/* Should never happen! */
		if (fl->fl_owner != my_task)
			continue;
		if (fl->fl_wait == NULL)
			continue;	/* no queues */
		dlock_wait = fl->fl_wait;
		do {
			if (dlock_wait->task != NULL) {
				if (dlock_wait->task == blocked_task)
					return (-EDEADLOCK);
				if (posix_locks_deadlock(dlock_wait->task, blocked_task))
					return (-EDEADLOCK);
			}
			dlock_wait = dlock_wait->next;
		} while (dlock_wait != fl->fl_wait);
	}
	return (0);
}

/* Try to create a FLOCK lock on filp. We rely on FLOCK locks being sorting
 * first in an inode's lock list, and always insert new locks at the head
 * of the list.
 */
static int flock_lock_file(struct file *filp, struct file_lock *caller,
			   unsigned int wait)
{
	struct file_lock *fl;
	struct file_lock *new_fl;
	struct file_lock **before;
	int change = 0;

	/* This a compact little algorithm based on us always placing FLOCK
	 * locks at the front of the list.
	 */
	before = &filp->f_inode->i_flock;
	while ((fl = *before) && (fl->fl_flags == F_FLOCK)) {
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
	for (fl = filp->f_inode->i_flock; fl != NULL; fl = fl->fl_next) {
		if (!flock_locks_conflict(new_fl, fl))
			continue;
		
		if (wait) {
			if (current->signal & ~current->blocked) {
				/* Note: new_fl is not in any queue at this
				 * point. So we must use locks_free_lock()
				 * instead of locks_delete_lock()
				 * 	Dmitry Gorodchanin 09/02/96.
				 */
				locks_free_lock(&new_fl);
				return (-ERESTARTSYS);
			}
			locks_insert_block(&fl->fl_block, new_fl);
			interruptible_sleep_on(&new_fl->fl_wait);
			wake_up(&new_fl->fl_wait);
			if (current->signal & ~current->blocked) {
				/* If we are here, than we were awaken
				 * by signal, so new_fl is still in 
				 * block queue of fl. We need remove 
				 * new_fl and then free it. 
				 * 	Dmitry Gorodchanin 09/02/96.
				 */

				locks_delete_block(&fl->fl_block, new_fl);
				locks_free_lock(&new_fl);
				return (-ERESTARTSYS);
			}
			goto repeat;
		}
		
		locks_free_lock(&new_fl);
		return (-EAGAIN);
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

	if (caller->fl_type != F_UNLCK) {
repeat:
		for (fl = filp->f_inode->i_flock; fl != NULL; fl = fl->fl_next) {
			if (!posix_locks_conflict(caller, fl))
				continue;
			if (wait) {
				if (current->signal & ~current->blocked)
					return (-ERESTARTSYS);
				if (fl->fl_flags == F_POSIX)
					if (posix_locks_deadlock(caller->fl_owner, fl->fl_owner))
						return (-EDEADLOCK);
				interruptible_sleep_on(&fl->fl_wait);
				if (current->signal & ~current->blocked)
					return (-ERESTARTSYS);
				goto repeat;
			}
			return (-EAGAIN);
  		}
  	}
	/*
	 * Find the first old lock with the same owner as the new lock.
	 */
	
	before = &filp->f_inode->i_flock;

	/* First skip FLOCK locks and locks owned by other processes.
	 */
	while ((fl = *before) && ((fl->fl_flags == F_FLOCK) ||
				  (caller->fl_owner != fl->fl_owner))) {
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
			goto next_lock;
		}
		/* Processing for different lock types is a bit more complex.
		 */
		if (fl->fl_end < caller->fl_start)
			goto next_lock;
		if (fl->fl_start > caller->fl_end)
			break;
		if (caller->fl_type == F_UNLCK)
			added = 1;
		if (fl->fl_start < caller->fl_start)
			left = fl;
		/* If the next lock in the list has a higher end address than
		 * the new one, insert the new one here.
		 */
		if (fl->fl_end > caller->fl_end) {
			right = fl;
			break;
		}
		if (fl->fl_start >= caller->fl_start) {
			/* The new lock completely replaces an old one (This may
			 * happen several times).
			 */
			if (added) {
				locks_delete_lock(before, 0);
				continue;
			}
			/* Replace the old lock with the new one. Wake up
			 * anybody waiting for the old one, as the change in
			 * lock type might satisfy his needs.
			 */
			wake_up(&fl->fl_wait);
			fl->fl_start = caller->fl_start;
			fl->fl_end = caller->fl_end;
			fl->fl_type = caller->fl_type;
			caller = fl;
			added = 1;
		}
		/* Go on to next lock.
		 */
	next_lock:
		before = &(*before)->fl_next;
	}

	/* FIXME:
	 * Note: We may sleep in locks_alloc_lock(), so
	 * the 'before' pointer may be not valid any more.
	 * This can cause random kernel memory corruption.
	 * It seems the right way is to alloc two locks
	 * at the begining of this func, and then free them
	 * if they were not needed.
	 * Another way is to change GFP_KERNEL to GFP_ATOMIC
	 * in locks_alloc_lock() for this case.
	 * 
	 * Dmitry Gorodchanin 09/02/96.
	 */ 
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
	}
	if (left)
		left->fl_end = caller->fl_start - 1;
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
					       GFP_KERNEL)) == NULL)
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
 * First remove our lock from the lock lists. Then remove all the blocked locks
 * from our blocked list, waking up the processes that own them. If told to wait,
 * then sleep on each of these lock's wait queues. Each blocked process will wake
 * up and immediately wake up its own wait queue allowing us to be scheduled again.
 * Lastly, wake up our own wait queue before freeing the file_lock structure.
 */

static void locks_delete_lock(struct file_lock **fl_p, unsigned int wait)
{
	struct file_lock *fl;
	struct file_lock *bfl;
	
	fl = *fl_p;
	*fl_p = (*fl_p)->fl_next;

	if (fl->fl_nextlink != NULL)
		fl->fl_nextlink->fl_prevlink = fl->fl_prevlink;

	if (fl->fl_prevlink != NULL)
		fl->fl_prevlink->fl_nextlink = fl->fl_nextlink;
	else {
		file_lock_table = fl->fl_nextlink;
	}
	
	while ((bfl = fl->fl_block) != NULL) {
		fl->fl_block = bfl->fl_block;
		bfl->fl_block = NULL;
		wake_up(&bfl->fl_wait);
		if (wait)
			sleep_on(&bfl->fl_wait);
	}

	wake_up(&fl->fl_wait);
	kfree(fl);

	return;
}

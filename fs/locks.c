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
 *  Implemented two lock personalities - FL_FLOCK and FL_POSIX.
 *
 *  FL_POSIX locks are created with calls to fcntl() and lockf() through the
 *  fcntl() system call. They have the semantics described above.
 *
 *  FL_FLOCK locks are created with calls to flock(), through the flock()
 *  system call, which is new. Old C libraries implement flock() via fcntl()
 *  and will continue to use the old, broken implementation.
 *
 *  FL_FLOCK locks follow the 4.4 BSD flock() semantics. They are associated
 *  with a file pointer (filp). As a result they can be shared by a parent
 *  process and its children after a fork(). They are removed when the last
 *  file descriptor referring to the file pointer is closed (unless explicitly
 *  unlocked). 
 *
 *  FL_FLOCK locks never deadlock, an existing lock is always removed before
 *  upgrading from shared to exclusive (or vice versa). When this happens
 *  any processes blocked by the current lock are woken up and allowed to
 *  run before the new lock is applied.
 *  Andy Walker (andy@lysaker.kvaerner.no), June 09, 1995
 *
 *  Removed some race conditions in flock_lock_file(), marked other possible
 *  races. Just grep for FIXME to see them. 
 *  Dmitry Gorodchanin (pgmdsg@ibi.com), February 09, 1996.
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
 *  Allow only one type of locking scheme (FL_POSIX or FL_FLOCK) to be in use
 *  for a given file at a time. Changed the CONFIG_LOCK_MANDATORY scheme to
 *  guarantee sensible behaviour in the case where file system modules might
 *  be compiled with different options than the kernel itself.
 *  Andy Walker (andy@lysaker.kvaerner.no), May 15, 1996.
 *
 *  Added a couple of missing wake_up() calls. Thanks to Thomas Meckel
 *  (Thomas.Meckel@mni.fh-giessen.de) for spotting this.
 *  Andy Walker (andy@lysaker.kvaerner.no), May 15, 1996.
 *
 *  Changed FL_POSIX locks to use the block list in the same way as FL_FLOCK
 *  locks. Changed process synchronisation to avoid dereferencing locks that
 *  have already been freed.
 *  Andy Walker (andy@lysaker.kvaerner.no), Sep 21, 1996.
 *
 *  Made the block list a circular list to minimise searching in the list.
 *  Andy Walker (andy@lysaker.kvaerner.no), Sep 25, 1996.
 *
 *  Made mandatory locking a mount option. Default is not to allow mandatory
 *  locking.
 *  Andy Walker (andy@lysaker.kvaerner.no), Oct 04, 1996.
 *
 *  Some adaptations for NFS support.
 *  Olaf Kirch (okir@monad.swb.de), Dec 1996,
 *
 *  Fixed /proc/locks interface so that we can't overrun the buffer we are handed.
 *  Andy Walker (andy@lysaker.kvaerner.no), May 12, 1997.
 */

#include <linux/malloc.h>
#include <linux/file.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

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
static int posix_locks_deadlock(struct file_lock *caller,
				struct file_lock *blocker);

static struct file_lock *locks_empty_lock(void);
static struct file_lock *locks_init_lock(struct file_lock *,
					 struct file_lock *);
static void locks_insert_lock(struct file_lock **pos, struct file_lock *fl);
static void locks_delete_lock(struct file_lock **thisfl_p, unsigned int wait);
static char *lock_get_status(struct file_lock *fl, int id, char *pfx);

static void locks_insert_block(struct file_lock *blocker, struct file_lock *waiter);
static void locks_delete_block(struct file_lock *blocker, struct file_lock *waiter);
static void locks_wake_up_blocks(struct file_lock *blocker, unsigned int wait);

struct file_lock *file_lock_table = NULL;

/* Allocate a new lock, and initialize its fields from fl.
 * The lock is not inserted into any lists until locks_insert_lock() or 
 * locks_insert_block() are called.
 */
static inline struct file_lock *locks_alloc_lock(struct file_lock *fl)
{
	return locks_init_lock(locks_empty_lock(), fl);
}

/* Free lock not inserted in any queue.
 */
static inline void locks_free_lock(struct file_lock *fl)
{
	if (waitqueue_active(&fl->fl_wait))
		panic("Attempting to free lock with active wait queue");

	if (fl->fl_nextblock != NULL || fl->fl_prevblock != NULL)
		panic("Attempting to free lock with active block list");
		
	kfree(fl);
	return;
}

/* Check if two locks overlap each other.
 */
static inline int locks_overlap(struct file_lock *fl1, struct file_lock *fl2)
{
	return ((fl1->fl_end >= fl2->fl_start) &&
		(fl2->fl_end >= fl1->fl_start));
}

/*
 * Check whether two locks have the same owner
 * N.B. Do we need the test on PID as well as owner?
 * (Clone tasks should be considered as one "owner".)
 */
static inline int
locks_same_owner(struct file_lock *fl1, struct file_lock *fl2)
{
	return (fl1->fl_owner == fl2->fl_owner) &&
	       (fl1->fl_pid   == fl2->fl_pid);
}

/* Insert waiter into blocker's block list.
 * We use a circular list so that processes can be easily woken up in
 * the order they blocked. The documentation doesn't require this but
 * it seems like the reasonable thing to do.
 */
static void locks_insert_block(struct file_lock *blocker, 
			       struct file_lock *waiter)
{
	struct file_lock *prevblock;

	if (blocker->fl_prevblock == NULL)
		/* No previous waiters - list is empty */
		prevblock = blocker;
	else
		/* Previous waiters exist - add to end of list */
		prevblock = blocker->fl_prevblock;

	prevblock->fl_nextblock = waiter;
	blocker->fl_prevblock = waiter;
	waiter->fl_nextblock = blocker;
	waiter->fl_prevblock = prevblock;
	
	return;
}

/* Remove waiter from blocker's block list.
 * When blocker ends up pointing to itself then the list is empty.
 */
static void locks_delete_block(struct file_lock *blocker,
			       struct file_lock *waiter)
{
	struct file_lock *nextblock;
	struct file_lock *prevblock;
	
	nextblock = waiter->fl_nextblock;
	prevblock = waiter->fl_prevblock;

	if (nextblock == NULL)
		return;
	
	nextblock->fl_prevblock = prevblock;
	prevblock->fl_nextblock = nextblock;

	waiter->fl_prevblock = waiter->fl_nextblock = NULL;
	if (blocker->fl_nextblock == blocker)
		/* No more locks on blocker's blocked list */
		blocker->fl_prevblock = blocker->fl_nextblock = NULL;
	return;
}

/* The following two are for the benefit of lockd.
 */
void
posix_block_lock(struct file_lock *blocker, struct file_lock *waiter)
{
	locks_insert_block(blocker, waiter);
	return;
}

void
posix_unblock_lock(struct file_lock *waiter)
{
	if (waiter->fl_prevblock)
		locks_delete_block(waiter->fl_prevblock, waiter);
	return;
}

/* Wake up processes blocked waiting for blocker.
 * If told to wait then schedule the processes until the block list
 * is empty, otherwise empty the block list ourselves.
 */
static void locks_wake_up_blocks(struct file_lock *blocker, unsigned int wait)
{
	struct file_lock *waiter;

	while ((waiter = blocker->fl_nextblock) != NULL) {
		/* N.B. Is it possible for the notify function to block?? */
		if (waiter->fl_notify)
			waiter->fl_notify(waiter);
		wake_up(&waiter->fl_wait);
		if (wait)
			/* Let the blocked process remove waiter from the
			 * block list when it gets scheduled.
			 */
			schedule();
		else
			/* Remove waiter from the block list, because by the
			 * time it wakes up blocker won't exist any more.
			 */
			locks_delete_block(blocker, waiter);
	}
	return;
}

/* flock() system call entry point. Apply a FL_FLOCK style lock to
 * an open file descriptor.
 */
asmlinkage int sys_flock(unsigned int fd, unsigned int cmd)
{
	struct file_lock file_lock;
	struct file *filp;
	int error;

	lock_kernel();
	error = -EBADF;
	filp = fget(fd);
	if (!filp)
		goto out;
	error = -EINVAL;
	if (!flock_make_lock(filp, &file_lock, cmd))
		goto out_putf;
	error = -EBADF;
	if ((file_lock.fl_type != F_UNLCK) && !(filp->f_mode & 3))
		goto out_putf;
	error = flock_lock_file(filp, &file_lock,
				(cmd & (LOCK_UN | LOCK_NB)) ? 0 : 1);
out_putf:
	fput(filp);
out:
	unlock_kernel();
	return (error);
}

/* Report the first existing lock that would conflict with l.
 * This implements the F_GETLK command of fcntl().
 */
int fcntl_getlk(unsigned int fd, struct flock *l)
{
	struct file *filp;
	struct file_lock *fl,file_lock;
	struct flock flock;
	int error;

	error = -EFAULT;
	if (copy_from_user(&flock, l, sizeof(flock)))
		goto out;
	error = -EINVAL;
	if ((flock.l_type != F_RDLCK) && (flock.l_type != F_WRLCK))
		goto out;

	error = -EBADF;
	filp = fget(fd);
	if (!filp)
		goto out;

	error = -EINVAL;
	if (!filp->f_dentry || !filp->f_dentry->d_inode)
		goto out_putf;

	if (!posix_make_lock(filp, &file_lock, &flock))
		goto out_putf;

	if (filp->f_op->lock) {
		error = filp->f_op->lock(filp, F_GETLK, &file_lock);
		if (error < 0)
			goto out_putf;
		fl = &file_lock;
	} else {
		fl = posix_test_lock(filp, &file_lock);
	}
 
	flock.l_type = F_UNLCK;
	if (fl != NULL) {
		flock.l_pid = fl->fl_pid;
		flock.l_start = fl->fl_start;
		flock.l_len = fl->fl_end == OFFSET_MAX ? 0 :
			fl->fl_end - fl->fl_start + 1;
		flock.l_whence = 0;
		flock.l_type = fl->fl_type;
	}
	error = -EFAULT;
	if (!copy_to_user(l, &flock, sizeof(flock)))
		error = 0;
  
out_putf:
	fput(filp);
out:
	return error;
}

/* Apply the lock described by l to an open file descriptor.
 * This implements both the F_SETLK and F_SETLKW commands of fcntl().
 */
int fcntl_setlk(unsigned int fd, unsigned int cmd, struct flock *l)
{
	struct file *filp;
	struct file_lock file_lock;
	struct flock flock;
	struct dentry * dentry;
	struct inode *inode;
	int error;

	/*
	 * This might block, so we do it before checking the inode.
	 */
	error = -EFAULT;
	if (copy_from_user(&flock, l, sizeof(flock)))
		goto out;

	/* Get arguments and validate them ...
	 */

	error = -EBADF;
	filp = fget(fd);
	if (!filp)
		goto out;

	error = -EINVAL;
	if (!(dentry = filp->f_dentry))
		goto out_putf;
	if (!(inode = dentry->d_inode))
		goto out_putf;

	/* Don't allow mandatory locks on files that may be memory mapped
	 * and shared.
	 */
	if (IS_MANDLOCK(inode) &&
	    (inode->i_mode & (S_ISGID | S_IXGRP)) == S_ISGID &&
	    inode->i_mmap) {
		struct vm_area_struct *vma = inode->i_mmap;
		error = -EAGAIN;
		do {
			if (vma->vm_flags & VM_MAYSHARE)
				goto out_putf;
		} while ((vma = vma->vm_next_share) != NULL);
	}

	error = -EINVAL;
	if (!posix_make_lock(filp, &file_lock, &flock))
		goto out_putf;
	
	error = -EBADF;
	switch (flock.l_type) {
	case F_RDLCK:
		if (!(filp->f_mode & FMODE_READ))
			goto out_putf;
		break;
	case F_WRLCK:
		if (!(filp->f_mode & FMODE_WRITE))
			goto out_putf;
		break;
	case F_UNLCK:
		break;
	case F_SHLCK:
	case F_EXLCK:
#ifdef __sparc__
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
		if (!(filp->f_mode & 3))
			goto out_putf;
		break;
#endif
	default:
		error = -EINVAL;
		goto out_putf;
	}

	if (filp->f_op->lock != NULL) {
		error = filp->f_op->lock(filp, cmd, &file_lock);
		if (error < 0)
			goto out_putf;
	}
	error = posix_lock_file(filp, &file_lock, cmd == F_SETLKW);

out_putf:
	fput(filp);
out:
	return error;
}

/*
 * This function is called when the file is being removed
 * from the task's fd array.
 */
void locks_remove_posix(struct file *filp, fl_owner_t owner)
{
	struct inode * inode = filp->f_dentry->d_inode;
	struct file_lock file_lock, *fl;
	struct file_lock **before;

	/*
	 * For POSIX locks we free all locks on this file for the given task.
	 */
repeat:
	before = &inode->i_flock;
	while ((fl = *before) != NULL) {
		if ((fl->fl_flags & FL_POSIX) && fl->fl_owner == owner) {
			int (*lock)(struct file *, int, struct file_lock *);
			lock = filp->f_op->lock;
			if (lock) {
				file_lock = *fl;
				file_lock.fl_type = F_UNLCK;
			}
			locks_delete_lock(before, 0);
			if (lock) {
				lock(filp, F_SETLK, &file_lock);
				/* List may have changed: */
				goto repeat;
			}
			continue;
		}
		before = &fl->fl_next;
	}
}

/*
 * This function is called on the last close of an open file.
 */
void locks_remove_flock(struct file *filp)
{
	struct inode * inode = filp->f_dentry->d_inode; 
	struct file_lock file_lock, *fl;
	struct file_lock **before;

repeat:
	before = &inode->i_flock;
	while ((fl = *before) != NULL) {
		if ((fl->fl_flags & FL_FLOCK) && fl->fl_file == filp) {
			int (*lock)(struct file *, int, struct file_lock *);
			lock = filp->f_op->lock;
			if (lock) {
				file_lock = *fl;
				file_lock.fl_type = F_UNLCK;
			}
			locks_delete_lock(before, 0);
			if (lock) {
				lock(filp, F_SETLK, &file_lock);
				/* List may have changed: */
				goto repeat;
			}
			continue;
		}
		before = &fl->fl_next;
	}
}

struct file_lock *
posix_test_lock(struct file *filp, struct file_lock *fl)
{
	struct file_lock *cfl;

	for (cfl = filp->f_dentry->d_inode->i_flock; cfl; cfl = cfl->fl_next) {
		if (!(cfl->fl_flags & FL_POSIX))
			continue;
		if (posix_locks_conflict(cfl, fl))
			break;
	}

	return (cfl);
}

int locks_verify_locked(struct inode *inode)
{
	/* Candidates for mandatory locking have the setgid bit set
	 * but no group execute bit -  an otherwise meaningless combination.
	 */
	if (IS_MANDLOCK(inode) &&
	    (inode->i_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return (locks_mandatory_locked(inode));
	return (0);
}

int locks_verify_area(int read_write, struct inode *inode, struct file *filp,
		      loff_t offset, size_t count)
{
	/* Candidates for mandatory locking have the setgid bit set
	 * but no group execute bit -  an otherwise meaningless combination.
	 */
	if (IS_MANDLOCK(inode) &&
	    (inode->i_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return (locks_mandatory_area(read_write, inode, filp, offset,
					     count));
	return (0);
}

int locks_mandatory_locked(struct inode *inode)
{
	fl_owner_t owner = current->files;
	struct file_lock *fl;

	/* Search the lock list for this inode for any POSIX locks.
	 */
	for (fl = inode->i_flock; fl != NULL; fl = fl->fl_next) {
		if (!(fl->fl_flags & FL_POSIX))
			continue;
		if (fl->fl_owner != owner)
			return (-EAGAIN);
	}
	return (0);
}

int locks_mandatory_area(int read_write, struct inode *inode,
			 struct file *filp, loff_t offset,
			 size_t count)
{
	struct file_lock *fl;
	struct file_lock tfl;

	memset(&tfl, 0, sizeof(tfl));

	tfl.fl_file = filp;
	tfl.fl_flags = FL_POSIX | FL_ACCESS;
	tfl.fl_owner = current->files;
	tfl.fl_pid = current->pid;
	tfl.fl_type = (read_write == FLOCK_VERIFY_WRITE) ? F_WRLCK : F_RDLCK;
	tfl.fl_start = offset;
	tfl.fl_end = offset + count - 1;

repeat:
	/* Search the lock list for this inode for locks that conflict with
	 * the proposed read/write.
	 */
	for (fl = inode->i_flock; fl != NULL; fl = fl->fl_next) {
		if (!(fl->fl_flags & FL_POSIX))
			continue;
		/* Block for writes against a "read" lock,
		 * and both reads and writes against a "write" lock.
		 */
		if (posix_locks_conflict(fl, &tfl)) {
			if (filp && (filp->f_flags & O_NONBLOCK))
				return (-EAGAIN);
			if (signal_pending(current))
				return (-ERESTARTSYS);
			if (posix_locks_deadlock(&tfl, fl))
				return (-EDEADLK);

			locks_insert_block(fl, &tfl);
			interruptible_sleep_on(&tfl.fl_wait);
			locks_delete_block(fl, &tfl);

			if (signal_pending(current))
				return (-ERESTARTSYS);
			/* If we've been sleeping someone might have
			 * changed the permissions behind our back.
			 */
			if ((inode->i_mode & (S_ISGID | S_IXGRP)) != S_ISGID)
				break;
			goto repeat;
		}
	}
	return (0);
}

/* Verify a "struct flock" and copy it to a "struct file_lock" as a POSIX
 * style lock.
 */
static int posix_make_lock(struct file *filp, struct file_lock *fl,
			   struct flock *l)
{
	off_t start;

	memset(fl, 0, sizeof(*fl));
	
	fl->fl_flags = FL_POSIX;

	switch (l->l_type) {
	case F_RDLCK:
	case F_WRLCK:
	case F_UNLCK:
		fl->fl_type = l->l_type;
		break;
	default:
		return (0);
	}

	switch (l->l_whence) {
	case 0: /*SEEK_SET*/
		start = 0;
		break;
	case 1: /*SEEK_CUR*/
		start = filp->f_pos;
		break;
	case 2: /*SEEK_END*/
		start = filp->f_dentry->d_inode->i_size;
		break;
	default:
		return (0);
	}

	if (((start += l->l_start) < 0) || (l->l_len < 0))
		return (0);
	fl->fl_start = start;	/* we record the absolute position */
	if ((l->l_len == 0) || ((fl->fl_end = start + l->l_len - 1) < 0))
		fl->fl_end = OFFSET_MAX;
	
	fl->fl_file = filp;
	fl->fl_owner = current->files;
	fl->fl_pid = current->pid;

	return (1);
}

/* Verify a call to flock() and fill in a file_lock structure with
 * an appropriate FLOCK lock.
 */
static int flock_make_lock(struct file *filp, struct file_lock *fl,
			   unsigned int cmd)
{
	memset(fl, 0, sizeof(*fl));

	if (!filp->f_dentry)	/* just in case */
		return (0);

	switch (cmd & ~LOCK_NB) {
	case LOCK_SH:
		fl->fl_type = F_RDLCK;
		break;
	case LOCK_EX:
		fl->fl_type = F_WRLCK;
		break;
	case LOCK_UN:
		fl->fl_type = F_UNLCK;
		break;
	default:
		return (0);
	}

	fl->fl_flags = FL_FLOCK;
	fl->fl_start = 0;
	fl->fl_end = OFFSET_MAX;
	fl->fl_file = filp;
	fl->fl_owner = NULL;
	
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
	if (!(sys_fl->fl_flags & FL_POSIX) ||
	    locks_same_owner(caller_fl, sys_fl))
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
	if (!(sys_fl->fl_flags & FL_FLOCK) ||
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
	case F_RDLCK:
		return (sys_fl->fl_type == F_WRLCK);
		
	case F_WRLCK:
		return (1);

	default:
		printk("locks_conflict(): impossible lock type - %d\n",
		       caller_fl->fl_type);
		break;
	}
	return (0);	/* This should never happen */
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
 *
 * Note: the above assumption may not be true when handling lock requests
 * from a broken NFS client. But broken NFS clients have a lot more to
 * worry about than proper deadlock detection anyway... --okir
 */
static int posix_locks_deadlock(struct file_lock *caller_fl,
				struct file_lock *block_fl)
{
	struct file_lock *fl;
	struct file_lock *bfl;
	void		 *caller_owner, *blocked_owner;
	unsigned int	 caller_pid, blocked_pid;

	caller_owner = caller_fl->fl_owner;
	caller_pid = caller_fl->fl_pid;
	blocked_owner = block_fl->fl_owner;
	blocked_pid = block_fl->fl_pid;

next_task:
	if (caller_owner == blocked_owner && caller_pid == blocked_pid)
		return (1);
	for (fl = file_lock_table; fl != NULL; fl = fl->fl_nextlink) {
		if (fl->fl_owner == NULL || fl->fl_nextblock == NULL)
			continue;
		for (bfl = fl->fl_nextblock; bfl != fl; bfl = bfl->fl_nextblock) {
			if (bfl->fl_owner == blocked_owner &&
			    bfl->fl_pid == blocked_pid) {
				if (fl->fl_owner == caller_owner &&
				    fl->fl_pid == caller_pid) {
					return (1);
				}
				blocked_owner = fl->fl_owner;
				blocked_pid = fl->fl_pid;
				goto next_task;
			}
		}
	}
	return (0);
}

/* Try to create a FLOCK lock on filp. We always insert new FLOCK locks at
 * the head of the list, but that's secret knowledge known only to the next
 * two functions.
 */
static int flock_lock_file(struct file *filp, struct file_lock *caller,
			   unsigned int wait)
{
	struct file_lock *fl;
	struct file_lock *new_fl = NULL;
	struct file_lock **before;
	struct inode * inode = filp->f_dentry->d_inode;
	int error, change;
	int unlock = (caller->fl_type == F_UNLCK);

	/*
	 * If we need a new lock, get it in advance to avoid races.
	 */
	if (!unlock) {
		error = -ENOLCK;
		new_fl = locks_alloc_lock(caller);
		if (!new_fl)
			goto out;
	}

	error = 0;
search:
	change = 0;
	before = &inode->i_flock;
	while (((fl = *before) != NULL) && (fl->fl_flags & FL_FLOCK)) {
		if (caller->fl_file == fl->fl_file) {
			if (caller->fl_type == fl->fl_type)
				goto out;
			change = 1;
			break;
		}
		before = &fl->fl_next;
	}
	/* change means that we are changing the type of an existing lock, or
	 * or else unlocking it.
	 */
	if (change) {
		/* N.B. What if the wait argument is false? */
		locks_delete_lock(before, !unlock);
		/*
		 * If we waited, another lock may have been added ...
		 */
		if (!unlock)
			goto search;
	}
	if (unlock)
		goto out;

repeat:
	/* Check signals each time we start */
	error = -ERESTARTSYS;
	if (signal_pending(current))
		goto out;
	for (fl = inode->i_flock; (fl != NULL) && (fl->fl_flags & FL_FLOCK);
	     fl = fl->fl_next) {
		if (!flock_locks_conflict(new_fl, fl))
			continue;
		error = -EAGAIN;
		if (!wait)
			goto out;
		locks_insert_block(fl, new_fl);
		interruptible_sleep_on(&new_fl->fl_wait);
		locks_delete_block(fl, new_fl);
		goto repeat;
	}
	locks_insert_lock(&inode->i_flock, new_fl);
	new_fl = NULL;
	error = 0;

out:
	if (new_fl)
		locks_free_lock(new_fl);
	return error;
}

/* Add a POSIX style lock to a file.
 * We merge adjacent locks whenever possible. POSIX locks are sorted by owner
 * task, then by starting address
 *
 * Kai Petzke writes:
 * To make freeing a lock much faster, we keep a pointer to the lock before the
 * actual one. But the real gain of the new coding was, that lock_it() and
 * unlock_it() became one function.
 *
 * To all purists: Yes, I use a few goto's. Just pass on to the next function.
 */

int posix_lock_file(struct file *filp, struct file_lock *caller,
			   unsigned int wait)
{
	struct file_lock *fl;
	struct file_lock *new_fl, *new_fl2;
	struct file_lock *left = NULL;
	struct file_lock *right = NULL;
	struct file_lock **before;
	struct inode * inode = filp->f_dentry->d_inode;
	int error, added = 0;

	/*
	 * We may need two file_lock structures for this operation,
	 * so we get them in advance to avoid races.
	 */
	new_fl  = locks_empty_lock();
	new_fl2 = locks_empty_lock();
	error = -ENOLCK; /* "no luck" */
	if (!(new_fl && new_fl2))
		goto out;

	if (caller->fl_type != F_UNLCK) {
  repeat:
		for (fl = inode->i_flock; fl != NULL; fl = fl->fl_next) {
			if (!(fl->fl_flags & FL_POSIX))
				continue;
			if (!posix_locks_conflict(caller, fl))
				continue;
			error = -EAGAIN;
			if (!wait)
				goto out;
			error = -EDEADLK;
			if (posix_locks_deadlock(caller, fl))
				goto out;
			error = -ERESTARTSYS;
			if (signal_pending(current))
				goto out;
			locks_insert_block(fl, caller);
			interruptible_sleep_on(&caller->fl_wait);
			locks_delete_block(fl, caller);
			goto repeat;
  		}
  	}

	/*
	 * We've allocated the new locks in advance, so there are no
	 * errors possible (and no blocking operations) from here on.
	 * 
	 * Find the first old lock with the same owner as the new lock.
	 */
	
	before = &inode->i_flock;

	/* First skip locks owned by other processes.
	 */
	while ((fl = *before) && (!(fl->fl_flags & FL_POSIX) ||
				  !locks_same_owner(caller, fl))) {
		before = &fl->fl_next;
	}

	/* Process locks with this owner.
	 */
	while ((fl = *before) && locks_same_owner(caller, fl)) {
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
				locks_wake_up_blocks(fl, 0);
				fl->fl_start = caller->fl_start;
				fl->fl_end = caller->fl_end;
				fl->fl_type = caller->fl_type;
				fl->fl_u = caller->fl_u;
				caller = fl;
				added = 1;
			}
		}
		/* Go on to next lock.
		 */
	next_lock:
		before = &fl->fl_next;
	}

	error = 0;
	if (!added) {
		if (caller->fl_type == F_UNLCK)
			goto out;
		locks_init_lock(new_fl, caller);
		locks_insert_lock(before, new_fl);
		new_fl = NULL;
	}
	if (right) {
		if (left == right) {
			/* The new lock breaks the old one in two pieces,
			 * so we have to use the second new lock (in this
			 * case, even F_UNLCK may fail!).
			 */
			left = locks_init_lock(new_fl2, right);
			locks_insert_lock(before, left);
			new_fl2 = NULL;
		}
		right->fl_start = caller->fl_end + 1;
		locks_wake_up_blocks(right, 0);
	}
	if (left) {
		left->fl_end = caller->fl_start - 1;
		locks_wake_up_blocks(left, 0);
	}
out:
	/*
	 * Free any unused locks.  (They haven't
	 * ever been used, so we use kfree().)
	 */
	if (new_fl)
		kfree(new_fl);
	if (new_fl2)
		kfree(new_fl2);
	return error;
}

/*
 * Allocate an empty lock structure. We can use GFP_KERNEL now that
 * all allocations are done in advance.
 */
static struct file_lock *locks_empty_lock(void)
{
	/* Okay, let's make a new file_lock structure... */
	return ((struct file_lock *) kmalloc(sizeof(struct file_lock),
						GFP_KERNEL));
}

/*
 * Initialize a new lock from an existing file_lock structure.
 */
static struct file_lock *locks_init_lock(struct file_lock *new,
					 struct file_lock *fl)
{
	if (new) {
		memset(new, 0, sizeof(*new));
		new->fl_owner = fl->fl_owner;
		new->fl_pid = fl->fl_pid;
		new->fl_file = fl->fl_file;
		new->fl_flags = fl->fl_flags;
		new->fl_type = fl->fl_type;
		new->fl_start = fl->fl_start;
		new->fl_end = fl->fl_end;
		new->fl_notify = fl->fl_notify;
		new->fl_u = fl->fl_u;
	}
	return new;
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
 * First remove our lock from the active lock lists. Then call
 * locks_wake_up_blocks() to wake up processes that are blocked
 * waiting for this lock. Finally free the lock structure.
 */
static void locks_delete_lock(struct file_lock **thisfl_p, unsigned int wait)
{
	struct file_lock *thisfl;
	struct file_lock *prevfl;
	struct file_lock *nextfl;
	
	thisfl = *thisfl_p;
	*thisfl_p = thisfl->fl_next;

	prevfl = thisfl->fl_prevlink;
	nextfl = thisfl->fl_nextlink;

	if (nextfl != NULL)
		nextfl->fl_prevlink = prevfl;

	if (prevfl != NULL)
		prevfl->fl_nextlink = nextfl;
	else
		file_lock_table = nextfl;
	
	locks_wake_up_blocks(thisfl, wait);
	locks_free_lock(thisfl);

	return;
}


static char *lock_get_status(struct file_lock *fl, int id, char *pfx)
{
	static char temp[129];
	char *p = temp;
	struct inode *inode;

	inode = fl->fl_file->f_dentry->d_inode;

	p += sprintf(p, "%d:%s ", id, pfx);
	if (fl->fl_flags & FL_POSIX) {
		p += sprintf(p, "%6s %s ",
			     (fl->fl_flags & FL_ACCESS) ? "ACCESS" : "POSIX ",
			     (IS_MANDLOCK(inode) &&
			      (inode->i_mode & (S_IXGRP | S_ISGID)) == S_ISGID) ?
			     "MANDATORY" : "ADVISORY ");
	}
	else {
		p += sprintf(p, "FLOCK  ADVISORY  ");
	}
	p += sprintf(p, "%s ", (fl->fl_type == F_RDLCK) ? "READ " : "WRITE");
	p += sprintf(p, "%d %s:%ld %ld %ld ",
		     fl->fl_pid,
		     kdevname(inode->i_dev), inode->i_ino, fl->fl_start,
		     fl->fl_end);
	sprintf(p, "%08lx %08lx %08lx %08lx %08lx\n",
		(long)fl, (long)fl->fl_prevlink, (long)fl->fl_nextlink,
		(long)fl->fl_next, (long)fl->fl_nextblock);
	return (temp);
}

static inline int copy_lock_status(char *p, char **q, off_t pos, int len,
				   off_t offset, off_t length)
{
	off_t i;

	i = pos - offset;
	if (i > 0) {
		if (i >= length) {
			i = len + length - i;
			memcpy(*q, p, i);
			*q += i;
			return (0);
		}
		if (i < len) {
			p += len - i;
		}
		else
			i = len;
		memcpy(*q, p, i);
		*q += i;
	}
	
	return (1);
}

int get_locks_status(char *buffer, char **start, off_t offset, off_t length)
{
	struct file_lock *fl;
	struct file_lock *bfl;
	char *p;
	char *q = buffer;
	off_t i, len, pos = 0;

	for (fl = file_lock_table, i = 1; fl != NULL; fl = fl->fl_nextlink, i++) {
		p = lock_get_status(fl, i, "");
		len = strlen(p);
		pos += len;
		if (!copy_lock_status(p, &q, pos, len, offset, length))
			goto done;
		if ((bfl = fl->fl_nextblock) == NULL)
			continue;
		do {
			p = lock_get_status(bfl, i, " ->");
			len = strlen(p);
			pos += len;
			if (!copy_lock_status(p, &q, pos, len, offset, length))
				goto done;
		} while ((bfl = bfl->fl_nextblock) != fl);
	}
done:
	if (q != buffer)
		*start = buffer;
	return (q - buffer);
}




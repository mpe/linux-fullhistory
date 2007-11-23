/*
 *  linux/fs/fcntl.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/file.h>
#include <linux/smp_lock.h>

#include <asm/poll.h>
#include <asm/siginfo.h>
#include <asm/uaccess.h>

extern int sock_fcntl (struct file *, unsigned int cmd, unsigned long arg);

/*
 * locate_fd finds a free file descriptor in the open_fds fdset,
 * expanding the fd arrays if necessary.  The files write lock will be
 * held on exit to ensure that the fd can be entered atomically.
 */

static inline int locate_fd(struct files_struct *files, 
			    struct file *file, int start)
{
	unsigned int newfd;
	int error;

	write_lock(&files->file_lock);
	
repeat:
	error = -EMFILE;
	if (start < files->next_fd)
		start = files->next_fd;
	if (start >= files->max_fdset) {
	expand:
		error = expand_files(files, start);
		if (error < 0)
			goto out;
		goto repeat;
	}
	
	newfd = find_next_zero_bit(files->open_fds->fds_bits, 
				   files->max_fdset, start);

	error = -EMFILE;
	if (newfd >= current->rlim[RLIMIT_NOFILE].rlim_cur)
		goto out;
	if (newfd >= files->max_fdset) 
		goto expand;

	error = expand_files(files, newfd);
	if (error < 0)
		goto out;
	if (error) /* If we might have blocked, try again. */
		goto repeat;

	if (start <= files->next_fd)
		files->next_fd = newfd + 1;
	
	error = newfd;
	
out:
	return error;
}

static inline void allocate_fd(struct files_struct *files, 
					struct file *file, int fd)
{
	FD_SET(fd, files->open_fds);
	FD_CLR(fd, files->close_on_exec);
	write_unlock(&files->file_lock);
	fd_install(fd, file);
}

static int dupfd(struct file *file, int start)
{
	struct files_struct * files = current->files;
	int ret;

	ret = locate_fd(files, file, start);
	if (ret < 0) 
		goto out_putf;
	allocate_fd(files, file, ret);
	return ret;

out_putf:
	write_unlock(&files->file_lock);
	fput(file);
	return ret;
}

asmlinkage long sys_dup2(unsigned int oldfd, unsigned int newfd)
{
	int err = -EBADF;
	struct file * file;
	struct files_struct * files = current->files;

	write_lock(&current->files->file_lock);
	if (!(file = fcheck(oldfd)))
		goto out_unlock;
	err = newfd;
	if (newfd == oldfd)
		goto out_unlock;
	err = -EBADF;
	if (newfd >= NR_OPEN)
		goto out_unlock;	/* following POSIX.1 6.2.1 */
	get_file(file);			/* We are now finished with oldfd */

	err = expand_files(files, newfd);
	if (err < 0) {
		write_unlock(&files->file_lock);
		fput(file);
		goto out;
	}

	/* To avoid races with open() and dup(), we will mark the fd as
	 * in-use in the open-file bitmap throughout the entire dup2()
	 * process.  This is quite safe: do_close() uses the fd array
	 * entry, not the bitmap, to decide what work needs to be
	 * done.  --sct */
	FD_SET(newfd, files->open_fds);
	write_unlock(&files->file_lock);
	
	do_close(newfd, 0);

	write_lock(&files->file_lock);
	allocate_fd(files, file, newfd);
	err = newfd;

out:
	return err;
out_unlock:
	write_unlock(&current->files->file_lock);
	goto out;
}

asmlinkage long sys_dup(unsigned int fildes)
{
	int ret = -EBADF;
	struct file * file = fget(fildes);

	if (file)
		ret = dupfd(file, 0);
	return ret;
}

#define SETFL_MASK (O_APPEND | O_NONBLOCK | O_NDELAY | FASYNC)

static int setfl(int fd, struct file * filp, unsigned long arg)
{
	struct inode * inode = filp->f_dentry->d_inode;

	/*
	 * In the case of an append-only file, O_APPEND
	 * cannot be cleared
	 */
	if (!(arg & O_APPEND) && IS_APPEND(inode))
		return -EPERM;

	/* Did FASYNC state change? */
	if ((arg ^ filp->f_flags) & FASYNC) {
		if (filp->f_op && filp->f_op->fasync)
			filp->f_op->fasync(fd, filp, (arg & FASYNC) != 0);
	}

	/* required for strict SunOS emulation */
	if (O_NONBLOCK != O_NDELAY)
	       if (arg & O_NDELAY)
		   arg |= O_NONBLOCK;

	filp->f_flags = (arg & SETFL_MASK) | (filp->f_flags & ~SETFL_MASK);
	return 0;
}

asmlinkage long sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;
	long err = -EBADF;

	filp = fget(fd);
	if (!filp)
		goto out;
	err = 0;
	lock_kernel();
	switch (cmd) {
		case F_DUPFD:
			err = -EINVAL;
			if (arg < NR_OPEN) {
				get_file(filp);
				err = dupfd(filp, arg);
			}
			break;
		case F_GETFD:
			err = FD_ISSET(fd, current->files->close_on_exec);
			break;
		case F_SETFD:
			if (arg&1)
				FD_SET(fd, current->files->close_on_exec);
			else
				FD_CLR(fd, current->files->close_on_exec);
			break;
		case F_GETFL:
			err = filp->f_flags;
			break;
		case F_SETFL:
			err = setfl(fd, filp, arg);
			break;
		case F_GETLK:
			err = fcntl_getlk(fd, (struct flock *) arg);
			break;
		case F_SETLK:
			err = fcntl_setlk(fd, cmd, (struct flock *) arg);
			break;
		case F_SETLKW:
			err = fcntl_setlk(fd, cmd, (struct flock *) arg);
			break;
		case F_GETOWN:
			/*
			 * XXX If f_owner is a process group, the
			 * negative return value will get converted
			 * into an error.  Oops.  If we keep the
			 * current syscall conventions, the only way
			 * to fix this will be in libc.
			 */
			err = filp->f_owner.pid;
			break;
		case F_SETOWN:
			filp->f_owner.pid = arg;
			filp->f_owner.uid = current->uid;
			filp->f_owner.euid = current->euid;
			if (S_ISSOCK (filp->f_dentry->d_inode->i_mode))
				err = sock_fcntl (filp, F_SETOWN, arg);
			break;
		case F_GETSIG:
			err = filp->f_owner.signum;
			break;
		case F_SETSIG:
			/* arg == 0 restores default behaviour. */
			if (arg < 0 || arg > _NSIG) {
				err = -EINVAL;
				break;
			}
			err = 0;
			filp->f_owner.signum = arg;
			break;
		default:
			/* sockets need a few special fcntls. */
			err = -EINVAL;
			if (S_ISSOCK (filp->f_dentry->d_inode->i_mode))
				err = sock_fcntl (filp, cmd, arg);
			break;
	}
	fput(filp);
	unlock_kernel();
out:
	return err;
}

/* Table to convert sigio signal codes into poll band bitmaps */

static long band_table[NSIGPOLL+1] = {
	~0,
	POLLIN | POLLRDNORM,			/* POLL_IN */
	POLLOUT | POLLWRNORM | POLLWRBAND,	/* POLL_OUT */
	POLLIN | POLLRDNORM | POLLMSG,		/* POLL_MSG */
	POLLERR,				/* POLL_ERR */
	POLLPRI | POLLRDBAND,			/* POLL_PRI */
	POLLHUP | POLLERR			/* POLL_HUP */
};

static void send_sigio_to_task(struct task_struct *p,
			       struct fown_struct *fown, 
			       struct fasync_struct *fa,
			       int reason)
{
	if ((fown->euid != 0) &&
	    (fown->euid ^ p->suid) && (fown->euid ^ p->uid) &&
	    (fown->uid ^ p->suid) && (fown->uid ^ p->uid))
		return;
	switch (fown->signum) {
		siginfo_t si;
		default:
			/* Queue a rt signal with the appropriate fd as its
			   value.  We use SI_SIGIO as the source, not 
			   SI_KERNEL, since kernel signals always get 
			   delivered even if we can't queue.  Failure to
			   queue in this case _should_ be reported; we fall
			   back to SIGIO in that case. --sct */
			si.si_signo = fown->signum;
			si.si_errno = 0;
		        si.si_code  = reason;
			if (reason < 0 || reason > NSIGPOLL)
				si.si_band  = ~0L;
			else
				si.si_band = band_table[reason];
			si.si_fd    = fa->fa_fd;
			if (!send_sig_info(fown->signum, &si, p))
				break;
		/* fall-through: fall back on the old plain SIGIO signal */
		case 0:
			send_sig(SIGIO, p, 1);
	}
}

static void send_sigio(struct fown_struct *fown, struct fasync_struct *fa, 
		       int band)
{
	struct task_struct * p;
	int   pid	= fown->pid;
	
	read_lock(&tasklist_lock);
	if ( (pid > 0) && (p = find_task_by_pid(pid)) ) {
		send_sigio_to_task(p, fown, fa, band);
		goto out;
	}
	for_each_task(p) {
		int match = p->pid;
		if (pid < 0)
			match = -p->pgrp;
		if (pid != match)
			continue;
		send_sigio_to_task(p, fown, fa, band);
	}
out:
	read_unlock(&tasklist_lock);
}

void kill_fasync(struct fasync_struct *fa, int sig, int band)
{
	while (fa) {
		struct fown_struct * fown;
		if (fa->magic != FASYNC_MAGIC) {
			printk("kill_fasync: bad magic number in "
			       "fasync_struct!\n");
			return;
		}
		fown = &fa->fa_file->f_owner;
		/* Don't send SIGURG to processes which have not set a
		   queued signum: SIGURG has its own default signalling
		   mechanism. */
		if (fown->pid && !(sig == SIGURG && fown->signum == 0))
			send_sigio(fown, fa, band);
		fa = fa->fa_next;
	}
}

/*
 *  linux/fs/fcntl.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/file.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

extern int sock_fcntl (struct file *, unsigned int cmd, unsigned long arg);

static inline int dupfd(unsigned int fd, unsigned int arg)
{
	struct files_struct * files = current->files;
	struct file * file;
	int error;

	error = -EINVAL;
	if (arg >= NR_OPEN)
		goto out;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	error = -EMFILE;
	arg = find_next_zero_bit(&files->open_fds, NR_OPEN, arg);
	if (arg >= current->rlim[RLIMIT_NOFILE].rlim_cur)
		goto out_putf;
	FD_SET(arg, &files->open_fds);
	FD_CLR(arg, &files->close_on_exec);
	fd_install(arg, file);
	error = arg;
out:
	return error;

out_putf:
	fput(file);
	goto out;
}

asmlinkage int sys_dup2(unsigned int oldfd, unsigned int newfd)
{
	int err = -EBADF;

	lock_kernel();
	if (!fcheck(oldfd))
		goto out;
	err = newfd;
	if (newfd == oldfd)
		goto out;
	err = -EBADF;
	if (newfd >= NR_OPEN)
		goto out;	/* following POSIX.1 6.2.1 */

	sys_close(newfd);
	err = dupfd(oldfd, newfd);
out:
	unlock_kernel();
	return err;
}

asmlinkage int sys_dup(unsigned int fildes)
{
	int ret;

	lock_kernel();
	ret = dupfd(fildes, 0);
	unlock_kernel();
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

	lock_kernel();
	filp = fget(fd);
	if (!filp)
		goto out;
	err = 0;
	switch (cmd) {
		case F_DUPFD:
			err = dupfd(fd, arg);
			break;
		case F_GETFD:
			err = FD_ISSET(fd, &current->files->close_on_exec);
			break;
		case F_SETFD:
			if (arg&1)
				FD_SET(fd, &current->files->close_on_exec);
			else
				FD_CLR(fd, &current->files->close_on_exec);
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
			err = 0;
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
			if (arg <= 0 || arg > _NSIG) {
				err = -EINVAL;
				break;
			}
			err = 0;
			filp->f_owner.signum = arg;
			break;
		default:
			/* sockets need a few special fcntls. */
			if (S_ISSOCK (filp->f_dentry->d_inode->i_mode))
				err = sock_fcntl (filp, cmd, arg);
			else
				err = -EINVAL;
			break;
	}
	fput(filp);
out:
	unlock_kernel();
	return err;
}

static void send_sigio(struct fown_struct *fown, struct fasync_struct *fa)
{
	struct task_struct * p;
	int   pid	= fown->pid;
	uid_t uid	= fown->uid;
	uid_t euid	= fown->euid;
	
	read_lock(&tasklist_lock);
	for_each_task(p) {
		int match = p->pid;
		if (pid < 0)
			match = -p->pgrp;
		if (pid != match)
			continue;
		if ((euid != 0) &&
		    (euid ^ p->suid) && (euid ^ p->uid) &&
		    (uid ^ p->suid) && (uid ^ p->uid))
			continue;
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
		        si.si_code  = SI_SIGIO;
			si.si_pid   = pid;
			si.si_uid   = uid;
			si.si_fd    = fa->fa_fd;
			if (!send_sig_info(fown->signum, &si, p))
				break;
		/* fall-through: fall back on the old plain SIGIO signal */
		case 0:
			send_sig(SIGIO, p, 1);
		}
	}
	read_unlock(&tasklist_lock);
}

void kill_fasync(struct fasync_struct *fa, int sig)
{
	while (fa) {
		struct fown_struct * fown;
		if (fa->magic != FASYNC_MAGIC) {
			printk("kill_fasync: bad magic number in "
			       "fasync_struct!\n");
			return;
		}
		fown = &fa->fa_file->f_owner;
		if (fown->pid)
			send_sigio(fown, fa);
		fa = fa->fa_next;
	}
}

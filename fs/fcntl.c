/*
 *  linux/fs/fcntl.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/bitops.h>
#include <asm/uaccess.h>

extern int sock_fcntl (struct file *, unsigned int cmd, unsigned long arg);

static inline int dupfd(unsigned int fd, unsigned int arg)
{
	struct files_struct * files = current->files;

	if (fd >= NR_OPEN || !files->fd[fd])
		return -EBADF;
	if (arg >= NR_OPEN)
		return -EINVAL;
	arg = find_next_zero_bit(&files->open_fds, NR_OPEN, arg);
	if (arg >= current->rlim[RLIMIT_NOFILE].rlim_cur)
		return -EMFILE;
	FD_SET(arg, &files->open_fds);
	FD_CLR(arg, &files->close_on_exec);
	(files->fd[arg] = files->fd[fd])->f_count++;
	return arg;
}

asmlinkage int sys_dup2(unsigned int oldfd, unsigned int newfd)
{
	int err = -EBADF;

	lock_kernel();
	if (oldfd >= NR_OPEN || !current->files->fd[oldfd])
		goto out;
	err = newfd;
	if (newfd == oldfd)
		goto out;
	err = -EBADF;
	if (newfd >= NR_OPEN)
		goto out;	/* following POSIX.1 6.2.1 */

	sys_close(newfd);
	err = dupfd(oldfd,newfd);
out:
	unlock_kernel();
	return err;
}

asmlinkage int sys_dup(unsigned int fildes)
{
	int ret;

	lock_kernel();
	ret = dupfd(fildes,0);
	unlock_kernel();
	return ret;
}

#define SETFL_MASK (O_APPEND | O_NONBLOCK | O_NDELAY | FASYNC)

static int setfl(struct file * filp, unsigned long arg)
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
		if (filp->f_op->fasync)
			filp->f_op->fasync(filp, (arg & FASYNC) != 0);
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
	struct task_struct *p;
	int task_found = 0;
	long err = -EBADF;

	lock_kernel();
	if (fd >= NR_OPEN || !(filp = current->files->fd[fd]))
		goto out;
	err = 0;
	switch (cmd) {
		case F_DUPFD:
			err = dupfd(fd,arg);
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
			err = setfl(filp, arg);
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
			 * into an error.  Oops.  If we keep the the
			 * current syscall conventions, the only way
			 * to fix this will be in libc.
			 */
			err = filp->f_owner;
			break;
		case F_SETOWN:
			/*
			 *	Add the security checks - AC. Without
			 *	this there is a massive Linux security
			 *	hole here - consider what happens if
			 *	you do something like
			 * 
			 *		fcntl(0,F_SETOWN,some_root_process);
			 *		getchar();
			 * 
			 *	and input a line!
			 * 
			 * BTW: Don't try this for fun. Several Unix
			 *	systems I tried this on fall for the
			 *	trick!
			 * 
			 * I had to fix this botch job as Linux
			 *	kill_fasync asserts priv making it a
			 *	free all user process killer!
			 *
			 * Changed to make the security checks more
			 * liberal.  -- TYT
			 */
			if (current->pgrp == -arg || current->pid == arg)
				goto fasync_ok;
			
			read_lock(&tasklist_lock);
			for_each_task(p) {
				if ((p->pid == arg) || (p->pid == -arg) || 
				    (p->pgrp == -arg)) {
					task_found++;
					err = -EPERM;
					if ((p->session != current->session) &&
					    (p->uid != current->uid) &&
					    (p->euid != current->euid) &&
					    !suser()) {
						read_unlock(&tasklist_lock);
						goto out;
					}
					break;
				}
			}
			read_unlock(&tasklist_lock);
			err = -EINVAL;
			if ((task_found == 0) && !suser())
				break;
		fasync_ok:
			err = 0;
			filp->f_owner = arg;
			if (S_ISSOCK (filp->f_dentry->d_inode->i_mode))
				err = sock_fcntl (filp, F_SETOWN, arg);
			break;
		default:
			/* sockets need a few special fcntls. */
			if (S_ISSOCK (filp->f_dentry->d_inode->i_mode))
				err = sock_fcntl (filp, cmd, arg);
			else
				err = -EINVAL;
			break;
	}
out:
	unlock_kernel();
	return err;
}

void kill_fasync(struct fasync_struct *fa, int sig)
{
	while (fa) {
		if (fa->magic != FASYNC_MAGIC) {
			printk("kill_fasync: bad magic number in "
			       "fasync_struct!\n");
			return;
		}
		if (fa->fa_file->f_owner > 0)
			kill_proc(fa->fa_file->f_owner, sig, 1);
		else
			kill_pg(-fa->fa_file->f_owner, sig, 1);
		fa = fa->fa_next;
	}
}

/*
 *  linux/fs/fcntl.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <asm/segment.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/string.h>

extern int fcntl_getlk(unsigned int, struct flock *);
extern int fcntl_setlk(unsigned int, unsigned int, struct flock *);
extern int sock_fcntl (struct file *, unsigned int cmd, unsigned long arg);

static int dupfd(unsigned int fd, unsigned int arg)
{
	if (fd >= NR_OPEN || !current->files->fd[fd])
		return -EBADF;
	if (arg >= NR_OPEN)
		return -EINVAL;
	while (arg < NR_OPEN)
		if (current->files->fd[arg])
			arg++;
		else
			break;
	if (arg >= NR_OPEN)
		return -EMFILE;
	FD_CLR(arg, &current->files->close_on_exec);
	(current->files->fd[arg] = current->files->fd[fd])->f_count++;
	return arg;
}

asmlinkage int sys_dup2(unsigned int oldfd, unsigned int newfd)
{
	if (oldfd >= NR_OPEN || !current->files->fd[oldfd])
		return -EBADF;
	if (newfd == oldfd)
		return newfd;
	/*
	 * errno's for dup2() are slightly different than for fcntl(F_DUPFD)
	 * for historical reasons.
	 */
	if (newfd > NR_OPEN)	/* historical botch - should have been >= */
		return -EBADF;	/* dupfd() would return -EINVAL */
#if 1
	if (newfd == NR_OPEN)
		return -EBADF;	/* dupfd() does return -EINVAL and that may
				 * even be the standard!  But that is too
				 * weird for now.
				 */
#endif
	sys_close(newfd);
	return dupfd(oldfd,newfd);
}

asmlinkage int sys_dup(unsigned int fildes)
{
	return dupfd(fildes,0);
}

asmlinkage int sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;
	struct task_struct *p;
	int task_found = 0;

	if (fd >= NR_OPEN || !(filp = current->files->fd[fd]))
		return -EBADF;
	switch (cmd) {
		case F_DUPFD:
			return dupfd(fd,arg);
		case F_GETFD:
			return FD_ISSET(fd, &current->files->close_on_exec);
		case F_SETFD:
			if (arg&1)
				FD_SET(fd, &current->files->close_on_exec);
			else
				FD_CLR(fd, &current->files->close_on_exec);
			return 0;
		case F_GETFL:
			return filp->f_flags;
		case F_SETFL:
			/*
			 * In the case of an append-only file, O_APPEND
			 * cannot be cleared
			 */
			if (IS_APPEND(filp->f_inode) && !(arg & O_APPEND))
				return -EPERM;
			if ((arg & FASYNC) && !(filp->f_flags & FASYNC) &&
			    filp->f_op->fasync)
				filp->f_op->fasync(filp->f_inode, filp, 1);
			if (!(arg & FASYNC) && (filp->f_flags & FASYNC) &&
			    filp->f_op->fasync)
				filp->f_op->fasync(filp->f_inode, filp, 0);
			filp->f_flags &= ~(O_APPEND | O_NONBLOCK | FASYNC);
			filp->f_flags |= arg & (O_APPEND | O_NONBLOCK |
						FASYNC);
			return 0;
		case F_GETLK:
			return fcntl_getlk(fd, (struct flock *) arg);
		case F_SETLK:
			return fcntl_setlk(fd, cmd, (struct flock *) arg);
		case F_SETLKW:
			return fcntl_setlk(fd, cmd, (struct flock *) arg);
		case F_GETOWN:
			/*
			 * XXX If f_owner is a process group, the
			 * negative return value will get converted
			 * into an error.  Oops.  If we keep the the
			 * current syscall conventions, the only way
			 * to fix this will be in libc.
			 */
			return filp->f_owner;
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
			
			for_each_task(p) {
				if ((p->pid == arg) || (p->pid == -arg) || 
				    (p->pgrp == -arg)) {
					task_found++;
					if ((p->session != current->session) &&
					    (p->uid != current->uid) &&
					    (p->euid != current->euid) &&
					    !suser())
						return -EPERM;
					break;
				}
			}
			if ((task_found == 0) && !suser())
				return -EINVAL;
		fasync_ok:
			filp->f_owner = arg;
			if (S_ISSOCK (filp->f_inode->i_mode))
				sock_fcntl (filp, F_SETOWN, arg);
			return 0;
		default:
			/* sockets need a few special fcntls. */
			if (S_ISSOCK (filp->f_inode->i_mode))
			  {
			     return (sock_fcntl (filp, cmd, arg));
			  }
			return -EINVAL;
	}
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

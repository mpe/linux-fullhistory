/*
 *  linux/fs/proc/fd.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  proc fd directory handling functions
 *
 *  01-May-98 Edgar Toernig <froese@gmx.de>
 *	Added support for more than 256 fds.
 *	Limit raised to 32768.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

#include <asm/uaccess.h>

static int proc_readfd(struct file *, void *, filldir_t);
static int proc_lookupfd(struct inode *, struct dentry *);

static struct file_operations proc_fd_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
	proc_readfd,		/* readdir */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* flush */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

/*
 * proc directories can do almost nothing..
 */
struct inode_operations proc_fd_inode_operations = {
	&proc_fd_operations,	/* default base directory file-ops */
	NULL,			/* create */
	proc_lookupfd,		/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	proc_permission		/* permission */
};

/*
 * NOTE! Normally we'd indicate that a file does not
 * exist by creating a negative dentry and returning
 * a successful return code. However, for this case
 * we do not want to create negative dentries, because
 * the state of the world can change behind our backs.
 *
 * Thus just return -ENOENT instead.
 */
static int proc_lookupfd(struct inode * dir, struct dentry * dentry)
{
	unsigned int ino, pid, fd, c;
	struct task_struct * p;
	struct file * file;
	struct inode *inode;
	const char *name;
	int len, err;

	err = -ENOENT;
	if (!dir)
		goto out;
	ino = dir->i_ino;
	pid = ino >> 16;
	ino &= 0x0000ffff;

	if (!pid || ino != PROC_PID_FD || !S_ISDIR(dir->i_mode))
		goto out;

	fd = 0;
	len = dentry->d_name.len;
	name = dentry->d_name.name;
	while (len-- > 0) {
		c = *name - '0';
		name++;
		if (c > 9)
			goto out;
		fd *= 10;
		fd += c;
		if (fd & 0xffff8000)
			goto out;
	}

	read_lock(&tasklist_lock);
	file = NULL;
	p = find_task_by_pid(pid);
	if (p)
		file = fcheck_task(p, fd);
	read_unlock(&tasklist_lock);

	/*
	 *	File handle is invalid if it is out of range, if the process
	 *	has no files (Zombie) if the file is closed, or if its inode
	 *	is NULL
	 */

	if (!file || !file->f_dentry)
		goto out;

	ino = (pid << 16) + PROC_PID_FD_DIR + fd;
	inode = proc_get_inode(dir->i_sb, ino, NULL);
	if (inode) {
		dentry->d_op = &proc_dentry_operations;
		d_add(dentry, inode);
		err = 0;
	}
out:
	return err;
}

#define NUMBUF 10

static int proc_readfd(struct file * filp, void * dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct task_struct * p, **tarrayp;
	unsigned int fd, pid, ino;
	int retval;
	char buf[NUMBUF];

	retval = -EBADF;
	if (!inode || !S_ISDIR(inode->i_mode))
		goto out;

	retval = 0;
	ino = inode->i_ino;
	pid = ino >> 16;
	ino &= 0x0000ffff;
	if (ino != PROC_PID_FD)
		goto out;

	for (fd = filp->f_pos; fd < 2; fd++, filp->f_pos++) {
		ino = inode->i_ino;
		if (fd)
			ino = (ino & 0xffff0000) | PROC_PID_INO;
		if (filldir(dirent, "..", fd+1, fd, ino) < 0)
			goto out;
	}

	read_lock(&tasklist_lock);
	p = find_task_by_pid(pid);
	if (!p)
		goto out_unlock;
	tarrayp = p->tarray_ptr;

	for (fd -= 2 ; p->files && fd < p->files->max_fds; fd++, filp->f_pos++)
	{
		struct file * file = fcheck_task(p, fd);
		unsigned int i,j;

		if (!file || !file->f_dentry)
			continue;

		j = NUMBUF;
		i = fd;
		do {
			j--;
			buf[j] = '0' + (i % 10);
			i /= 10;
		} while (i);

		/* Drop the task lock, as the filldir function may block */
		read_unlock(&tasklist_lock);

		ino = (pid << 16) + PROC_PID_FD_DIR + fd;
		if (filldir(dirent, buf+j, NUMBUF-j, fd+2, ino) < 0)
			goto out;

		read_lock(&tasklist_lock);
		/* filldir() might have slept, so we must re-validate "p" */
		if (p != *tarrayp || p->pid != pid)
			break;
	}
out_unlock:
	read_unlock(&tasklist_lock);

out:
	return retval;
}

/*
 *  linux/fs/proc/fd.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  proc fd directory handling functions
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

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
	NULL			/* permission */
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
	struct super_block * sb;
	struct inode *inode;
	const char *name;
	int len;

	ino = dir->i_ino;
	pid = ino >> 16;
	ino &= 0x0000ffff;
	if (!dir)
		return -ENOENT;
	sb = dir->i_sb;
	if (!pid || ino != PROC_PID_FD || !S_ISDIR(dir->i_mode))
		return -ENOENT;

	fd = 0;
	len = dentry->d_name.len;
	name = dentry->d_name.name;
	while (len-- > 0) {
		c = *name - '0';
		name++;
		if (c > 9) {
			fd = 0xfffff;
			break;
		}
		fd *= 10;
		fd += c;
		if (fd & 0xffff0000) {
			fd = 0xfffff;
			break;
		}
	}
	p = find_task_by_pid(pid);
	if (!pid || !p)
		return -ENOENT;

	/*
	 *	File handle is invalid if it is out of range, if the process
	 *	has no files (Zombie) if the file is closed, or if its inode
	 *	is NULL
	 */

 	if (fd >= NR_OPEN	||
	    !p->files		||
	    !p->files->fd[fd]	||
	    !p->files->fd[fd]->f_dentry)
		return -ENOENT;

	ino = (pid << 16) + (PROC_PID_FD_DIR << 8) + fd;

	inode = proc_get_inode(sb, ino, NULL);
	if (!inode)
		return -ENOENT;

	d_add(dentry, inode);
	return 0;
}

#define NUMBUF 10

static int proc_readfd(struct file * filp,
	void * dirent, filldir_t filldir)
{
	char buf[NUMBUF];
	struct task_struct * p, **tarrayp;
	unsigned int fd, pid, ino;
	unsigned long i,j;
	struct inode *inode = filp->f_dentry->d_inode;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;
	ino = inode->i_ino;
	pid = ino >> 16;
	ino &= 0x0000ffff;
	if (ino != PROC_PID_FD)
		return 0;

	for (fd = filp->f_pos; fd < 2; fd++, filp->f_pos++) {
		unsigned long ino = inode->i_ino;
		if (fd)
			ino = (ino & 0xffff0000) | PROC_PID_INO;
		if (filldir(dirent, "..", fd+1, fd, ino) < 0)
			return 0;
	}

	p = find_task_by_pid(pid);
	if(!p)
		return 0;
	tarrayp = p->tarray_ptr;

	for (fd -= 2 ; fd < NR_OPEN; fd++, filp->f_pos++) {
		if (!p->files)
			break;
		if (!p->files->fd[fd] || !p->files->fd[fd]->f_dentry)
			continue;

		j = NUMBUF;
		i = fd;
		do {
			j--;
			buf[j] = '0' + (i % 10);
			i /= 10;
		} while (i);

		ino = (pid << 16) + (PROC_PID_FD_DIR << 8) + fd;
		if (filldir(dirent, buf+j, NUMBUF-j, fd+2, ino) < 0)
			break;

		/* filldir() might have slept, so we must re-validate "p" */
		if (p != *tarrayp || p->pid != pid)
			break;
	}
	return 0;
}

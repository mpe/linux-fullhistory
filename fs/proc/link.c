/*
 *  linux/fs/proc/link.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  /proc link-file handling code
 */

#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

static int proc_readlink(struct inode *, char *, int);
static int proc_follow_link(struct inode *, struct inode *, int, int,
			    struct inode **);
static int proc_fd_dupf(struct inode * inode, struct file * f);

#define PLAN9_SEMANTICS

/*
 * links can't do much...
 */
static struct file_operations proc_fd_link_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	proc_fd_dupf,		/* very special open code */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

struct inode_operations proc_link_inode_operations = {
	&proc_fd_link_operations,/* file-operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	proc_readlink,		/* readlink */
	proc_follow_link,	/* follow_link */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

/*
 * This open routine is somewhat of a hack.... what we are doing is
 * looking up the file structure of the newly opened proc fd file, and
 * replacing it with the actual file structure of the process's file
 * descriptor.  This allows plan 9 semantics, so that the returned
 * file descriptor is an dup of the target file descriptor.
 */
static int proc_fd_dupf(struct inode * inode, struct file * f)
{
	unsigned int pid, ino;
	int	i, fd;
	struct task_struct * p;
	struct file *new_f;
	
	for(fd=0 ; fd<NR_OPEN ; fd++)
		if (current->files->fd[fd] == f)
			break;
	if (fd>=NR_OPEN)
		return -ENOENT;	/* should never happen */

	ino = inode->i_ino;
	pid = ino >> 16;
	ino &= 0x0000ffff;

	for (i = 0 ; i < NR_TASKS ; i++)
		if ((p = task[i]) && p->pid == pid)
			break;

	if ((i >= NR_TASKS) ||
	    ((ino >> 8) != 1) || !(new_f = p->files->fd[ino & 0x0ff]))
		return -ENOENT;

	if (new_f->f_mode && !f->f_mode && 3)
		return -EPERM;

	new_f->f_count++;
	current->files->fd[fd] = new_f;
	if (!--f->f_count)
		iput(f->f_inode);
	return 0;
}

static int proc_follow_link(struct inode * dir, struct inode * inode,
	int flag, int mode, struct inode ** res_inode)
{
	unsigned int pid, ino;
	struct task_struct * p;
	struct inode * new_inode;
	int i, error;

	*res_inode = NULL;
	if (dir)
		iput(dir);
	if (!inode)
		return -ENOENT;
	if ((error = permission(inode, MAY_EXEC)) != 0){
		iput(inode);
		return error;
	}
	ino = inode->i_ino;
	pid = ino >> 16;
	ino &= 0x0000ffff;
	for (i = 0 ; i < NR_TASKS ; i++)
		if ((p = task[i]) && p->pid == pid)
			break;
	if (i >= NR_TASKS) {
		iput(inode);
		return -ENOENT;
	}
	new_inode = NULL;
	switch (ino) {
		case PROC_PID_CWD:
			new_inode = p->fs->pwd;
			break;
		case PROC_PID_ROOT:
			new_inode = p->fs->root;
			break;
		case PROC_PID_EXE: {
			struct vm_area_struct * vma = p->mm->mmap;
			while (vma) {
				if (vma->vm_flags & VM_EXECUTABLE) {
					new_inode = vma->vm_inode;
					break;
				}
				vma = vma->vm_next;
			}
			break;
		}
		default:
			switch (ino >> 8) {
			case PROC_PID_FD_DIR:
				ino &= 0xff;
				if (ino < NR_OPEN && p->files->fd[ino]) {
#ifdef PLAN9_SEMANTICS
					if (dir) {
						*res_inode = inode;
						return 0;
					}
#endif
					new_inode = p->files->fd[ino]->f_inode;
				}
				break;
			}
	}
	iput(inode);
	if (!new_inode)
		return -ENOENT;
	*res_inode = new_inode;
	new_inode->i_count++;
	return 0;
}

static int proc_readlink(struct inode * inode, char * buffer, int buflen)
{
	int i;
	unsigned int dev,ino;
	char buf[64];

	if (!S_ISLNK(inode->i_mode)) {
		iput(inode);
		return -EINVAL;
	}
	i = proc_follow_link(NULL, inode, 0, 0, &inode);
	if (i)
		return i;
	if (!inode)
		return -EIO;
	dev = inode->i_dev;
	ino = inode->i_ino;
	iput(inode);
	i = sprintf(buf,"[%04x]:%u", dev, ino);
	if (buflen > i)
		buflen = i;
	i = 0;
	while (i < buflen)
		put_fs_byte(buf[i++],buffer++);
	return i;
}

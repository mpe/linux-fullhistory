/*
 *  linux/fs/proc/link.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  /proc link-file handling code
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

static int proc_readlink(struct dentry *, char *, int);
static struct dentry * proc_follow_link(struct dentry *, struct dentry *);

/*
 * PLAN9_SEMANTICS won't work any more: it used an ugly hack that broke 
 * when the files[] array was updated only after the open code
 */
#undef PLAN9_SEMANTICS

/*
 * links can't do much...
 */
static struct file_operations proc_fd_link_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
	NULL,			/* readdir - bad */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* very special open code */
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
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static struct dentry * proc_follow_link(struct dentry *dentry,
					struct dentry *base)
{
	struct inode *inode = dentry->d_inode;
	struct task_struct *p;
	struct dentry * result;
	int ino, pid;
	int error;

	/* We don't need a base pointer in the /proc filesystem */
	dput(base);

	error = permission(inode, MAY_EXEC);
	result = ERR_PTR(error);
	if (error)
		goto out;

	ino = inode->i_ino;
	pid = ino >> 16;
	ino &= 0x0000ffff;

	p = find_task_by_pid(pid);
	result = ERR_PTR(-ENOENT);
	if (!p)
		goto out;

	switch (ino) {
		case PROC_PID_CWD:
			if (!p->fs || !p->fs->pwd)
				break;
			result = dget(p->fs->pwd);
			break;

		case PROC_PID_ROOT:
			if (!p->fs || !p->fs->root)
				break;
			result = dget(p->fs->root);
			break;

		case PROC_PID_EXE: {
			struct vm_area_struct * vma;
			if (!p->mm)
				break;
			vma = p->mm->mmap;
			while (vma) {
				if ((vma->vm_flags & VM_EXECUTABLE) && vma->vm_file)
					return dget(vma->vm_file->f_dentry);

				vma = vma->vm_next;
			}
			break;
		}
		default:
			switch (ino >> 8) {
			case PROC_PID_FD_DIR:
				if (!p->files)
					break;
				ino &= 0xff;
				if (ino >= NR_OPEN)
					break;
				if (!p->files->fd[ino])
					break;
				if (!p->files->fd[ino]->f_dentry)
					break;
				result = dget(p->files->fd[ino]->f_dentry);
				break;
			}
	}
out:
	return result;
}

/*
 * This pretty-prints the pathname of a dentry,
 * clarifying sockets etc.
 */
static int do_proc_readlink(struct dentry *dentry, char * buffer, int buflen)
{
	struct inode * inode;
	char * tmp = (char*)__get_free_page(GFP_KERNEL), *path, *pattern;
	int len;

	/* Check for special dentries.. */
	pattern = NULL;
	inode = dentry->d_inode;
	if (inode && dentry->d_parent == dentry) {
		if (S_ISSOCK(inode->i_mode))
			pattern = "socket:[%lu]";
		if (S_ISFIFO(inode->i_mode))
			pattern = "pipe:[%lu]";
	}
	
	if (pattern) {
		len = sprintf(tmp, pattern, inode->i_ino);
		path = tmp;
	} else {
		path = d_path(dentry, tmp, PAGE_SIZE);
		len = tmp + PAGE_SIZE - path;
	}

	if (len < buflen)
		buflen = len;
	dput(dentry);
	copy_to_user(buffer, path, buflen);
	free_page((unsigned long)tmp);
	return buflen;
}

static int proc_readlink(struct dentry * dentry, char * buffer, int buflen)
{
	int error;

	dentry = proc_follow_link(dentry, NULL);
	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		error = -ENOENT;
		if (dentry) {
			error = do_proc_readlink(dentry, buffer, buflen);
		}
	}
	return error;
}

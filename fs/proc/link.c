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
#include <linux/dalloc.h>

static int proc_readlink(struct inode *, char *, int);

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
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

/* [Feb-1997 T. Schoebel-Theuer] This is no longer called from the
 * VFS, but only from proc_readlink(). All the functionality
 * should the moved there (without using temporary inodes any more)
 * and then it could be eliminated.
 */
static int proc_follow_link(struct inode * dir, struct inode * inode,
	int flag, int mode, struct inode ** res_inode)
{
	unsigned int pid, ino;
	struct task_struct * p;
	struct inode * new_inode;
	int error;

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

	p = find_task_by_pid(pid);
	if (!p) {
		iput(inode);
		return -ENOENT;
	}
	new_inode = NULL;
	switch (ino) {
		case PROC_PID_CWD:
			if (!p->fs)
				break;
			new_inode = p->fs->pwd;
			break;
		case PROC_PID_ROOT:
			if (!p->fs)
				break;
			new_inode = p->fs->root;
			break;
		case PROC_PID_EXE: {
			struct vm_area_struct * vma;
			if (!p->mm)
				break;
			vma = p->mm->mmap;
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
				if (!p->files)
					break;
				ino &= 0xff;
				if (ino < NR_OPEN && p->files->fd[ino]) {
					new_inode = p->files->fd[ino]->f_inode;
				}
				break;
			}
	}
	iput(inode);
	if (!new_inode)
		return -ENOENT;
	*res_inode = new_inode;
	atomic_inc(&new_inode->i_count);
	return 0;
}

static int proc_readlink(struct inode * inode, char * buffer, int buflen)
{
	int error = proc_follow_link(NULL, inode, 0, 0, &inode);

	if (error)
		return error;
	if (!inode)
		return -EIO;

	/* This will return *one* of the alias names (which is not quite
	 * correct). I have to rethink the problem, so this is only a
	 * quick hack...
	 */
	if(inode->i_dentry) {
		char * tmp = (char*)__get_free_page(GFP_KERNEL);
		int len = d_path(inode->i_dentry, current->fs->root, tmp);
		int min = buflen<PAGE_SIZE ? buflen : PAGE_SIZE;
		if(len <= min)
			min = len+1;
		copy_to_user(buffer, tmp, min);
		free_page((unsigned long)tmp);
		error = len;
	} else {
		error= -ENOENT;
	}
	iput(inode);
	return error;
}

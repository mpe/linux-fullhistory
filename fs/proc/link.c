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
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

static int proc_readlink(struct dentry *, char *, int);
static struct dentry * proc_follow_link(struct dentry *, struct dentry *, unsigned int);

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
	NULL,			/* flush */
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
	NULL,			/* get_block */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* flushpage */
	NULL,			/* truncate */
	proc_permission,	/* permission */
	NULL,			/* smap */
	NULL			/* revalidate */
};

static struct dentry * proc_follow_link(struct dentry *dentry,
					struct dentry *base,
					unsigned int follow)
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

	result = ERR_PTR(-ENOENT);

	switch (ino) {
		case PROC_PID_CWD:
			read_lock(&tasklist_lock);
			p = find_task_by_pid(pid);
			if (p && p->fs && p->fs->pwd)
				result = dget(p->fs->pwd);
			read_unlock(&tasklist_lock);
			break;

		case PROC_PID_ROOT:
			read_lock(&tasklist_lock);
			p = find_task_by_pid(pid);
			if (p && p->fs && p->fs->root)
				result = dget(p->fs->root);
			read_unlock(&tasklist_lock);
			break;

		case PROC_PID_EXE: {
			struct mm_struct *mm = NULL;
			struct vm_area_struct * vma;
			read_lock(&tasklist_lock);
			p = find_task_by_pid(pid);
			if (p)
				mm = p->mm;
			if (mm)
				atomic_inc(&mm->mm_users);
			read_unlock(&tasklist_lock);
			if (!mm)
				break;
			down(&mm->mmap_sem);
			vma = mm->mmap;
			while (vma) {
				if ((vma->vm_flags & VM_EXECUTABLE) && 
				    vma->vm_file) {
					result = dget(vma->vm_file->f_dentry);
					break;
				}
				vma = vma->vm_next;
			}
			up(&mm->mmap_sem);
			mmput(mm);
			break;
		}
		default:
			if (ino & PROC_PID_FD_DIR) {
				struct file * file;
				struct files_struct *files = NULL;
				read_lock(&tasklist_lock);
				p = find_task_by_pid(pid);
				if (p)
					files = p->files;
				read_unlock(&tasklist_lock);
				if (!files)
					break;
				ino &= 0x7fff;
				read_lock(&files->file_lock);
				/* XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX */
				if (ino < files->max_fds &&
				    (file = files->fd[ino]) && file->f_dentry)
					result = dget(file->f_dentry);
				read_unlock(&p->files->file_lock);
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

	if(tmp==NULL)
		return -ENOMEM;
		
	/* Check for special dentries.. */
	pattern = NULL;
	inode = dentry->d_inode;
	if (inode && IS_ROOT(dentry)) {
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
		len = tmp + PAGE_SIZE - 1 - path;
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

	dentry = proc_follow_link(dentry, NULL, 1);
	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		error = -ENOENT;
		if (dentry) {
			error = do_proc_readlink(dentry, buffer, buflen);
		}
	}
	return error;
}

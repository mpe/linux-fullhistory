/*
 *  linux/fs/proc/base.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  proc base directory handling functions
 */

#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

static struct file_operations proc_base_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
	proc_readdir,		/* readdir */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

/*
 * proc directories can do almost nothing..
 */
static struct inode_operations proc_base_inode_operations = {
	&proc_base_operations,	/* default base directory file-ops */
	NULL,			/* create */
	proc_lookup,		/* lookup */
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

static void proc_pid_fill_inode(struct inode * inode)
{
	struct task_struct * p;
	int pid = inode->i_ino >> 16;
	int ino = inode->i_ino & 0xffff;

	for_each_task(p) {
		if (p->pid == pid) {
			if (p->dumpable || ino == PROC_PID_INO) {
				inode->i_uid = p->euid;
				inode->i_gid = p->gid;
			}
			return;
		}
	}
}

/*
 * This is really a pseudo-entry, and only links
 * backwards to the parent with no link from the
 * root directory to this. This way we can have just
 * one entry for every /proc/<pid>/ directory.
 */
struct proc_dir_entry proc_pid = {
	PROC_PID_INO, 5, "<pid>",
	S_IFDIR | S_IRUGO | S_IXUGO, 2, 0, 0,
	0, &proc_base_inode_operations,
	NULL, proc_pid_fill_inode,
	NULL, &proc_root, NULL
};

void proc_base_init(void)
{
	proc_register(&proc_pid, &(struct proc_dir_entry) {
		PROC_PID_STATUS, 6, "status",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_array_inode_operations,
		NULL, proc_pid_fill_inode,
	});
	proc_register(&proc_pid, &(struct proc_dir_entry) {
		PROC_PID_MEM, 3, "mem",
		S_IFREG | S_IRUSR | S_IWUSR, 1, 0, 0,
		0, &proc_mem_inode_operations,
		NULL, proc_pid_fill_inode,
	});
	proc_register(&proc_pid, &(struct proc_dir_entry) {
		PROC_PID_CWD, 3, "cwd",
		S_IFLNK | S_IRWXU, 1, 0, 0,
		0, &proc_link_inode_operations,
		NULL, proc_pid_fill_inode,
	});
	proc_register(&proc_pid, &(struct proc_dir_entry) {
		PROC_PID_ROOT, 4, "root",
		S_IFLNK | S_IRWXU, 1, 0, 0,
		0, &proc_link_inode_operations,
		NULL, proc_pid_fill_inode,
	});
	proc_register(&proc_pid, &(struct proc_dir_entry) {
		PROC_PID_EXE, 3, "exe",
		S_IFLNK | S_IRWXU, 1, 0, 0,
		0, &proc_link_inode_operations,
		NULL, proc_pid_fill_inode,
	});
	proc_register(&proc_pid, &(struct proc_dir_entry) {
		PROC_PID_FD, 2, "fd",
		S_IFDIR | S_IRUSR | S_IXUSR, 1, 0, 0,
		0, &proc_fd_inode_operations,
		NULL, proc_pid_fill_inode,
	});
	proc_register(&proc_pid, &(struct proc_dir_entry) {
		PROC_PID_ENVIRON, 7, "environ",
		S_IFREG | S_IRUSR, 1, 0, 0,
		0, &proc_array_inode_operations,
		NULL, proc_pid_fill_inode,
	});
	proc_register(&proc_pid, &(struct proc_dir_entry) {
		PROC_PID_CMDLINE, 7, "cmdline",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_array_inode_operations,
		NULL, proc_pid_fill_inode,
	});
	proc_register(&proc_pid, &(struct proc_dir_entry) {
		PROC_PID_STAT, 4, "stat",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_array_inode_operations,
		NULL, proc_pid_fill_inode,
	});
	proc_register(&proc_pid, &(struct proc_dir_entry) {
		PROC_PID_STATM, 5, "statm",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_array_inode_operations,
		NULL, proc_pid_fill_inode,
	});
	proc_register(&proc_pid, &(struct proc_dir_entry) {
		PROC_PID_MAPS, 4, "maps",
		S_IFIFO | S_IRUGO, 1, 0, 0,
		0, &proc_arraylong_inode_operations,
		NULL, proc_pid_fill_inode,
	});
};

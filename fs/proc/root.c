/*
 *  linux/fs/proc/root.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  proc root directory handling functions
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/config.h>
#include <linux/init.h>
#include <asm/bitops.h>

struct proc_dir_entry *proc_net, *proc_bus, *proc_root_fs, *proc_root_driver;

#ifdef CONFIG_SYSCTL
struct proc_dir_entry *proc_sys_root;
#endif

/*
 * /proc/self:
 */
static int proc_self_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	char tmp[30];
	sprintf(tmp, "%d", current->pid);
	return vfs_readlink(dentry,buffer,buflen,tmp);
}

static int proc_self_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	char tmp[30];
	sprintf(tmp, "%d", current->pid);
	return vfs_follow_link(nd,tmp);
}	

static struct inode_operations proc_self_inode_operations = {
	readlink:	proc_self_readlink,
	follow_link:	proc_self_follow_link
};

static struct proc_dir_entry proc_root_self = {
	0, 4, "self",
	S_IFLNK | S_IRUGO | S_IWUGO | S_IXUGO, 1, 0, 0,
	64, &proc_self_inode_operations,
};

void __init proc_root_init(void)
{
	proc_misc_init();
	proc_register(&proc_root, &proc_root_self);
	proc_net = proc_mkdir("net", 0);
#ifdef CONFIG_SYSVIPC
	proc_mkdir("sysvipc", 0);
#endif
#ifdef CONFIG_SYSCTL
	proc_sys_root = proc_mkdir("sys", 0);
#endif
	proc_root_fs = proc_mkdir("fs", 0);
	proc_root_driver = proc_mkdir("driver", 0);
#if defined(CONFIG_SUN_OPENPROMFS) || defined(CONFIG_SUN_OPENPROMFS_MODULE)
	/* just give it a mountpoint */
	proc_mkdir("openprom", 0);
#endif
	proc_tty_init();
#ifdef CONFIG_PROC_DEVICETREE
	proc_device_tree_init();
#endif
	proc_bus = proc_mkdir("bus", 0);
}

static struct dentry *proc_root_lookup(struct inode * dir, struct dentry * dentry)
{
	struct task_struct *p;

	if (dir->i_ino == PROC_ROOT_INO) { /* check for safety... */
		extern unsigned long total_forks;
		static int last_timestamp = 0;

		/*
		 * this one can be a serious 'ps' performance problem if
		 * there are many threads running - thus we do 'lazy'
		 * link-recalculation - we change it only if the number
		 * of threads has increased.
		 */
		if (total_forks != last_timestamp) {
			int nlink = proc_root.nlink;

			read_lock(&tasklist_lock);
			last_timestamp = total_forks;
			for_each_task(p)
				nlink++;
			read_unlock(&tasklist_lock);
			/*
			 * subtract the # of idle threads which
			 * do not show up in /proc:
			 */
			dir->i_nlink = nlink - smp_num_cpus;
		}
	}

	if (!proc_lookup(dir, dentry))
		return NULL;
	
	return proc_pid_lookup(dir, dentry);
}

static int proc_root_readdir(struct file * filp,
	void * dirent, filldir_t filldir)
{
	unsigned int nr = filp->f_pos;

	if (nr < FIRST_PROCESS_ENTRY) {
		int error = proc_readdir(filp, dirent, filldir);
		if (error <= 0)
			return error;
		filp->f_pos = FIRST_PROCESS_ENTRY;
	}

	return proc_pid_readdir(filp, dirent, filldir);
}

/*
 * The root /proc directory is special, as it has the
 * <pid> directories. Thus we don't use the generic
 * directory handling functions for that..
 */
static struct file_operations proc_root_operations = {
	readdir:	 proc_root_readdir,
};

/*
 * proc root can do almost nothing..
 */
static struct inode_operations proc_root_inode_operations = {
	lookup:		proc_root_lookup,
};

/*
 * This is the root "inode" in the /proc tree..
 */
struct proc_dir_entry proc_root = {
	PROC_ROOT_INO, 5, "/proc",
	S_IFDIR | S_IRUGO | S_IXUGO, 2, 0, 0,
	0, &proc_root_inode_operations, &proc_root_operations,
	NULL, NULL,
	NULL,
	&proc_root, NULL
};

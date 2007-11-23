/*
 *  linux/fs/proc/root.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  proc root directory handling functions
 */

#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/config.h>

static int proc_readroot(struct inode *, struct file *, struct dirent *, int);
static int proc_lookuproot(struct inode *,const char *,int,struct inode **);

static struct file_operations proc_root_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
	proc_readroot,		/* readdir */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	NULL			/* no fsync */
};

/*
 * proc directories can do almost nothing..
 */
struct inode_operations proc_root_inode_operations = {
	&proc_root_operations,	/* default base directory file-ops */
	NULL,			/* create */
	proc_lookuproot,	/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static struct proc_dir_entry root_dir[] = {
	{ PROC_ROOT_INO,	1, "." },
	{ PROC_ROOT_INO,	2, ".." },
	{ PROC_LOADAVG,		7, "loadavg" },
	{ PROC_UPTIME,		6, "uptime" },
	{ PROC_MEMINFO,		7, "meminfo" },
	{ PROC_KMSG,		4, "kmsg" },
	{ PROC_VERSION,		7, "version" },
#ifdef CONFIG_PCI
	{ PROC_PCI,             3, "pci"  },
#endif
	{ PROC_CPUINFO,		7, "cpuinfo" },
	{ PROC_SELF,		4, "self" },	/* will change inode # */
	{ PROC_NET,		3, "net" },
#ifdef CONFIG_DEBUG_MALLOC
	{ PROC_MALLOC,		6, "malloc" },
#endif
	{ PROC_KCORE,		5, "kcore" },
   	{ PROC_MODULES,		7, "modules" },
   	{ PROC_STAT,		4, "stat" },
   	{ PROC_DEVICES,		7, "devices" },
	{ PROC_INTERRUPTS,	10,"interrupts" },
   	{ PROC_FILESYSTEMS,	11,"filesystems" },
   	{ PROC_KSYMS,		5, "ksyms" },
   	{ PROC_DMA,		3, "dma" },
	{ PROC_IOPORTS,		7, "ioports"},
#ifdef CONFIG_PROFILE
	{ PROC_PROFILE,		7, "profile"},
#endif
};

#define NR_ROOT_DIRENTRY ((sizeof (root_dir))/(sizeof (root_dir[0])))

static int proc_lookuproot(struct inode * dir,const char * name, int len,
	struct inode ** result)
{
	unsigned int pid, c;
	int i, ino;

	*result = NULL;
	if (!dir)
		return -ENOENT;
	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOENT;
	}
	i = NR_ROOT_DIRENTRY;
	while (i-- > 0 && !proc_match(len,name,root_dir+i))
		/* nothing */;
	if (i >= 0) {
		ino = root_dir[i].low_ino;
		if (ino == PROC_ROOT_INO) {
			*result = dir;
			return 0;
		}
		if (ino == PROC_SELF) /* self modifying inode ... */
			ino = (current->pid << 16) + 2;
	} else {
		pid = 0;
		while (len-- > 0) {
			c = *name - '0';
			name++;
			if (c > 9) {
				pid = 0;
				break;
			}
			pid *= 10;
			pid += c;
			if (pid & 0xffff0000) {
				pid = 0;
				break;
			}
		}
		for (i = 0 ; i < NR_TASKS ; i++)
			if (task[i] && task[i]->pid == pid)
				break;
		if (!pid || i >= NR_TASKS) {
			iput(dir);
			return -ENOENT;
		}
		ino = (pid << 16) + 2;
	}
	if (!(*result = iget(dir->i_sb,ino))) {
		iput(dir);
		return -ENOENT;
	}
	iput(dir);
	return 0;
}

static int proc_readroot(struct inode * inode, struct file * filp,
	struct dirent * dirent, int count)
{
	struct task_struct * p;
	unsigned int nr,pid;
	int i,j;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;
repeat:
	nr = filp->f_pos;
	if (nr < NR_ROOT_DIRENTRY) {
		struct proc_dir_entry * de = root_dir + nr;

		filp->f_pos++;
		i = de->namelen;
		put_fs_long(de->low_ino, &dirent->d_ino);
		put_fs_word(i,&dirent->d_reclen);
		put_fs_byte(0,i+dirent->d_name);
		j = i;
		while (i--)
			put_fs_byte(de->name[i], i+dirent->d_name);
		return j;
	}
	nr -= NR_ROOT_DIRENTRY;
	if (nr >= NR_TASKS)
		return 0;
	filp->f_pos++;
	p = task[nr];
	if (!p || !(pid = p->pid))
		goto repeat;
	if (pid & 0xffff0000)
		goto repeat;
	j = 10;
	i = 1;
	while (pid >= j) {
		j *= 10;
		i++;
	}
	j = i;
	put_fs_long((pid << 16)+2, &dirent->d_ino);
	put_fs_word(i, &dirent->d_reclen);
	put_fs_byte(0, i+dirent->d_name);
	while (i--) {
		put_fs_byte('0'+(pid % 10), i+dirent->d_name);
		pid /= 10;
	}
	return j;
}

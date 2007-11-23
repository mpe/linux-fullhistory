/*
 *  linux/fs/proc/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/locks.h>
#include <linux/limits.h>
#include <linux/config.h>

#include <asm/system.h>
#include <asm/segment.h>

extern unsigned long prof_len;

void proc_put_inode(struct inode *inode)
{
	if (inode->i_nlink)
		return;
	inode->i_size = 0;
}

void proc_put_super(struct super_block *sb)
{
	lock_super(sb);
	sb->s_dev = 0;
	unlock_super(sb);
}

static struct super_operations proc_sops = { 
	proc_read_inode,
	NULL,
	proc_write_inode,
	proc_put_inode,
	proc_put_super,
	NULL,
	proc_statfs,
	NULL
};


static int parse_options(char *options,uid_t *uid,gid_t *gid)
{
	char *this_char,*value;

	*uid = current->uid;
	*gid = current->gid;
	if (!options) return 1;
	for (this_char = strtok(options,","); this_char; this_char = strtok(NULL,",")) {
		if ((value = strchr(this_char,'=')) != NULL)
			*value++ = 0;
		if (!strcmp(this_char,"uid")) {
			if (!value || !*value)
				return 0;
			*uid = simple_strtoul(value,&value,0);
			if (*value)
				return 0;
		}
		else if (!strcmp(this_char,"gid")) {
			if (!value || !*value)
				return 0;
			*gid = simple_strtoul(value,&value,0);
			if (*value)
				return 0;
		}
		else return 0;
	}
	return 1;
}


struct super_block *proc_read_super(struct super_block *s,void *data, 
				    int silent)
{
	lock_super(s);
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = PROC_SUPER_MAGIC;
	s->s_op = &proc_sops;
	unlock_super(s);
	if (!(s->s_mounted = iget(s,PROC_ROOT_INO))) {
		s->s_dev = 0;
		printk("get root inode failed\n");
		return NULL;
	}
	parse_options(data, &s->s_mounted->i_uid, &s->s_mounted->i_gid);
	return s;
}

void proc_statfs(struct super_block *sb, struct statfs *buf)
{
	put_fs_long(PROC_SUPER_MAGIC, &buf->f_type);
	put_fs_long(PAGE_SIZE/sizeof(long), &buf->f_bsize);
	put_fs_long(0, &buf->f_blocks);
	put_fs_long(0, &buf->f_bfree);
	put_fs_long(0, &buf->f_bavail);
	put_fs_long(0, &buf->f_files);
	put_fs_long(0, &buf->f_ffree);
	put_fs_long(NAME_MAX, &buf->f_namelen);
	/* Don't know what value to put in buf->f_fsid */
}

void proc_read_inode(struct inode * inode)
{
	unsigned long ino, pid;
	struct task_struct * p;
	int i;
	
	inode->i_op = NULL;
	inode->i_mode = 0;
	inode->i_uid = 0;
	inode->i_gid = 0;
	inode->i_nlink = 1;
	inode->i_size = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_blocks = 0;
	inode->i_blksize = 1024;
	ino = inode->i_ino;
	pid = ino >> 16;
	p = task[0];
	for (i = 0; i < NR_TASKS ; i++)
		if ((p = task[i]) && (p->pid == pid))
			break;
	if (!p || i >= NR_TASKS)
		return;
	if (ino == PROC_ROOT_INO) {
		inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO;
		inode->i_nlink = 2;
		for (i = 1 ; i < NR_TASKS ; i++)
			if (task[i])
				inode->i_nlink++;
		inode->i_op = &proc_root_inode_operations;
		return;
	}

#ifdef CONFIG_IP_ACCT
	/* this file may be opened R/W by root to reset the accounting */
	if (ino == PROC_NET_IPACCT) {
		inode->i_mode = S_IFREG | S_IRUGO | S_IWUSR;
		inode->i_op = &proc_net_inode_operations;
		return;
	}
#endif
#ifdef CONFIG_IP_FIREWALL
	/* these files may be opened R/W by root to reset the counters */
	if ((ino == PROC_NET_IPFWFWD) || (ino == PROC_NET_IPFWBLK)) {
		inode->i_mode = S_IFREG | S_IRUGO | S_IWUSR;
		inode->i_op = &proc_net_inode_operations;
		return;
	}
#endif

	/* other files within /proc/net */
	if ((ino >= PROC_NET_UNIX) && (ino < PROC_NET_LAST)) {
		inode->i_mode = S_IFREG | S_IRUGO;
		inode->i_op = &proc_net_inode_operations;
		return;
	}

	if (!pid) {
		switch (ino) {
			case PROC_KMSG:
				inode->i_mode = S_IFREG | S_IRUSR;
				inode->i_op = &proc_kmsg_inode_operations;
				break;
			case PROC_NET:
				inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO;
				inode->i_nlink = 2;
				inode->i_op = &proc_net_inode_operations;
				break;
			case PROC_KCORE:
				inode->i_mode = S_IFREG | S_IRUSR;
				inode->i_op = &proc_kcore_inode_operations;
				inode->i_size = high_memory + PAGE_SIZE;
				break;
#ifdef CONFIG_PROFILE
			case PROC_PROFILE:
				inode->i_mode = S_IFREG | S_IRUGO | S_IWUSR;
				inode->i_op = &proc_profile_inode_operations;
				inode->i_size = (1+prof_len) * sizeof(unsigned long);
				break;
#endif
			default:
				inode->i_mode = S_IFREG | S_IRUGO;
				inode->i_op = &proc_array_inode_operations;
				break;
		}
		return;
	}
	ino &= 0x0000ffff;
	inode->i_uid = p->euid;
	inode->i_gid = p->egid;
	switch (ino) {
		case PROC_PID_INO:
			inode->i_nlink = 4;
			inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO;
			inode->i_op = &proc_base_inode_operations;
			return;
		case PROC_PID_MEM:
			inode->i_op = &proc_mem_inode_operations;
			inode->i_mode = S_IFREG | S_IRUSR | S_IWUSR;
			return;
		case PROC_PID_CWD:
		case PROC_PID_ROOT:
		case PROC_PID_EXE:
			inode->i_op = &proc_link_inode_operations;
			inode->i_size = 64;
			inode->i_mode = S_IFLNK | S_IRWXU;
			return;
		case PROC_PID_FD:
			inode->i_mode = S_IFDIR | S_IRUSR | S_IXUSR;
			inode->i_op = &proc_fd_inode_operations;
			inode->i_nlink = 2;
			return;
		case PROC_PID_ENVIRON:
			inode->i_mode = S_IFREG | S_IRUSR;
			inode->i_op = &proc_array_inode_operations;
			return;
		case PROC_PID_CMDLINE:
		case PROC_PID_STAT:
		case PROC_PID_STATM:
			inode->i_mode = S_IFREG | S_IRUGO;
			inode->i_op = &proc_array_inode_operations;
			return;
		case PROC_PID_MAPS:
			inode->i_mode = S_IFIFO | S_IRUGO;
			inode->i_op = &proc_arraylong_inode_operations;
			return;
	}
	switch (ino >> 8) {
		case PROC_PID_FD_DIR:
			ino &= 0xff;
			if (ino >= NR_OPEN || !p->files->fd[ino])
				return;
			inode->i_op = &proc_link_inode_operations;
			inode->i_size = 64;
			inode->i_mode = S_IFLNK;
			if (p->files->fd[ino]->f_mode & 1)
				inode->i_mode |= S_IRUSR | S_IXUSR;
			if (p->files->fd[ino]->f_mode & 2)
				inode->i_mode |= S_IWUSR | S_IXUSR;
			return;
	}
	return;
}

void proc_write_inode(struct inode * inode)
{
	inode->i_dirt=0;
}

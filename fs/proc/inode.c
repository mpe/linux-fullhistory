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
#include <asm/uaccess.h>

static void proc_put_inode(struct inode *inode)
{
#ifdef CONFIG_SUN_OPENPROMFS_MODULE
	if ((inode->i_ino >= PROC_OPENPROM_FIRST)
	    && (inode->i_ino < PROC_OPENPROM_FIRST + PROC_NOPENPROM)
	    && proc_openprom_use)
		(*proc_openprom_use)(inode, 0);
#endif	
}

/*
 * Does this ever happen?
 */
static void proc_delete_inode(struct inode *inode)
{
	printk("proc_delete_inode()?\n");
	inode->i_size = 0;
}

static void proc_put_super(struct super_block *sb)
{
	lock_super(sb);
	sb->s_dev = 0;
	unlock_super(sb);
}

static struct super_operations proc_sops = { 
	proc_read_inode,
	proc_write_inode,
	proc_put_inode,
	proc_delete_inode,
	NULL,
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
		else return 1;
	}
	return 1;
}

struct inode * proc_get_inode(struct super_block * s, int ino, struct proc_dir_entry * de)
{
	struct inode * inode = iget(s, ino);
	
#ifdef CONFIG_SUN_OPENPROMFS_MODULE
	if ((inode->i_ino >= PROC_OPENPROM_FIRST)
	    && (inode->i_ino < PROC_OPENPROM_FIRST + PROC_NOPENPROM)
	    && proc_openprom_use)
		(*proc_openprom_use)(inode, 1);
#endif	
	if (inode && inode->i_sb == s) {
		inode->u.generic_ip = (void *) de;
		if (de) {
			if (de->mode) {
				inode->i_mode = de->mode;
				inode->i_uid = de->uid;
				inode->i_gid = de->gid;
			}
			if (de->size)
				inode->i_size = de->size;
			if (de->ops)
				inode->i_op = de->ops;
			if (de->nlink)
				inode->i_nlink = de->nlink;
			if (de->fill_inode)
				de->fill_inode(inode);
		}
	}
	/*
	 * Fixup the root inode's nlink value
	 */
	if (inode->i_ino == PROC_ROOT_INO) {
		struct task_struct *p;

		read_lock(&tasklist_lock);
		for_each_task(p) {
			if (p->pid)
				inode->i_nlink++;
		}
		read_unlock(&tasklist_lock);
	}
	return inode;
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
	s->s_root = d_alloc_root(proc_get_inode(s, PROC_ROOT_INO, &proc_root), NULL);
	if (!s->s_root) {
		s->s_dev = 0;
		printk("get root inode failed\n");
		return NULL;
	}
	parse_options(data, &s->s_root->d_inode->i_uid, &s->s_root->d_inode->i_gid);
	return s;
}

int proc_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	struct statfs tmp;

	tmp.f_type = PROC_SUPER_MAGIC;
	tmp.f_bsize = PAGE_SIZE/sizeof(long);
	tmp.f_blocks = 0;
	tmp.f_bfree = 0;
	tmp.f_bavail = 0;
	tmp.f_files = 0;
	tmp.f_ffree = 0;
	tmp.f_namelen = NAME_MAX;
	return copy_to_user(buf, &tmp, bufsiz) ? -EFAULT : 0;
}

void proc_read_inode(struct inode * inode)
{
	unsigned long ino, pid;
	struct task_struct * p;
	
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_blocks = 0;
	inode->i_blksize = 1024;
	ino = inode->i_ino;
	if (ino >= PROC_OPENPROM_FIRST && ino < PROC_OPENPROM_FIRST + PROC_NOPENPROM)
		return;
	inode->i_op = NULL;
	inode->i_mode = 0;
	inode->i_uid = 0;
	inode->i_gid = 0;
	inode->i_nlink = 1;
	inode->i_size = 0;
	pid = ino >> 16;

	if (!pid || ((p = find_task_by_pid(pid)) == NULL))
		return;

	ino &= 0x0000ffff;
	if (ino == PROC_PID_INO || p->dumpable) {
		inode->i_uid = p->euid;
		inode->i_gid = p->egid;
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
}

void proc_write_inode(struct inode * inode)
{
}

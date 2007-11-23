/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/devpts/inode.c
 *
 *  Copyright 1998 H. Peter Anvin -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/module.h>

#include <linux/string.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/locks.h>
#include <linux/major.h>
#include <linux/malloc.h>
#include <linux/stat.h>
#include <linux/tty.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>

#include "devpts_i.h"

static struct super_block *mounts = NULL;

static void devpts_put_super(struct super_block *sb)
{
	struct devpts_sb_info *sbi = SBI(sb);
	struct inode *inode;
	int i;

	for ( i = 0 ; i < sbi->max_ptys ; i++ ) {
		if ( (inode = sbi->inodes[i]) ) {
			if ( inode->i_count != 1 )
				printk("devpts_put_super: badness: entry %d count %d\n",
				       i, inode->i_count);
			inode->i_nlink--;
			iput(inode);
		}
	}

	*sbi->back = sbi->next;
	if ( sbi->next )
		SBI(sbi->next)->back = sbi->back;

	kfree(sbi->inodes);
	kfree(sbi);
}

static int devpts_statfs(struct super_block *sb, struct statfs *buf);
static void devpts_read_inode(struct inode *inode);

static struct super_operations devpts_sops = {
	read_inode:	devpts_read_inode,
	put_super:	devpts_put_super,
	statfs:		devpts_statfs,
};

static int devpts_parse_options(char *options, struct devpts_sb_info *sbi)
{
	int setuid = 0;
	int setgid = 0;
	uid_t uid = 0;
	gid_t gid = 0;
	umode_t mode = 0600;
	char *this_char, *value;

	this_char = NULL;
	if ( options )
		this_char = strtok(options,",");
	for ( ; this_char; this_char = strtok(NULL,",")) {
		if ((value = strchr(this_char,'=')) != NULL)
			*value++ = 0;
		if (!strcmp(this_char,"uid")) {
			if (!value || !*value)
				return 1;
			uid = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
			setuid = 1;
		}
		else if (!strcmp(this_char,"gid")) {
			if (!value || !*value)
				return 1;
			gid = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
			setgid = 1;
		}
		else if (!strcmp(this_char,"mode")) {
			if (!value || !*value)
				return 1;
			mode = simple_strtoul(value,&value,8);
			if (*value)
				return 1;
		}
		else
			return 1;
	}
	sbi->setuid  = setuid;
	sbi->setgid  = setgid;
	sbi->uid     = uid;
	sbi->gid     = gid;
	sbi->mode    = mode & ~S_IFMT;

	return 0;
}

struct super_block *devpts_read_super(struct super_block *s, void *data,
				      int silent)
{
	struct inode * root_inode;
	struct dentry * root;
	struct devpts_sb_info *sbi;

	/* Super block already completed? */
	if (s->s_root)
		goto out_unlock;

	sbi = (struct devpts_sb_info *) kmalloc(sizeof(struct devpts_sb_info), GFP_KERNEL);
	if ( !sbi )
		goto fail_unlock;

	sbi->magic  = DEVPTS_SBI_MAGIC;
	sbi->max_ptys = unix98_max_ptys;
	sbi->inodes = kmalloc(sizeof(struct inode *) * sbi->max_ptys, GFP_KERNEL);
	if ( !sbi->inodes ) {
		kfree(sbi);
		goto fail_unlock;
	}
	memset(sbi->inodes, 0, sizeof(struct inode *) * sbi->max_ptys);

	s->u.generic_sbp = (void *) sbi;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = DEVPTS_SUPER_MAGIC;
	s->s_op = &devpts_sops;
	s->s_root = NULL;

	/*
	 * Get the root inode and dentry, but defer checking for errors.
	 */
	root_inode = iget(s, 1); /* inode 1 == root directory */
	root = d_alloc_root(root_inode);

	/*
	 * Check whether somebody else completed the super block.
	 */
	if (s->s_root)
		goto out_dput;

	if (!root)
		goto fail_iput;

	/* Can this call block?  (It shouldn't) */
	if ( devpts_parse_options(data,sbi) ) {
		printk("devpts: called with bogus options\n");
		goto fail_dput;
	}

	/*
	 * Check whether somebody else completed the super block.
	 */
	if (s->s_root)
		goto out_dec;
	
	/*
	 * Success! Install the root dentry now to indicate completion.
	 */
	s->s_root = root;

	sbi->next = mounts;
	if ( sbi->next )
		SBI(sbi->next)->back = &(sbi->next);
	sbi->back = &mounts;
	mounts = s;

	return s;

	/*
	 * Success ... somebody else completed the super block for us. 
	 */ 
out_unlock:
	goto out_dec;
out_dput:
	if (root)
		dput(root);
	else
		iput(root_inode);
out_dec:
	return s;
	
	/*
	 * Failure ... clear the s_dev slot and clean up.
	 */
fail_dput:
	/*
	 * dput() can block, so we clear the super block first.
	 */
	dput(root);
	goto fail_free;
fail_iput:
	printk("devpts: get root dentry failed\n");
	/*
	 * iput() can block, so we clear the super block first.
	 */
	iput(root_inode);
fail_free:
	kfree(sbi);
fail_unlock:
	return NULL;
}

static int devpts_statfs(struct super_block *sb, struct statfs *buf)
{
	buf->f_type = DEVPTS_SUPER_MAGIC;
	buf->f_bsize = 1024;
	buf->f_bfree = 0;
	buf->f_bavail = 0;
	buf->f_ffree = 0;
	buf->f_namelen = NAME_MAX;
	return 0;
}

static void devpts_read_inode(struct inode *inode)
{
	ino_t ino = inode->i_ino;
	struct devpts_sb_info *sbi = SBI(inode->i_sb);

	inode->i_mode = 0;
	inode->i_nlink = 0;
	inode->i_size = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_blocks = 0;
	inode->i_blksize = 1024;
	inode->i_uid = inode->i_gid = 0;

	if ( ino == 1 ) {
		inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
		inode->i_op = &devpts_root_inode_operations;
		inode->i_fop = &devpts_root_operations;
		inode->i_nlink = 2;
		return;
	} 

	ino -= 2;
	if ( ino >= sbi->max_ptys )
		return;		/* Bogus */
	
	/* Gets filled in by devpts_pty_new() */
	init_special_inode(inode,S_IFCHR,0);

	return;
}

static DECLARE_FSTYPE(devpts_fs_type, "devpts", devpts_read_super, 0);

void devpts_pty_new(int number, kdev_t device)
{
	struct super_block *sb;
	struct devpts_sb_info *sbi;
	struct inode *inode;
		
	for ( sb = mounts ; sb ; sb = sbi->next ) {
		sbi = SBI(sb);

		if ( sbi->inodes[number] ) {
			continue; /* Already registered, this does happen */
		}
		
		/* Yes, this looks backwards, but it is correct */
		inode = iget(sb, number+2);
		if ( inode ) {
			inode->i_uid = sbi->setuid ? sbi->uid : current->fsuid;
			inode->i_gid = sbi->setgid ? sbi->gid : current->fsgid;
			inode->i_mode = sbi->mode | S_IFCHR;
			inode->i_rdev = device;
			inode->i_nlink++;
			sbi->inodes[number] = inode;
		}
	}
}

void devpts_pty_kill(int number)
{
	struct super_block *sb;
	struct devpts_sb_info *sbi;
	struct inode *inode;
		
	for ( sb = mounts ; sb ; sb = sbi->next ) {
		sbi = SBI(sb);

		inode = sbi->inodes[number];

		if ( inode ) {
			sbi->inodes[number] = NULL;
			inode->i_nlink--;
			iput(inode);
		}
	}
}

int __init init_devpts_fs(void)
{
	return register_filesystem(&devpts_fs_type);

}

#ifdef MODULE

int init_module(void)
{
	int err = init_devpts_fs();
	if ( !err ) {
		devpts_upcall_new  = devpts_pty_new;
		devpts_upcall_kill = devpts_pty_kill;
	}
	return err;
}

void cleanup_module(void)
{
	devpts_upcall_new  = NULL;
	devpts_upcall_kill = NULL;
	unregister_filesystem(&devpts_fs_type);
}

#endif

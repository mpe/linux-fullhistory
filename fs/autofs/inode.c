/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/autofs/inode.c
 *
 *  Copyright 1997 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/file.h>
#include <linux/locks.h>
#include <asm/bitops.h>
#include "autofs_i.h"
#define __NO_VERSION__
#include <linux/module.h>

static void autofs_put_inode(struct inode *inode)
{
	if (inode->i_nlink)
		return;
	inode->i_size = 0;
}

static void autofs_put_super(struct super_block *sb)
{
	struct autofs_sb_info *sbi =
		(struct autofs_sb_info *) sb->u.generic_sbp;
	unsigned int n;

	if ( !sbi->catatonic )
		autofs_catatonic_mode(sbi); /* Free wait queues, close pipe */

	lock_super(sb);
	autofs_hash_nuke(&sbi->dirhash);
	for ( n = 0 ; n < AUTOFS_MAX_SYMLINKS ; n++ ) {
		if ( test_bit(n, sbi->symlink_bitmap) )
			kfree(sbi->symlink[n].data);
	}

	sb->s_dev = 0;
	kfree(sb->u.generic_sbp);
	unlock_super(sb);

	DPRINTK(("autofs: shutting down\n"));
	
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}

static int autofs_statfs(struct super_block *sb, struct statfs *buf, int bufsiz);
static void autofs_read_inode(struct inode *inode);
static void autofs_write_inode(struct inode *inode);

static struct super_operations autofs_sops = {
	autofs_read_inode,
	NULL,
	autofs_write_inode,
	autofs_put_inode,
	autofs_put_super,
	NULL,
	autofs_statfs,
	NULL
};

static int parse_options(char *options, int *pipefd, uid_t *uid, gid_t *gid, pid_t *pgrp, int *minproto, int *maxproto)
{
	char *this_char, *value;
	
	*uid = current->uid;
	*gid = current->gid;
	*pgrp = current->pgrp;

	*minproto = *maxproto = AUTOFS_PROTO_VERSION;

	*pipefd = -1;

	if ( !options ) return 1;
	for (this_char = strtok(options,","); this_char; this_char = strtok(NULL,",")) {
		if ((value = strchr(this_char,'=')) != NULL)
			*value++ = 0;
		if (!strcmp(this_char,"fd")) {
			if (!value || !*value)
				return 1;
			*pipefd = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		}
		else if (!strcmp(this_char,"uid")) {
			if (!value || !*value)
				return 1;
			*uid = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		}
		else if (!strcmp(this_char,"gid")) {
			if (!value || !*value)
				return 1;
			*gid = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		}
		else if (!strcmp(this_char,"pgrp")) {
			if (!value || !*value)
				return 1;
			*pgrp = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		}
		else if (!strcmp(this_char,"minproto")) {
			if (!value || !*value)
				return 1;
			*minproto = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		}
		else if (!strcmp(this_char,"maxproto")) {
			if (!value || !*value)
				return 1;
			*maxproto = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		}
		else break;
	}
	return (*pipefd < 0);
}

struct super_block *autofs_read_super(struct super_block *s, void *data,
				      int silent)
{
	int pipefd;
	struct autofs_sb_info *sbi;
	int minproto, maxproto;

	MOD_INC_USE_COUNT;

	lock_super(s);
	sbi = (struct autofs_sb_info *) kmalloc(sizeof(struct autofs_sb_info), GFP_KERNEL);
	if ( !sbi ) {
		s->s_dev = 0;
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	DPRINTK(("autofs: starting up, sbi = %p\n",sbi));

	s->u.generic_sbp = sbi;
	sbi->magic = AUTOFS_SBI_MAGIC;
	sbi->catatonic = 0;
	sbi->exp_timeout = 0;
	sbi->oz_pgrp = current->pgrp;
	autofs_initialize_hash(&sbi->dirhash);
	sbi->queues = NULL;
	memset(sbi->symlink_bitmap, 0, sizeof(u32)*AUTOFS_SYMLINK_BITMAP_LEN);
	sbi->next_dir_ino = AUTOFS_FIRST_DIR_INO;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = AUTOFS_SUPER_MAGIC;
	s->s_op = &autofs_sops;
	unlock_super(s);
	s->s_root = d_alloc_root(iget(s, AUTOFS_ROOT_INO), NULL);
	if (!s->s_root) {
		s->s_dev = 0;
		kfree(sbi);
		printk("autofs: get root inode failed\n");
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	if ( parse_options(data,&pipefd,&s->s_root->d_inode->i_uid,&s->s_root->d_inode->i_gid,&sbi->oz_pgrp,&minproto,&maxproto) ) {
		d_delete(s->s_root);
		s->s_dev = 0;
		kfree(sbi);
		printk("autofs: called with bogus options\n");
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	if ( minproto > AUTOFS_PROTO_VERSION || maxproto < AUTOFS_PROTO_VERSION ) {
		d_delete(s->s_root);
		s->s_dev = 0;
		kfree(sbi);
		printk("autofs: kernel does not match daemon version\n");
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	DPRINTK(("autofs: pipe fd = %d, pgrp = %u\n", pipefd, sbi->oz_pgrp));
	sbi->pipe = fget(pipefd);
	if ( !sbi->pipe || !sbi->pipe->f_op || !sbi->pipe->f_op->write ) {
		if ( sbi->pipe ) {
			fput(sbi->pipe, sbi->pipe->f_inode);
			printk("autofs: pipe file descriptor does not contain proper ops\n");
		} else {
			printk("autofs: could not open pipe file descriptor\n");
		}
		d_delete(s->s_root);
		s->s_dev = 0;
		kfree(sbi);
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	return s;
}

static int autofs_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	struct statfs tmp;

	tmp.f_type = AUTOFS_SUPER_MAGIC;
	tmp.f_bsize = 1024;
	tmp.f_blocks = 0;
	tmp.f_bfree = 0;
	tmp.f_bavail = 0;
	tmp.f_files = 0;
	tmp.f_ffree = 0;
	tmp.f_namelen = NAME_MAX;
	return copy_to_user(buf, &tmp, bufsiz) ? -EFAULT : 0;
}

static void autofs_read_inode(struct inode *inode)
{
	ino_t ino = inode->i_ino;
	unsigned int n;
	struct autofs_sb_info *sbi =
		(struct autofs_sb_info *) inode->i_sb->u.generic_sbp;

	inode->i_op = NULL;
	inode->i_mode = 0;
	inode->i_nlink = 2;
	inode->i_size = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_blocks = 0;
	inode->i_blksize = 1024;

	if ( ino == AUTOFS_ROOT_INO ) {
		inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
		inode->i_op = &autofs_root_inode_operations;
		inode->i_uid = inode->i_gid = 0; /* Changed in read_super */
		return;
	} 
	
	inode->i_uid = inode->i_sb->s_root->d_inode->i_uid;
	inode->i_gid = inode->i_sb->s_root->d_inode->i_gid;
	
	if ( ino >= AUTOFS_FIRST_SYMLINK && ino < AUTOFS_FIRST_DIR_INO ) {
		/* Symlink inode - should be in symlink list */
		struct autofs_symlink *sl;

		n = ino - AUTOFS_FIRST_SYMLINK;
		if ( n >= AUTOFS_MAX_SYMLINKS || !test_bit(n,sbi->symlink_bitmap)) {
			printk("autofs: Looking for bad symlink inode 0x%08x\n", (unsigned int) ino);
			return;
		}
		
		inode->i_op = &autofs_symlink_inode_operations;
		sl = &sbi->symlink[n];
		inode->u.generic_ip = sl;
		inode->i_mode = S_IFLNK | S_IRWXUGO;
		inode->i_mtime = inode->i_ctime = sl->mtime;
		inode->i_size = sl->len;
		inode->i_nlink = 1;
	} else {
		/* All non-root directory inodes look the same */
		inode->i_op = &autofs_dir_inode_operations;
		inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO;
	}
}

static void autofs_write_inode(struct inode *inode)
{
}

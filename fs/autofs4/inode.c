/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/autofs/inode.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
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

static void ino_lnkfree(struct autofs_info *ino)
{
	if (ino->u.symlink) {
		kfree(ino->u.symlink);
		ino->u.symlink = NULL;
	}
}

struct autofs_info *autofs4_init_ino(struct autofs_info *ino,
				     struct autofs_sb_info *sbi, mode_t mode)
{
	int reinit = 1;

	if (ino == NULL) {
		reinit = 0;
		ino = kmalloc(sizeof(*ino), GFP_KERNEL);
	}

	if (ino == NULL)
		return NULL;

	ino->flags = 0;
	ino->ino = sbi->next_ino++;
	ino->mode = mode;
	ino->inode = NULL;
	ino->dentry = NULL;
	ino->size = 0;

	ino->last_used = jiffies;

	ino->sbi = sbi;
	INIT_LIST_HEAD(&ino->ino_hash);

	if (reinit && ino->free)
		(ino->free)(ino);

	memset(&ino->u, 0, sizeof(ino->u));

	ino->free = NULL;

	if (S_ISLNK(mode))
		ino->free = ino_lnkfree;

	return ino;
}

void autofs4_free_ino(struct autofs_info *ino)
{
	autofs4_ihash_delete(ino);
	if (ino->dentry) {
		ino->dentry->d_fsdata = NULL;
		if (ino->dentry->d_inode)
			dput(ino->dentry);
		ino->dentry = NULL;
	}
	if (ino->free)
		(ino->free)(ino);
	kfree(ino);
}

/*
 * Dummy functions - do we ever actually want to do
 * something here?
 */
static void autofs4_put_inode(struct inode *inode)
{
}

static void autofs4_clear_inode(struct inode *inode)
{
}

static void autofs4_put_super(struct super_block *sb)
{
	struct autofs_sb_info *sbi = autofs4_sbi(sb);

	sb->u.generic_sbp = NULL;

	if ( !sbi->catatonic )
		autofs4_catatonic_mode(sbi); /* Free wait queues, close pipe */

	kfree(sbi);

	DPRINTK(("autofs: shutting down\n"));
}

static void autofs4_umount_begin(struct super_block *sb)
{
	struct autofs_sb_info *sbi = autofs4_sbi(sb);

	if (!sbi->catatonic)
		autofs4_catatonic_mode(sbi);
}

static int autofs4_statfs(struct super_block *sb, struct statfs *buf);
static void autofs4_read_inode(struct inode *inode);
static void autofs4_write_inode(struct inode *inode);

static struct super_operations autofs4_sops = {
	read_inode:	autofs4_read_inode,
	write_inode:	autofs4_write_inode,
	put_inode:	autofs4_put_inode,
	clear_inode:	autofs4_clear_inode,
	put_super:	autofs4_put_super,
	statfs:		autofs4_statfs,
	umount_begin:	autofs4_umount_begin,
};

static int parse_options(char *options, int *pipefd, uid_t *uid, gid_t *gid,
			 pid_t *pgrp, int *minproto, int *maxproto)
{
	char *this_char, *value;
	
	*uid = current->uid;
	*gid = current->gid;
	*pgrp = current->pgrp;

	*minproto = AUTOFS_MIN_PROTO_VERSION;
	*maxproto = AUTOFS_MAX_PROTO_VERSION;

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

static struct autofs_info *autofs4_mkroot(struct autofs_sb_info *sbi)
{
	struct autofs_info *ino;

	ino = autofs4_init_ino(NULL, sbi, S_IFDIR | 0755);
	if (!ino)
		return NULL;

	ino->ino = AUTOFS_ROOT_INO;
	
	return ino;
}

struct super_block *autofs4_read_super(struct super_block *s, void *data,
				      int silent)
{
	struct inode * root_inode;
	struct dentry * root;
	struct file * pipe;
	int pipefd;
	struct autofs_sb_info *sbi;
	int minproto, maxproto;

	/* Super block already completed? */
	if (s->s_root)
		goto out_unlock;

	sbi = (struct autofs_sb_info *) kmalloc(sizeof(*sbi), GFP_KERNEL);
	if ( !sbi )
		goto fail_unlock;
	DPRINTK(("autofs: starting up, sbi = %p\n",sbi));

	memset(sbi, 0, sizeof(*sbi));

	s->u.generic_sbp = sbi;
	sbi->magic = AUTOFS_SBI_MAGIC;
	sbi->catatonic = 0;
	sbi->exp_timeout = 0;
	sbi->oz_pgrp = current->pgrp;
	sbi->sb = s;
	sbi->version = 0;
	autofs4_init_ihash(&sbi->ihash);
	sbi->queues = NULL;
	sbi->next_ino = AUTOFS_FIRST_INO;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = AUTOFS_SUPER_MAGIC;
	s->s_op = &autofs4_sops;
	s->s_root = NULL;

	/*
	 * Get the root inode and dentry, but defer checking for errors.
	 */
	autofs4_ihash_insert(&sbi->ihash, autofs4_mkroot(sbi));

	root_inode = iget(s, AUTOFS_ROOT_INO);
	root = d_alloc_root(root_inode);
	pipe = NULL;

	/*
	 * Check whether somebody else completed the super block.
	 */
	if (s->s_root)
		goto out_dput;

	if (!root)
		goto fail_iput;

	/* Can this call block? */
	if (parse_options(data, &pipefd,
			  &root_inode->i_uid, &root_inode->i_gid,
			  &sbi->oz_pgrp,
			  &minproto, &maxproto)) {
		printk("autofs: called with bogus options\n");
		goto fail_dput;
	}

	/* Couldn't this be tested earlier? */
	if (maxproto < AUTOFS_MIN_PROTO_VERSION ||
	    minproto > AUTOFS_MAX_PROTO_VERSION) {
		printk("autofs: kernel does not match daemon version "
		       "daemon (%d, %d) kernel (%d, %d)\n",
			minproto, maxproto,
			AUTOFS_MIN_PROTO_VERSION, AUTOFS_MAX_PROTO_VERSION);
		goto fail_dput;
	}

	sbi->version = maxproto > AUTOFS_MAX_PROTO_VERSION ? AUTOFS_MAX_PROTO_VERSION : maxproto;

	DPRINTK(("autofs: pipe fd = %d, pgrp = %u\n", pipefd, sbi->oz_pgrp));
	pipe = fget(pipefd);
	/*
	 * Check whether somebody else completed the super block.
	 */
	if (s->s_root)
		goto out_fput;
	
	if ( !pipe ) {
		printk("autofs: could not open pipe file descriptor\n");
		goto fail_dput;
	}
	if ( !pipe->f_op || !pipe->f_op->write )
		goto fail_fput;
	sbi->pipe = pipe;

	/*
	 * Success! Install the root dentry now to indicate completion.
	 */
	s->s_root = root;
	return s;

	/*
	 * Success ... somebody else completed the super block for us. 
	 */ 
out_unlock:
	goto out_dec;
out_fput:
	if (pipe)
		fput(pipe);
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
fail_fput:
	printk("autofs: pipe file descriptor does not contain proper ops\n");
	/*
	 * fput() can block, so we clear the super block first.
	 */
	fput(pipe);
	/* fall through */
fail_dput:
	/*
	 * dput() can block, so we clear the super block first.
	 */
	dput(root);
	goto fail_free;
fail_iput:
	printk("autofs: get root dentry failed\n");
	/*
	 * iput() can block, so we clear the super block first.
	 */
	iput(root_inode);
fail_free:
	kfree(sbi);
fail_unlock:
	return NULL;
}

static int autofs4_statfs(struct super_block *sb, struct statfs *buf)
{
	buf->f_type = AUTOFS_SUPER_MAGIC;
	buf->f_bsize = 1024;
	buf->f_bfree = 0;
	buf->f_bavail = 0;
	buf->f_ffree = 0;
	buf->f_namelen = NAME_MAX;
	return 0;
}

static void autofs4_read_inode(struct inode *inode)
{
	struct autofs_sb_info *sbi = autofs4_sbi(inode->i_sb);
	struct autofs_info *inf;

	inf = autofs4_ihash_find(&sbi->ihash, inode->i_ino);

	if (inf == NULL || inf->inode != NULL)
		return;

	inode->i_mode = inf->mode;
	inode->i_mtime = inode->i_ctime = inode->i_atime = CURRENT_TIME;
	inode->i_size = inf->size;

	inode->i_blocks = 0;
	inode->i_blksize = 0;
	inode->i_nlink = 1;

	if (inode->i_sb->s_root) {
		inode->i_uid = inode->i_sb->s_root->d_inode->i_uid;
		inode->i_gid = inode->i_sb->s_root->d_inode->i_gid;
	} else {
		inode->i_uid = 0;
		inode->i_gid = 0;
	}

	inf->inode = inode;

	if (S_ISDIR(inf->mode)) {
		inode->i_nlink = 2;
		if (inode->i_ino == AUTOFS_ROOT_INO) {
			inode->i_op = &autofs4_root_inode_operations;
			inode->i_fop = &autofs4_root_operations;
		} else {
			inode->i_op = &autofs4_dir_inode_operations;
			inode->i_fop = &autofs4_dir_operations;
		}
	} else if (S_ISLNK(inf->mode)) {
		inode->i_op = &autofs4_symlink_inode_operations;
	}
}

static void autofs4_write_inode(struct inode *inode)
{
}

/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/autofs/dir.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include "autofs_i.h"

static int autofs_dir_readdir(struct file *filp,
			       void *dirent, filldir_t filldir)
{
	struct inode *inode=filp->f_dentry->d_inode;

	switch((unsigned long) filp->f_pos)
	{
	case 0:
		if (filldir(dirent, ".", 1, 0, inode->i_ino) < 0)
			return 0;
		filp->f_pos++;
		/* fall through */
	case 1:
		if (filldir(dirent, "..", 2, 1, AUTOFS_ROOT_INO) < 0)
			return 0;
		filp->f_pos++;
		/* fall through */
	}
	return 1;
}

/*
 * No entries except for "." and "..", both of which are handled by the VFS layer
 */
static struct dentry *autofs_dir_lookup(struct inode *dir,struct dentry *dentry)
{
	d_add(dentry, NULL);
	return NULL;
}

struct file_operations autofs_dir_operations = {
	read:		generic_read_dir,
	readdir:	autofs_dir_readdir,
};

struct inode_operations autofs_dir_inode_operations = {
	lookup:		autofs_dir_lookup,
};


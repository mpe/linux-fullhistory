/*
 *  linux/fs/adfs/namei.c
 *
 * Copyright (C) 1997 Russell King
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/adfs_fs.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>

/*
 * NOTE! unlike strncmp, ext2_match returns 1 for success, 0 for failure
 */
static int adfs_match (int len, const char * const name, struct adfs_idir_entry *de)
{
	int i;

	if (!de || len > ADFS_NAME_LEN)
		return 0;
	/*
	 * "" means "." ---> so paths like "/usr/lib//libc.a" work
	 */
	if (!len && de->name_len == 1 && de->name[0] == '.' &&
	    de->name[1] == '\0')
		return 1;
	if (len != de->name_len)
		return 0;

	for (i = 0; i < len; i++)
		if ((de->name[i] ^ name[i]) & 0x5f)
			return 0;
	return 1;
}

static int adfs_find_entry (struct inode *dir, const char * const name, int namelen,
			    struct adfs_idir_entry *ide)
{
	struct super_block *sb;
	struct buffer_head *bh[4];
	union  adfs_dirtail dt;
	unsigned long parent_object_id, dir_object_id;
	int buffers, pos;

	if (!dir || !S_ISDIR(dir->i_mode))
		return 0;

	sb = dir->i_sb;

	if (adfs_inode_validate (dir)) {
		adfs_error (sb, "adfs_find_entry",
			    "invalid inode number: %lu", dir->i_ino);
		return 0;
	}

	if (namelen > ADFS_NAME_LEN)
		return 0;

	if (!(buffers = adfs_dir_read (dir, bh))) {
		adfs_error (sb, "adfs_find_entry", "unable to read directory");
		return 0;
	}

	if (adfs_dir_check (dir, bh, buffers, &dt)) {
		adfs_dir_free (bh, buffers);
		return 0;
	}

	parent_object_id = adfs_val (dt.new.dirparent, 3);
	dir_object_id = adfs_inode_objid (dir);

	if (namelen == 2 && name[0] == '.' && name[1] == '.') {
		ide->name_len = 2;
		ide->name[0] = ide->name[1] = '.';
		ide->name[2] = '\0';
		ide->inode_no = adfs_inode_generate (parent_object_id, 0);
		adfs_dir_free (bh, buffers);
		return 1;
	}

	pos = 5;

	do {
		if (!adfs_dir_get (sb, bh, buffers, pos, dir_object_id, ide))
			break;

		if (adfs_match (namelen, name, ide)) {
			adfs_dir_free (bh, buffers);
			return pos;
		}
		pos += 26;
	} while (1);
	adfs_dir_free (bh, buffers);
	return 0;
}

int adfs_lookup (struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = NULL;
	struct adfs_idir_entry de;
	unsigned long ino;

	if (dentry->d_name.len > ADFS_NAME_LEN)
		return -ENAMETOOLONG;

	if (adfs_find_entry (dir, dentry->d_name.name, dentry->d_name.len, &de)) {
		ino = de.inode_no;
		inode = iget (dir->i_sb, ino);

		if (!inode)
			return -EACCES;
	}
	d_add(dentry, inode);
	return 0;
}

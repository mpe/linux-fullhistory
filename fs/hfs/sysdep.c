/*
 * linux/fs/hfs/sysdep.c
 *
 * Copyright (C) 1996  Paul H. Hargrove
 * This file may be distributed under the terms of the GNU Public License.
 *
 * This file contains the code to do various system dependent things.
 *
 * "XXX" in a comment is a note to myself to consider changing something.
 *
 * In function preconditions the term "valid" applied to a pointer to
 * a structure means that the pointer is non-NULL and the structure it
 * points to has all fields initialized to consistent values.
 */

#include "hfs.h"
#include <linux/hfs_fs_sb.h>
#include <linux/hfs_fs_i.h>
#include <linux/hfs_fs.h>

static int hfs_hash_dentry(struct dentry *, struct qstr *);
static int hfs_compare_dentry(struct dentry *, struct qstr *, struct qstr *);
static void hfs_dentry_iput(struct dentry *, struct inode *);
struct dentry_operations hfs_dentry_operations =
{
	NULL,	                /* d_validate(struct dentry *) */
	hfs_hash_dentry,	/* d_hash */
	hfs_compare_dentry,    	/* d_compare */
	NULL,	                /* d_delete(struct dentry *) */
	NULL,                   /* d_release(struct dentry *) */
	hfs_dentry_iput         /* d_iput(struct dentry *, struct inode *) */
};

/*
 * hfs_buffer_get()
 *
 * Return a buffer for the 'block'th block of the media.
 * If ('read'==0) then the buffer is not read from disk.
 */
hfs_buffer hfs_buffer_get(hfs_sysmdb sys_mdb, int block, int read) {
	hfs_buffer tmp = HFS_BAD_BUFFER;

	if (read) {
		tmp = bread(sys_mdb->s_dev, block, HFS_SECTOR_SIZE);
	} else {
		tmp = getblk(sys_mdb->s_dev, block, HFS_SECTOR_SIZE);
		if (tmp) {
			mark_buffer_uptodate(tmp, 1);
		}
	}
	if (!tmp) {
		hfs_error("hfs_fs: unable to read block 0x%08x from dev %s\n",
			  block, hfs_mdb_name(sys_mdb));
	}

	return tmp;
}

/* dentry case-handling: just lowercase everything */

/* hfs_strhash now uses the same hashing function as the dcache. */
static int hfs_hash_dentry(struct dentry *dentry, struct qstr *this)
{
        struct hfs_name cname;
	
	if ((cname.Len = this->len) > HFS_NAMELEN)
	        return 0;
	
	strncpy(cname.Name, this->name, this->len);
	this->hash = hfs_strhash(&cname);
	return 0;
}

static int hfs_compare_dentry(struct dentry *dentry, struct qstr *a, 
			      struct qstr *b)
{
	struct hfs_name s1, s2;

	if (a->len != b->len) return 1;

	if ((s1.Len = s2.Len = a->len) > HFS_NAMELEN)
	  return 1;

	strncpy(s1.Name, a->name, s1.Len);
	strncpy(s2.Name, b->name, s2.Len);
	return hfs_streq(&s1, &s2);
}

static void hfs_dentry_iput(struct dentry *dentry, struct inode *inode)
{
	struct hfs_cat_entry *entry = HFS_I(inode)->entry;

	entry->sys_entry[HFS_ITYPE_TO_INT(HFS_ITYPE(inode->i_ino))] = NULL;
        hfs_cat_put(entry);
	iput(inode);
}

/*
 *  linux/fs/adfs/inode.c
 *
 * Copyright (C) 1997 Russell King
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/adfs_fs.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/mm.h>

/*
 * Old Inode numbers:
 *  bit 30 - 16		FragID of parent object
 *  bit 15		0			1
 *  bit 14 - 0		FragID of object	Offset into parent FragID
 *
 * New Inode numbers:
 *  Inode = Frag ID of parent (14) + Frag Offset (8) + (index into directory + 1)(8)
 */
#define inode_frag(ino)		((ino) >> 8)
#define inode_idx(ino)		((ino) & 0xff)
#define inode_dirindex(idx)	(((idx) & 0xff) * 26 - 21)

#define frag_id(x)		(((x) >> 8) & 0x7fff)
#define off(x)			(((x) & 0xff) ? ((x) & 0xff) - 1 : 0)

static inline int adfs_inode_validate_no (struct super_block *sb, unsigned int inode_no)
{
	unsigned long max_frag_id;

	max_frag_id = sb->u.adfs_sb.s_map_size * sb->u.adfs_sb.s_ids_per_zone;

	return (inode_no & 0x800000ff) ||
	       (frag_id (inode_frag (inode_no)) > max_frag_id) ||
	       (frag_id (inode_frag (inode_no)) < 2);
}

int adfs_inode_validate (struct inode *inode)
{
	struct super_block *sb = inode->i_sb;

	return adfs_inode_validate_no (sb, inode->i_ino & 0xffffff00) ||
	       adfs_inode_validate_no (sb, inode->u.adfs_i.file_id << 8);
}

unsigned long adfs_inode_generate (unsigned long parent_id, int diridx)
{
	if (!parent_id)
		return -1;

	if (diridx)
		diridx = (diridx + 21) / 26;

	return (parent_id << 8) | diridx;
}

unsigned long adfs_inode_objid (struct inode *inode)
{
	if (adfs_inode_validate (inode)) {
		adfs_error (inode->i_sb, "adfs_inode_objid",
			    "bad inode number: %lu (%X,%X)",
			    inode->i_ino, inode->i_ino, inode->u.adfs_i.file_id);
		return 0;
	}

	return inode->u.adfs_i.file_id;
}

int adfs_bmap (struct inode *inode, int block)
{
	struct super_block *sb = inode->i_sb;
	unsigned int blk;

	if (adfs_inode_validate (inode)) {
		adfs_error (sb, "adfs_bmap",
			    "bad inode number: %lu (%X,%X)",
			    inode->i_ino, inode->i_ino, inode->u.adfs_i.file_id);
		return 0;
	}

	if (frag_id(inode->u.adfs_i.file_id) == ADFS_ROOT_FRAG)
		blk = sb->u.adfs_sb.s_map_block + off(inode_frag (inode->i_ino)) + block;
	else
		blk = adfs_map_lookup (sb, frag_id(inode->u.adfs_i.file_id),
				       off (inode->u.adfs_i.file_id) + block);
	return blk;
}

unsigned int adfs_parent_bmap (struct inode *inode, int block)
{
	struct super_block *sb = inode->i_sb;
	unsigned int blk, fragment;

	if (adfs_inode_validate_no (sb, inode->i_ino & 0xffffff00)) {
		adfs_error (sb, "adfs_parent_bmap",
			    "bad inode number: %lu (%X,%X)",
			    inode->i_ino, inode->i_ino, inode->u.adfs_i.file_id);
		return 0;
	}

	fragment = inode_frag (inode->i_ino);
	if (frag_id (fragment) == ADFS_ROOT_FRAG)
		blk = sb->u.adfs_sb.s_map_block + off (fragment) + block;
	else
		blk = adfs_map_lookup (sb, frag_id (fragment), off (fragment) + block);
	return blk;	
}

static int adfs_atts2mode (unsigned char mode, unsigned int filetype)
{
	int omode = 0;

	if (filetype == 0xfc0 /* LinkFS */) {
		omode = S_IFLNK|S_IRUSR|S_IWUSR|S_IXUSR|
				S_IRGRP|S_IWGRP|S_IXGRP|
				S_IROTH|S_IWOTH|S_IXOTH;
	} else {
		if (mode & ADFS_NDA_DIRECTORY)
			omode |= S_IFDIR|S_IRUSR|S_IXUSR|S_IXGRP|S_IXOTH;
		else
			omode |= S_IFREG;
		if (mode & ADFS_NDA_OWNER_READ) {
			omode |= S_IRUSR;
			if (filetype == 0xfe6 /* UnixExec */)
				omode |= S_IXUSR;
		}
		if (mode & ADFS_NDA_OWNER_WRITE)
			omode |= S_IWUSR;
		if (mode & ADFS_NDA_PUBLIC_READ) {
			omode |= S_IRGRP | S_IROTH;
			if (filetype == 0xfe6)
				omode |= S_IXGRP | S_IXOTH;
		}
		if (mode & ADFS_NDA_PUBLIC_WRITE)
			omode |= S_IWGRP | S_IWOTH;
	}
	return omode;
}

void adfs_read_inode (struct inode *inode)
{
	struct super_block *sb;
	struct buffer_head *bh[4];
	struct adfs_idir_entry ide;
	int buffers;

	sb = inode->i_sb;
	inode->i_uid = 0;
	inode->i_gid = 0;
	inode->i_version = ++event;

	if (adfs_inode_validate_no (sb, inode->i_ino & 0xffffff00)) {
		adfs_error (sb, "adfs_read_inode",
			    "bad inode number: %lu", inode->i_ino);
		goto bad;
	}

	if (frag_id(inode_frag (inode->i_ino)) == ADFS_ROOT_FRAG &&
	    inode_idx (inode->i_ino) == 0) {
		/* root dir */
		inode->i_mode	 = S_IRWXUGO | S_IFDIR;
		inode->i_nlink	 = 2;
		inode->i_size	 = ADFS_NEWDIR_SIZE;
		inode->i_blksize = PAGE_SIZE;
		inode->i_blocks  = inode->i_size / sb->s_blocksize;
		inode->i_mtime   =
		inode->i_atime   =
		inode->i_ctime   = 0;
		inode->u.adfs_i.file_id = inode_frag (inode->i_ino);
	} else {
		if (!(buffers = adfs_dir_read_parent (inode, bh)))
			goto bad;

		if (adfs_dir_check (inode, bh, buffers, NULL)) {
			adfs_dir_free (bh, buffers);
			goto bad;
		}

		if (!adfs_dir_find_entry (sb, bh, buffers, inode_dirindex (inode->i_ino), &ide)) {
			adfs_dir_free (bh, buffers);
			goto bad;
		}
		adfs_dir_free (bh, buffers);
		inode->i_mode	 = adfs_atts2mode (ide.mode, ide.filetype);
		inode->i_nlink	 = 2;
		inode->i_size    = ide.size;
		inode->i_blksize = PAGE_SIZE;
		inode->i_blocks	 = (inode->i_size + sb->s_blocksize - 1) >> sb->s_blocksize_bits;
		inode->i_mtime	 =
		inode->i_atime   =
		inode->i_ctime   = ide.mtime;
		inode->u.adfs_i.file_id = ide.file_id;
	}

	if (S_ISDIR(inode->i_mode))
		inode->i_op	 = &adfs_dir_inode_operations;
	else if (S_ISREG(inode->i_mode))
		inode->i_op	 = &adfs_file_inode_operations;
	return;

bad:
	inode->i_mode	 = 0;
	inode->i_nlink	 = 1;
	inode->i_size    = 0;
	inode->i_blksize = 0;
	inode->i_blocks  = 0;
	inode->i_mtime   =
	inode->i_atime   =
	inode->i_ctime   = 0;
	inode->i_op	 = NULL;
}

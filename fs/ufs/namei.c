/*
 * linux/fs/ufs/namei.c
 *
 * Copyright (C) 1998
 * Daniel Pirkl <daniel.pirkl@email.cz>
 * Charles University, Faculty of Mathematics and Physics
 *
 *  from
 *
 *  linux/fs/ext2/namei.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/quotaops.h>

#include "swab.h"
#include "util.h"

#undef UFS_NAMEI_DEBUG

#ifdef UFS_NAMEI_DEBUG
#define UFSD(x) printk("(%s, %d), %s: ", __FILE__, __LINE__, __FUNCTION__); printk x;
#else
#define UFSD(x)
#endif

/*
 * define how far ahead to read directories while searching them.
 */
#define NAMEI_RA_CHUNKS  2
#define NAMEI_RA_BLOCKS  4
#define NAMEI_RA_SIZE        (NAMEI_RA_CHUNKS * NAMEI_RA_BLOCKS)
#define NAMEI_RA_INDEX(c,b)  (((c) * NAMEI_RA_BLOCKS) + (b))

/*
 * NOTE! unlike strncmp, ufs_match returns 1 for success, 0 for failure.
 *
 * len <= UFS_MAXNAMLEN and de != NULL are guaranteed by caller.
 */
static inline int ufs_match (int len, const char * const name,
	struct ufs_dir_entry * de, unsigned flags, unsigned swab)
{
	if (len != ufs_get_de_namlen(de))
		return 0;
	if (!de->d_ino)
		return 0;
	return !memcmp(name, de->d_name, len);
}

/*
 *	ufs_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 */
static struct buffer_head * ufs_find_entry (struct inode * dir,
	const char * const name, int namelen, struct ufs_dir_entry ** res_dir)
{
	struct super_block * sb;
	struct buffer_head * bh_use[NAMEI_RA_SIZE];
	struct buffer_head * bh_read[NAMEI_RA_SIZE];
	unsigned long offset;
	int block, toread, i, err;
	unsigned flags, swab;

	UFSD(("ENTER, dir_ino %lu, name %s, namlen %u\n", dir->i_ino, name, namelen))
	
	*res_dir = NULL;
	if (!dir) 
		return NULL;
	
	sb = dir->i_sb;
	flags = sb->u.ufs_sb.s_flags;
	swab = sb->u.ufs_sb.s_swab;
	
	if (namelen > UFS_MAXNAMLEN)
		return NULL;

	memset (bh_use, 0, sizeof (bh_use));
	toread = 0;
	for (block = 0; block < NAMEI_RA_SIZE; ++block) {
		struct buffer_head * bh;

		if ((block << sb->s_blocksize_bits) >= dir->i_size)
			break;
		bh = ufs_getfrag (dir, block, 0, &err);
		bh_use[block] = bh;
		if (bh && !buffer_uptodate(bh))
			bh_read[toread++] = bh;
	}

	for (block = 0, offset = 0; offset < dir->i_size; block++) {
		struct buffer_head * bh;
		struct ufs_dir_entry * de;
		char * dlimit;

		if ((block % NAMEI_RA_BLOCKS) == 0 && toread) {
			ll_rw_block (READ, toread, bh_read);
			toread = 0;
		}
		bh = bh_use[block % NAMEI_RA_SIZE];
		if (!bh) {
			ufs_error (sb, "ufs_find_entry", 
				"directory #%lu contains a hole at offset %lu",
				dir->i_ino, offset);
			offset += sb->s_blocksize;
			continue;
		}
		wait_on_buffer (bh);
		if (!buffer_uptodate(bh)) {
			/*
			 * read error: all bets are off
			 */
			break;
		}

		de = (struct ufs_dir_entry *) bh->b_data;
		dlimit = bh->b_data + sb->s_blocksize;
		while ((char *) de < dlimit && offset < dir->i_size) {
			/* this code is executed quadratically often */
			/* do minimal checking by hand */
			int de_len;

			if ((char *) de + namelen <= dlimit &&
			    ufs_match (namelen, name, de, flags, swab)) {
				/* found a match -
				just to be sure, do a full check */
				if (!ufs_check_dir_entry("ufs_find_entry",
				    dir, de, bh, offset))
					goto failed;
				for (i = 0; i < NAMEI_RA_SIZE; ++i) {
					if (bh_use[i] != bh)
						brelse (bh_use[i]);
				}
				*res_dir = de;
				return bh;
			}
                        /* prevent looping on a bad block */
			de_len = SWAB16(de->d_reclen);
			if (de_len <= 0)
				goto failed;
			offset += de_len;
			de = (struct ufs_dir_entry *) ((char *) de + de_len);
		}

		brelse (bh);
		if (((block + NAMEI_RA_SIZE) << sb->s_blocksize_bits ) >=
		    dir->i_size)
			bh = NULL;
		else
			bh = ufs_getfrag (dir, block + NAMEI_RA_SIZE, 0, &err);
		bh_use[block % NAMEI_RA_SIZE] = bh;
		if (bh && !buffer_uptodate(bh))
			bh_read[toread++] = bh;
	}

failed:
	for (i = 0; i < NAMEI_RA_SIZE; ++i) brelse (bh_use[i]);
	UFSD(("EXIT\n"))
	return NULL;
}

int ufs_lookup(struct inode * dir, struct dentry *dentry)
{
	struct super_block * sb;
	struct inode * inode;
	struct ufs_dir_entry * de;
	struct buffer_head * bh;
	unsigned swab;
	
	UFSD(("ENTER\n"))
	
	sb = dir->i_sb;
	swab = sb->u.ufs_sb.s_swab;
	
	if (dentry->d_name.len > UFS_MAXNAMLEN)
		return -ENAMETOOLONG;

	bh = ufs_find_entry (dir, dentry->d_name.name, dentry->d_name.len, &de);
	inode = NULL;
	if (bh) {
		unsigned long ino = SWAB32(de->d_ino);
		brelse (bh);
		inode = iget(sb, ino);
		if (!inode) 
			return -EACCES;
	}
	d_add(dentry, inode);
	UFSD(("EXIT\n"))
	return 0;
}

/*
 *	ufs_add_entry()
 *
 * adds a file entry to the specified directory, using the same
 * semantics as ufs_find_entry(). It returns NULL if it failed.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
static struct buffer_head * ufs_add_entry (struct inode * dir,
	const char * name, int namelen, struct ufs_dir_entry ** res_dir, 
	int *err )
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	unsigned long offset;
	unsigned fragoff;
	unsigned short rec_len;
	struct buffer_head * bh;
	struct ufs_dir_entry * de, * de1;
	unsigned flags, swab;

	UFSD(("ENTER, name %s, namelen %u\n", name, namelen))
	
	*err = -EINVAL;
	*res_dir = NULL;
	if (!dir || !dir->i_nlink)
		return NULL;
		
	sb = dir->i_sb;
	flags = sb->u.ufs_sb.s_flags;
	swab = sb->u.ufs_sb.s_swab;
	uspi = sb->u.ufs_sb.s_uspi;

	if (namelen > UFS_MAXNAMLEN)
	{
		*err = -ENAMETOOLONG;
		return NULL;
	}

	if (!namelen)
		return NULL;
	/*
	 * Is this a busy deleted directory?  Can't create new files if so
	 */
	if (dir->i_size == 0)
	{
		*err = -ENOENT;
		return NULL;
	}
	bh = ufs_bread (dir, 0, 0, err);
	if (!bh)
		return NULL;
	rec_len = UFS_DIR_REC_LEN(namelen);
	offset = 0;
	de = (struct ufs_dir_entry *) bh->b_data;
	*err = -ENOSPC;
	while (1) {
		if ((char *)de >= UFS_SECTOR_SIZE + bh->b_data) {
			fragoff = offset & ~uspi->s_fmask;
			if (fragoff != 0 && fragoff != UFS_SECTOR_SIZE)
				ufs_error (sb, "ufs_add_entry", "internal error"
					" fragoff %u", fragoff);
			if (!fragoff) {
				brelse (bh);
				bh = NULL;
				bh = ufs_bread (dir, offset >> sb->s_blocksize_bits, 1, err);
			}
			if (!bh)
				return NULL;
			if (dir->i_size <= offset) {
				if (dir->i_size == 0) {
					*err = -ENOENT;
					return NULL;
				}
				de = (struct ufs_dir_entry *) (bh->b_data + fragoff);
				de->d_ino = SWAB32(0);
				de->d_reclen = SWAB16(UFS_SECTOR_SIZE);
				ufs_set_de_namlen(de,0);
				dir->i_size = offset + UFS_SECTOR_SIZE;
				mark_inode_dirty(dir);
			} else {
				de = (struct ufs_dir_entry *) bh->b_data;
			}
		}
		if (!ufs_check_dir_entry ("ufs_add_entry", dir, de, bh, offset)) {
			*err = -ENOENT;
			brelse (bh);
			return NULL;
		}
		if (ufs_match (namelen, name, de, flags, swab)) {
				*err = -EEXIST;
				brelse (bh);
				return NULL;
		}
		if ((SWAB32(de->d_ino) == 0 && SWAB16(de->d_reclen) >= rec_len) ||
		    (SWAB16(de->d_reclen) >= UFS_DIR_REC_LEN(ufs_get_de_namlen(de)) + rec_len)) {
			offset += SWAB16(de->d_reclen);
			if (SWAB32(de->d_ino)) {
				de1 = (struct ufs_dir_entry *) ((char *) de +
					UFS_DIR_REC_LEN(ufs_get_de_namlen(de)));
				de1->d_reclen = SWAB16(SWAB16(de->d_reclen) -
					UFS_DIR_REC_LEN(ufs_get_de_namlen(de)));
				de->d_reclen = SWAB16(UFS_DIR_REC_LEN(ufs_get_de_namlen(de)));
				de = de1;
			}
			de->d_ino = SWAB32(0);
			ufs_set_de_namlen(de, namelen);
			memcpy (de->d_name, name, namelen + 1);
			/*
			 * XXX shouldn't update any times until successful
			 * completion of syscall, but too many callers depend
			 * on this.
			 *
			 * XXX similarly, too many callers depend on
			 * ufs_new_inode() setting the times, but error
			 * recovery deletes the inode, so the worst that can
			 * happen is that the times are slightly out of date
			 * and/or different from the directory change time.
			 */
			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
			mark_inode_dirty(dir);
			dir->i_version = ++event;
			mark_buffer_dirty(bh, 1);
			*res_dir = de;
			*err = 0;
			
			UFSD(("EXIT\n"))
			return bh;
		}
		offset += SWAB16(de->d_reclen);
		de = (struct ufs_dir_entry *) ((char *) de + SWAB16(de->d_reclen));
	}
	brelse (bh);
	UFSD(("EXIT (FAILED)\n"))
	return NULL;
}

/*
 * ufs_delete_entry deletes a directory entry by merging it with the
 * previous entry.
 */
static int ufs_delete_entry (struct inode * inode, struct ufs_dir_entry * dir,
	struct buffer_head * bh )
	
{
	struct super_block * sb;
	struct ufs_dir_entry * de, * pde;
	unsigned i;
	unsigned flags, swab;
	
	UFSD(("ENTER\n"))

	sb = inode->i_sb;
	flags = sb->u.ufs_sb.s_flags;
	swab = sb->u.ufs_sb.s_swab;
	i = 0;
	pde = NULL;
	de = (struct ufs_dir_entry *) bh->b_data;
	
	UFSD(("ino %u, reclen %u, namlen %u, name %s\n", SWAB32(de->d_ino),
		SWAB16(de->d_reclen), ufs_get_de_namlen(de), de->d_name))

	while (i < bh->b_size) {
		if (!ufs_check_dir_entry ("ufs_delete_entry", inode, de, bh, i))
			return -EIO;
		if (de == dir)  {
			if (pde)
				pde->d_reclen =
				    SWAB16(SWAB16(pde->d_reclen) +
				    SWAB16(dir->d_reclen));
			dir->d_ino = SWAB32(0);
			UFSD(("EXIT\n"))
			return 0;
		}
		i += SWAB16(de->d_reclen);
		if (i == UFS_SECTOR_SIZE) pde = NULL;
		else pde = de;
		de = (struct ufs_dir_entry *)
		    ((char *) de + SWAB16(de->d_reclen));
		if (i == UFS_SECTOR_SIZE && SWAB16(de->d_reclen) == 0)
			break;
	}
	UFSD(("EXIT\n"))
	return -ENOENT;
}

/*
 * By the time this is called, we already have created
 * the directory cache entry for the new file, but it
 * is so far negative - it has no inode.
 *
 * If the create succeeds, we fill in the inode information
 * with d_instantiate(). 
 */
int ufs_create (struct inode * dir, struct dentry * dentry, int mode)
{
	struct super_block * sb;
	struct inode * inode;
	struct buffer_head * bh;
	struct ufs_dir_entry * de;
	int err = -EIO;
	unsigned flags, swab;
	
	sb = dir->i_sb;
	swab = sb->u.ufs_sb.s_swab;
	flags = sb->u.ufs_sb.s_flags;
	/*
	 * N.B. Several error exits in ufs_new_inode don't set err.
	 */
	UFSD(("ENTER\n"))
	
	inode = ufs_new_inode (dir, mode, &err);
	if (!inode)
		return err;
	inode->i_op = &ufs_file_inode_operations;
	inode->i_mode = mode;
	mark_inode_dirty(inode);
	bh = ufs_add_entry (dir, dentry->d_name.name, dentry->d_name.len, &de, &err);
	if (!bh) {
		inode->i_nlink--;
		mark_inode_dirty(inode);
		iput (inode);
		return err;
	}
	de->d_ino = SWAB32(inode->i_ino);
	ufs_set_de_type (de, inode->i_mode);
	dir->i_version = ++event;
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(dir)) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	brelse (bh);
	d_instantiate(dentry, inode);
	
	UFSD(("EXIT\n"))
	
	return 0;
}

int ufs_mknod (struct inode * dir, struct dentry *dentry, int mode, int rdev)
{
	struct super_block * sb;
	struct inode * inode;
	struct buffer_head * bh;
	struct ufs_dir_entry * de;
	int err = -EIO;
	unsigned flags, swab;
	
	sb = dir->i_sb;
	flags = sb->u.ufs_sb.s_flags;
	swab = sb->u.ufs_sb.s_swab;
	
	err = -ENAMETOOLONG;
	if (dentry->d_name.len > UFS_MAXNAMLEN)
		goto out;

	inode = ufs_new_inode (dir, mode, &err);
	if (!inode)
		goto out;

	inode->i_uid = current->fsuid;
	inode->i_mode = mode;
	inode->i_op = NULL;
	if (S_ISREG(inode->i_mode))
		inode->i_op = &ufs_file_inode_operations;
	else if (S_ISCHR(inode->i_mode))
		inode->i_op = &chrdev_inode_operations;
	else if (S_ISBLK(inode->i_mode))
		inode->i_op = &blkdev_inode_operations;
	else if (S_ISFIFO(inode->i_mode)) 
		init_fifo(inode);
	if (S_ISBLK(mode) || S_ISCHR(mode))
		inode->i_rdev = to_kdev_t(rdev);
	mark_inode_dirty(inode);
	bh = ufs_add_entry (dir, dentry->d_name.name, dentry->d_name.len, &de, &err);
	if (!bh)
		goto out_no_entry;
	de->d_ino = SWAB32(inode->i_ino);
	ufs_set_de_type (de, inode->i_mode);
	dir->i_version = ++event;
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(dir)) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	d_instantiate(dentry, inode);
	brelse(bh);
	err = 0;
out:
	return err;

out_no_entry:
	inode->i_nlink--;
	mark_inode_dirty(inode);
	iput(inode);
	goto out;
}

int ufs_mkdir(struct inode * dir, struct dentry * dentry, int mode)
{
	struct super_block * sb;
	struct inode * inode;
	struct buffer_head * bh, * dir_block;
	struct ufs_dir_entry * de;
	int err;
	unsigned flags, swab;
	
	sb = dir->i_sb;
	flags = sb->u.ufs_sb.s_flags;
	swab = sb->u.ufs_sb.s_swab;
	
	err = -ENAMETOOLONG;
	if (dentry->d_name.len > UFS_MAXNAMLEN)
		goto out;

	err = -EMLINK;
	if (dir->i_nlink >= UFS_LINK_MAX)
		goto out;
	err = -EIO;
	inode = ufs_new_inode (dir, S_IFDIR, &err);
	if (!inode)
		goto out;

	inode->i_op = &ufs_dir_inode_operations;
	inode->i_size = UFS_SECTOR_SIZE;
	dir_block = ufs_bread (inode, 0, 1, &err);
	if (!dir_block) {
		inode->i_nlink--; /* is this nlink == 0? */
		mark_inode_dirty(inode);
		iput (inode);
		return err;
	}
	inode->i_blocks = sb->s_blocksize / UFS_SECTOR_SIZE;
	de = (struct ufs_dir_entry *) dir_block->b_data;
	de->d_ino = SWAB32(inode->i_ino);
	ufs_set_de_type (de, inode->i_mode);
	ufs_set_de_namlen(de,1);
	de->d_reclen = SWAB16(UFS_DIR_REC_LEN(1));
	strcpy (de->d_name, ".");
	de = (struct ufs_dir_entry *) ((char *) de + SWAB16(de->d_reclen));
	de->d_ino = SWAB32(dir->i_ino);
	ufs_set_de_type (de, dir->i_mode);
	de->d_reclen = SWAB16(UFS_SECTOR_SIZE - UFS_DIR_REC_LEN(1));
	ufs_set_de_namlen(de,2);
	strcpy (de->d_name, "..");
	inode->i_nlink = 2;
	mark_buffer_dirty(dir_block, 1);
	brelse (dir_block);
	inode->i_mode = S_IFDIR | (mode & (S_IRWXUGO|S_ISVTX) & ~current->fs->umask);
	if (dir->i_mode & S_ISGID)
		inode->i_mode |= S_ISGID;
	mark_inode_dirty(inode);
	bh = ufs_add_entry (dir, dentry->d_name.name, dentry->d_name.len, &de, &err);
	if (!bh)
		goto out_no_entry;
	de->d_ino = SWAB32(inode->i_ino);
	ufs_set_de_type (de, inode->i_mode);
	dir->i_version = ++event;
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(dir)) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	dir->i_nlink++;
	mark_inode_dirty(dir);
	d_instantiate(dentry, inode);
	brelse (bh);
	err = 0;
out:
	return err;

out_no_entry:
	inode->i_nlink = 0;
	mark_inode_dirty(inode);
	iput (inode);
	goto out;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
static int ufs_empty_dir (struct inode * inode)
{
	struct super_block * sb;
	unsigned long offset;
	struct buffer_head * bh;
	struct ufs_dir_entry * de, * de1;
	int err;
	unsigned swab;	
	
	sb = inode->i_sb;
	swab = sb->u.ufs_sb.s_swab;

	if (inode->i_size < UFS_DIR_REC_LEN(1) + UFS_DIR_REC_LEN(2) ||
	    !(bh = ufs_bread (inode, 0, 0, &err))) {
	    	ufs_warning (inode->i_sb, "empty_dir",
			      "bad directory (dir #%lu) - no data block",
			      inode->i_ino);
		return 1;
	}
	de = (struct ufs_dir_entry *) bh->b_data;
	de1 = (struct ufs_dir_entry *) ((char *) de + SWAB16(de->d_reclen));
	if (SWAB32(de->d_ino) != inode->i_ino || !SWAB32(de1->d_ino) || 
	    strcmp (".", de->d_name) || strcmp ("..", de1->d_name)) {
	    	ufs_warning (inode->i_sb, "empty_dir",
			      "bad directory (dir #%lu) - no `.' or `..'",
			      inode->i_ino);
		return 1;
	}
	offset = SWAB16(de->d_reclen) + SWAB16(de1->d_reclen);
	de = (struct ufs_dir_entry *) ((char *) de1 + SWAB16(de1->d_reclen));
	while (offset < inode->i_size ) {
		if (!bh || (void *) de >= (void *) (bh->b_data + sb->s_blocksize)) {
			brelse (bh);
			bh = ufs_bread (inode, offset >> sb->s_blocksize_bits, 1, &err);
	 		if (!bh) {
				ufs_error (sb, "empty_dir",
					    "directory #%lu contains a hole at offset %lu",
					    inode->i_ino, offset);
				offset += sb->s_blocksize;
				continue;
			}
			de = (struct ufs_dir_entry *) bh->b_data;
		}
		if (!ufs_check_dir_entry ("empty_dir", inode, de, bh, offset)) {
			brelse (bh);
			return 1;
		}
		if (SWAB32(de->d_ino)) {
			brelse (bh);
			return 0;
		}
		offset += SWAB16(de->d_reclen);
		de = (struct ufs_dir_entry *) ((char *) de + SWAB16(de->d_reclen));
	}
	brelse (bh);
	return 1;
}

int ufs_rmdir (struct inode * dir, struct dentry *dentry)
{
	struct super_block *sb;
	int retval;
	struct inode * inode;
	struct buffer_head * bh;
	struct ufs_dir_entry * de;
	unsigned swab;
	
	sb = dir->i_sb;
	swab = sb->u.ufs_sb.s_swab;
		
	UFSD(("ENTER\n"))
	
	retval = -ENAMETOOLONG;
	if (dentry->d_name.len > UFS_MAXNAMLEN)
		goto out;

	retval = -ENOENT;
	bh = ufs_find_entry (dir, dentry->d_name.name, dentry->d_name.len, &de);
	if (!bh)
		goto end_rmdir;

	inode = dentry->d_inode;
	DQUOT_INIT(inode);

	retval = -EIO;
	if (SWAB32(de->d_ino) != inode->i_ino)
		goto end_rmdir;

	if (!ufs_empty_dir (inode))
		retval = -ENOTEMPTY;
	else if (SWAB32(de->d_ino) != inode->i_ino)
		retval = -ENOENT;
	else {
		retval = ufs_delete_entry (dir, de, bh);
		dir->i_version = ++event;
	}
	if (retval)
		goto end_rmdir;
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(dir)) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	if (inode->i_nlink != 2)
		ufs_warning (inode->i_sb, "ufs_rmdir",
			      "empty directory has nlink!=2 (%d)",
			      inode->i_nlink);
	inode->i_version = ++event;
	inode->i_nlink = 0;
	inode->i_size = 0;
	mark_inode_dirty(inode);
	dir->i_nlink--;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(dir);
	d_delete(dentry);

end_rmdir:
	brelse (bh);
out:
	UFSD(("EXIT\n"))
	
	return retval;
}

int ufs_unlink(struct inode * dir, struct dentry *dentry)
{
	struct super_block * sb;
	int retval;
	struct inode * inode;
	struct buffer_head * bh;
	struct ufs_dir_entry * de;
	unsigned flags, swab;

	sb = dir->i_sb;
	flags = sb->u.ufs_sb.s_flags;
	swab = sb->u.ufs_sb.s_swab;
		
	retval = -ENAMETOOLONG;
	if (dentry->d_name.len > UFS_MAXNAMLEN)
		goto out;

	retval = -ENOENT;
	bh = ufs_find_entry (dir, dentry->d_name.name, dentry->d_name.len, &de);
	UFSD(("de: ino %u, reclen %u, namelen %u, name %s\n", SWAB32(de->d_ino),
		SWAB16(de->d_reclen), ufs_get_de_namlen(de), de->d_name))
	if (!bh)
		goto end_unlink;

	inode = dentry->d_inode;
	DQUOT_INIT(inode);

	retval = -EIO;
	if (SWAB32(de->d_ino) != inode->i_ino)
		goto end_unlink;
	
	if (!inode->i_nlink) {
		ufs_warning (inode->i_sb, "ufs_unlink",
			      "Deleting nonexistent file (%lu), %d",
			      inode->i_ino, inode->i_nlink);
		inode->i_nlink = 1;
	}
	retval = ufs_delete_entry (dir, de, bh);
	if (retval)
		goto end_unlink;
	dir->i_version = ++event;
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(dir)) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(dir);
	inode->i_nlink--;
	mark_inode_dirty(inode);
	inode->i_ctime = dir->i_ctime;
	retval = 0;
	d_delete(dentry);	/* This also frees the inode */

end_unlink:
	brelse (bh);
out:
	return retval;
}


/*
 * Create symbolic link. We use only slow symlinks at this time.
 */
int ufs_symlink (struct inode * dir, struct dentry * dentry,
	const char * symname)
{
	struct super_block * sb;
	struct ufs_dir_entry * de;
	struct inode * inode;
	struct buffer_head * bh, * name_block;
	char * link;
	unsigned i, l;
	int err;
	char c;
	unsigned swab;
	
	UFSD(("ENTER\n"))
	
	sb = dir->i_sb;
	swab = sb->u.ufs_sb.s_swab;
	bh = name_block = NULL;
	err = -EIO;
	
	if (!(inode = ufs_new_inode (dir, S_IFLNK, &err))) {
		return err;
	}
	inode->i_mode = S_IFLNK | S_IRWXUGO;
	inode->i_op = &ufs_symlink_inode_operations;
	for (l = 0; l < sb->s_blocksize - 1 && symname [l]; l++);

	/***if (l >= sizeof (inode->u.ufs_i.i_data)) {***/
	if (1) {
		/* slow symlink */
		name_block = ufs_bread (inode, 0, 1, &err);
		if (!name_block) {
			inode->i_nlink--;
			mark_inode_dirty(inode);
			iput (inode);
			return err;
		}
		link = name_block->b_data;
		
	} else {
		/* fast symlink */
		link = (char *) inode->u.ufs_i.i_u1.i_data;
	}
	i = 0;
	while (i < sb->s_blocksize - 1 && (c = *(symname++)))
		link[i++] = c;
	link[i] = 0;
	if (name_block) {
		mark_buffer_dirty(name_block, 1);
		brelse (name_block);
	}
	inode->i_size = i;
	mark_inode_dirty(inode);

	bh = ufs_add_entry (dir, dentry->d_name.name, dentry->d_name.len, &de, &err);
	if (!bh)
		goto out_no_entry;
	de->d_ino = SWAB32(inode->i_ino);
	dir->i_version = ++event;
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(dir)) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	brelse (bh);
	d_instantiate(dentry, inode);
	err = 0;
out:
	return err;

out_no_entry:
	inode->i_nlink--;
	mark_inode_dirty(inode);
	iput (inode);
	goto out;
}

int ufs_link (struct dentry * old_dentry, struct inode * dir,
	struct dentry *dentry)
{
	struct super_block * sb;
	struct inode *inode = old_dentry->d_inode;
	struct ufs_dir_entry * de;
	struct buffer_head * bh;
	int err;
	unsigned swab;

	inode = old_dentry->d_inode;
	sb = inode->i_sb;
	swab = sb->u.ufs_sb.s_swab;
	
	if (S_ISDIR(inode->i_mode))
		return -EPERM;

	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return -EPERM;

	if (inode->i_nlink >= UFS_LINK_MAX)
		return -EMLINK;

	bh = ufs_add_entry (dir, dentry->d_name.name, dentry->d_name.len, &de, &err);
	if (!bh)
		return err;

	de->d_ino = SWAB32(inode->i_ino);
	dir->i_version = ++event;
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(dir)) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	brelse (bh);
	inode->i_nlink++;
	inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
	inode->i_count++;
	d_instantiate(dentry, inode);
	return 0;
}


#define PARENT_INO(buffer) \
	((struct ufs_dir_entry *) ((char *) buffer + \
	SWAB16(((struct ufs_dir_entry *) buffer)->d_reclen)))->d_ino
/*
 * rename uses retrying to avoid race-conditions: at least they should be
 * minimal.
 * it tries to allocate all the blocks, then sanity-checks, and if the sanity-
 * checks fail, it tries to restart itself again. Very practical - no changes
 * are done until we know everything works ok.. and then all the changes can be
 * done in one fell swoop when we have claimed all the buffers needed.
 *
 * Anybody can rename anything with this: the permission checks are left to the
 * higher-level routines.
 */
static int do_ufs_rename (struct inode * old_dir, struct dentry * old_dentry,
	struct inode * new_dir,	struct dentry * new_dentry )
{
	struct super_block * sb;
	struct inode * old_inode, * new_inode;
	struct buffer_head * old_bh, * new_bh, * dir_bh;
	struct ufs_dir_entry * old_de, * new_de;
	int retval;
	unsigned flags, swab;
	
	sb = old_dir->i_sb;
	flags = sb->u.ufs_sb.s_flags;
	swab = sb->u.ufs_sb.s_swab;

	UFSD(("ENTER\n"))
	
	old_inode = new_inode = NULL;
	old_bh = new_bh = dir_bh = NULL;
	new_de = NULL;
	retval = -ENAMETOOLONG;
	if (old_dentry->d_name.len > UFS_MAXNAMLEN)
		goto end_rename;

	old_bh = ufs_find_entry (old_dir, old_dentry->d_name.name, old_dentry->d_name.len, &old_de);
	/*
	 *  Check for inode number is _not_ due to possible IO errors.
	 *  We might rmdir the source, keep it as pwd of some process
	 *  and merrily kill the link to whatever was created under the
	 *  same name. Goodbye sticky bit ;-<
	 */
	retval = -ENOENT;
	old_inode = old_dentry->d_inode;
	if (!old_bh || SWAB32(old_de->d_ino) != old_inode->i_ino)
		goto end_rename;

	new_inode = new_dentry->d_inode;
	new_bh = ufs_find_entry (new_dir, new_dentry->d_name.name, new_dentry->d_name.len, &new_de);
	if (new_bh) {
		if (!new_inode) {
			brelse (new_bh);
			new_bh = NULL;
		} else {
			DQUOT_INIT(new_inode);
		}
	}
	retval = 0;
	if (new_inode == old_inode)
		goto end_rename;
	if (S_ISDIR(old_inode->i_mode)) {
		retval = -EINVAL;
		if (is_subdir(new_dentry, old_dentry))
			goto end_rename;
		if (new_inode) {
			/* Prune any children before testing for busy */
			if (new_dentry->d_count > 1)
				shrink_dcache_parent(new_dentry);
			retval = -EBUSY;
			if (new_dentry->d_count > 1)
				goto end_rename;
			retval = -ENOTEMPTY;
			if (!ufs_empty_dir (new_inode))
				goto end_rename;
		}

		dir_bh = ufs_bread (old_inode, 0, 0, &retval);
		if (!dir_bh)
			goto end_rename;
		if (SWAB32(PARENT_INO(dir_bh->b_data)) != old_dir->i_ino)
			goto end_rename;
		retval = -EMLINK;
		if (!new_inode && new_dir->i_nlink >= UFS_LINK_MAX)
			goto end_rename;
	}

	if (!new_bh)
		new_bh = ufs_add_entry (new_dir, new_dentry->d_name.name, new_dentry->d_name.len, &new_de,
					 &retval);
	if (!new_bh)
		goto end_rename;
	new_dir->i_version = ++event;

	/*
	 * ok, that's it
	 */
	new_de->d_ino = SWAB32(old_inode->i_ino);
	ufs_delete_entry (old_dir, old_de, old_bh);

	old_dir->i_version = ++event;
	if (new_inode) {
		new_inode->i_nlink--;
		new_inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty(new_inode);
	}
	old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(old_dir);
	if (dir_bh) {
		PARENT_INO(dir_bh->b_data) = SWAB32(new_dir->i_ino);
		mark_buffer_dirty(dir_bh, 1);
		old_dir->i_nlink--;
		mark_inode_dirty(old_dir);
		if (new_inode) {
			new_inode->i_nlink--;
			mark_inode_dirty(new_inode);
		} else {
			new_dir->i_nlink++;
			mark_inode_dirty(new_dir);
		}
	}
	mark_buffer_dirty(old_bh,  1);
	if (IS_SYNC(old_dir)) {
		ll_rw_block (WRITE, 1, &old_bh);
		wait_on_buffer (old_bh);
	}
	
	mark_buffer_dirty(new_bh, 1);
	if (IS_SYNC(new_dir)) {
		ll_rw_block (WRITE, 1, &new_bh);
		wait_on_buffer (new_bh);
	}

	/* Update the dcache */
	d_move(old_dentry, new_dentry);
	retval = 0;
end_rename:
	brelse (dir_bh);
	brelse (old_bh);
	brelse (new_bh);
	
	UFSD(("EXIT\n"))
	
	return retval;
}

/*
 * Ok, rename also locks out other renames, as they can change the parent of
 * a directory, and we don't want any races. Other races are checked for by
 * "do_rename()", which restarts if there are inconsistencies.
 *
 * Note that there is no race between different filesystems: it's only within
 * the same device that races occur: many renames can happen at once, as long
 * as they are on different partitions.
 *
 * In the second extended file system, we use a lock flag stored in the memory
 * super-block.  This way, we really lock other renames only if they occur
 * on the same file system
 */
int ufs_rename (struct inode * old_dir, struct dentry *old_dentry,
	struct inode * new_dir,	struct dentry *new_dentry )
{
	int result;

	UFSD(("ENTER\n"))
	
	while (old_dir->i_sb->u.ufs_sb.s_rename_lock)
		sleep_on (&old_dir->i_sb->u.ufs_sb.s_rename_wait);
	old_dir->i_sb->u.ufs_sb.s_rename_lock = 1;
	result = do_ufs_rename (old_dir, old_dentry, new_dir, new_dentry);
	old_dir->i_sb->u.ufs_sb.s_rename_lock = 0;
	wake_up (&old_dir->i_sb->u.ufs_sb.s_rename_wait);
	
	UFSD(("EXIT\n"))
	
	return result;
}
 

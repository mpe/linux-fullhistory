/*
 *  linux/fs/ext2/dir.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/dir.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext2 directory handling functions
 */

#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/sched.h>
#include <linux/stat.h>

#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x) (((x)+3) & ~3)

static int ext2_dir_read (struct inode * inode, struct file * filp,
			    char * buf, int count)
{
	return -EISDIR;
}

static int ext2_readdir (struct inode *, struct file *, struct dirent *, int);

static struct file_operations ext2_dir_operations = {
	NULL,			/* lseek - default */
	ext2_dir_read,		/* read */
	NULL,			/* write - bad */
	ext2_readdir,		/* readdir */
	NULL,			/* select - default */
	ext2_ioctl,		/* ioctl */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	file_fsync,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

/*
 * directories can handle most operations...
 */
struct inode_operations ext2_dir_inode_operations = {
	&ext2_dir_operations,	/* default directory file-ops */
	ext2_create,		/* create */
	ext2_lookup,		/* lookup */
	ext2_link,		/* link */
	ext2_unlink,		/* unlink */
	ext2_symlink,		/* symlink */
	ext2_mkdir,		/* mkdir */
	ext2_rmdir,		/* rmdir */
	ext2_mknod,		/* mknod */
	ext2_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* bmap */
	ext2_truncate,		/* truncate */
	ext2_permission,	/* permission */
	NULL			/* smap */
};

int ext2_check_dir_entry (char * function, struct inode * dir,
			  struct ext2_dir_entry * de, struct buffer_head * bh,
			  unsigned long offset)
{
	char * error_msg = NULL;

	if (de->rec_len < EXT2_DIR_REC_LEN(1))
		error_msg = "rec_len is smaller than minimal";
	else if (de->rec_len % 4 != 0)
		error_msg = "rec_len % 4 != 0";
	else if (de->rec_len < EXT2_DIR_REC_LEN(de->name_len))
		error_msg = "rec_len is too small for name_len";
	else if (dir && ((char *) de - bh->b_data) + de->rec_len >
		 dir->i_sb->s_blocksize)
		error_msg = "directory entry across blocks";
	else if (dir && de->inode > dir->i_sb->u.ext2_sb.s_es->s_inodes_count)
		error_msg = "inode out of bounds";

	if (error_msg != NULL)
		ext2_error (dir->i_sb, function, "bad directory entry: %s\n"
			    "offset=%lu, inode=%lu, rec_len=%d, name_len=%d",
			    error_msg, offset, (unsigned long) de->inode, de->rec_len,
			    de->name_len);
	return error_msg == NULL ? 1 : 0;
}

static int ext2_readdir (struct inode * inode, struct file * filp,
			 struct dirent * dirent, int count)
{
	unsigned long offset, blk;
	int i, num, stored, dlen;
	struct buffer_head * bh, * tmp, * bha[16];
	struct ext2_dir_entry * de;
	struct super_block * sb;
	int err, version;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;
	sb = inode->i_sb;

	stored = 0;
	bh = NULL;
	offset = filp->f_pos & (sb->s_blocksize - 1);

	while (count > 0 && !stored && filp->f_pos < inode->i_size) {
		blk = (filp->f_pos) >> EXT2_BLOCK_SIZE_BITS(sb);
		bh = ext2_bread (inode, blk, 0, &err);
		if (!bh) {
			filp->f_pos += sb->s_blocksize - offset;
			continue;
		}

		/*
		 * Do the readahead
		 */
		if (!offset) {
			for (i = 16 >> (EXT2_BLOCK_SIZE_BITS(sb) - 9), num = 0;
			     i > 0; i--) {
				tmp = ext2_getblk (inode, ++blk, 0, &err);
				if (tmp && !tmp->b_uptodate && !tmp->b_lock)
					bha[num++] = tmp;
				else
					brelse (tmp);
			}
			if (num) {
				ll_rw_block (READA, num, bha);
				for (i = 0; i < num; i++)
					brelse (bha[i]);
			}
		}
		
revalidate:
		/* If the dir block has changed since the last call to
		 * readdir(2), then we might be pointing to an invalid
		 * dirent right now.  Scan from the start of the block
		 * to make sure. */
		if (filp->f_version != inode->i_version) {
			for (i = 0; i < sb->s_blocksize && i < offset; ) {
				de = (struct ext2_dir_entry *) 
					(bh->b_data + i);
				/* It's too expensive to do a full
				 * dirent test each time round this
				 * loop, but we do have to test at
				 * least that it is non-zero.  A
				 * failure will be detected in the
				 * dirent test below. */
				if (de->rec_len < EXT2_DIR_REC_LEN(1))
					break;
				i += de->rec_len;
			}
			offset = i;
			filp->f_pos = (filp->f_pos & ~(sb->s_blocksize - 1))
				| offset;
			filp->f_version = inode->i_version;
		}
		
		while (count > 0 && filp->f_pos < inode->i_size 
		       && offset < sb->s_blocksize) {
			de = (struct ext2_dir_entry *) (bh->b_data + offset);
			if (!ext2_check_dir_entry ("ext2_readdir", inode, de,
						   bh, offset)) {
				/* On error, skip the f_pos to the
                                   next block. */
				filp->f_pos = (filp->f_pos & (sb->s_blocksize - 1))
					      + sb->s_blocksize;
				brelse (bh);
				return stored;
			}
			if (de->inode) {
				dlen = ROUND_UP(NAME_OFFSET(dirent) 
						+ de->name_len + 1);
				/* Old libc libraries always use a
                                   count of 1. */
				if (count == 1 && !stored)
					count = dlen;
				if (count < dlen) {
					count = 0;
					break;
				}

				/* We might block in the next section
				 * if the data destination is
				 * currently swapped out.  So, use a
				 * version stamp to detect whether or
				 * not the directory has been modified
				 * during the copy operation. */
				version = inode->i_version;
				i = de->name_len;
				memcpy_tofs (dirent->d_name, de->name, i);
				put_fs_long (de->inode, &dirent->d_ino);
				put_fs_byte (0, dirent->d_name + i);
				put_fs_word (i, &dirent->d_reclen);
				put_fs_long (dlen, &dirent->d_off);
				if (version != inode->i_version)
					goto revalidate;
				dcache_add(inode, de->name, de->name_len,
						 de->inode);

				stored += dlen;
				count -= dlen;
				((char *) dirent) += dlen;
			}
			offset += de->rec_len;
			filp->f_pos += de->rec_len;
		}
		offset = 0;
		brelse (bh);
	}
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
	return stored;
}

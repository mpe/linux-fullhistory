/*
 *  linux/fs/ufs/ufs_dir.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * swab support by Francois-Rene Rideau <rideau@ens.fr> 19970406
 *
 * 4.4BSD (FreeBSD) support added on February 1st 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk> partially based
 * on code by Martin von Loewis <martin@mira.isdn.cs.tu-berlin.de>.
 */

#include <linux/fs.h>

#include "ufs_swab.h"

/*
 * This is blatantly stolen from ext2fs
 */
static int
ufs_readdir (struct file * filp, void * dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	int error = 0;
	unsigned long offset, lblk, blk;
	int i, stored;
	struct buffer_head * bh;
	struct ufs_direct * de;
	struct super_block * sb;
	int de_reclen;
	__u32 flags;

	/* Isn't that already done in the upper layer???
         * the VFS layer really needs some explicit documentation!
         */
	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;

	sb = inode->i_sb;
        flags = sb->u.ufs_sb.s_flags;

	if (flags & UFS_DEBUG) {
	        printk("ufs_readdir: ino %lu  f_pos %lu\n",
	               inode->i_ino, (unsigned long) filp->f_pos);
	        ufs_print_inode(inode);
	}

	stored = 0;
	bh = NULL;
	offset = filp->f_pos & (sb->s_blocksize - 1);

	while (!error && !stored && filp->f_pos < inode->i_size) {
		lblk = (filp->f_pos) >> sb->s_blocksize_bits;
	        /* XXX - ufs_bmap() call needs error checking */
	        blk = ufs_bmap(inode, lblk);
		bh = bread (sb->s_dev, blk, sb->s_blocksize);
		if (!bh) {
	                /* XXX - error - skip to the next block */
	                printk("ufs_readdir: "
			       "dir inode %lu has a hole at offset %lu\n",
	                       inode->i_ino, (unsigned long int)filp->f_pos);
			filp->f_pos += sb->s_blocksize - offset;
			continue;
		}

revalidate:
		/* If the dir block has changed since the last call to
		 * readdir(2), then we might be pointing to an invalid
		 * dirent right now.  Scan from the start of the block
		 * to make sure. */
		if (filp->f_version != inode->i_version) {
			for (i = 0; i < sb->s_blocksize && i < offset; ) {
				de = (struct ufs_direct *)
					(bh->b_data + i);
				/* It's too expensive to do a full
				 * dirent test each time round this
				 * loop, but we do have to test at
				 * least that it is non-zero.  A
				 * failure will be detected in the
				 * dirent test below. */
				de_reclen = SWAB16(de->d_reclen);
				if (de_reclen < 1)
					break;
				i += de_reclen;
			}
			offset = i;
			filp->f_pos = (filp->f_pos & ~(sb->s_blocksize - 1))
				| offset;
			filp->f_version = inode->i_version;
		}

		while (!error && filp->f_pos < inode->i_size
		       && offset < sb->s_blocksize) {
			de = (struct ufs_direct *) (bh->b_data + offset);
	                /* XXX - put in a real ufs_check_dir_entry() */
	                if ((de->d_reclen == 0) || (NAMLEN(de) == 0)) {
			/* SWAB16() was unneeded -- compare to 0 */
	                        filp->f_pos = (filp->f_pos &
				              (sb->s_blocksize - 1)) +
				               sb->s_blocksize;
	                        brelse(bh);
	                        return stored;
	                }
#if 0 /* XXX */
			if (!ext2_check_dir_entry ("ext2_readdir", inode, de,
			/* XXX - beware about de having to be swabped somehow */
						   bh, offset)) {
				/* On error, skip the f_pos to the
	                           next block. */
				filp->f_pos = (filp->f_pos &
				              (sb->s_blocksize - 1)) +
					       sb->s_blocksize;
				brelse (bh);
				return stored;
			}
#endif /* XXX */
			offset += SWAB16(de->d_reclen);
			if (de->d_ino) {
			/* SWAB16() was unneeded -- compare to 0 */
				/* We might block in the next section
				 * if the data destination is
				 * currently swapped out.  So, use a
				 * version stamp to detect whether or
				 * not the directory has been modified
				 * during the copy operation. */
				unsigned long version = inode->i_version;

	                        if (flags & UFS_DEBUG) {
	                                printk("ufs_readdir: filldir(%s,%u)\n",
	                                       de->d_name, SWAB32(de->d_ino));
	                        }
				error = filldir(dirent, de->d_name, NAMLEN(de),
				                filp->f_pos, SWAB32(de->d_ino));
				if (error)
					break;
				if (version != inode->i_version)
					goto revalidate;
				stored ++;
			}
			filp->f_pos += SWAB16(de->d_reclen);
		}
		offset = 0;
		brelse (bh);
	}
#if 0 /* XXX */
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
#endif /* XXX */
	return 0;
}

static struct file_operations ufs_dir_operations = {
	NULL,			/* lseek */
	NULL,			/* read */
	NULL,			/* write */
	ufs_readdir,		/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	NULL,			/* open */
	NULL,			/* release */
	file_fsync,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL,			/* revalidate */
};

struct inode_operations ufs_dir_inode_operations = {
	&ufs_dir_operations,	/* default directory file operations */
	NULL,			/* create */
	ufs_lookup,		/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
};

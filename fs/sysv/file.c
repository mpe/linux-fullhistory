/*
 *  linux/fs/sysv/file.c
 *
 *  minix/file.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/file.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/file.c
 *  Copyright (C) 1993  Bruno Haible
 *
 *  SystemV/Coherent regular file handling primitives
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sysv_fs.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/pagemap.h>

#include <asm/uaccess.h>

#define	NBUF	32

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#include <linux/fs.h>
#include <linux/sysv_fs.h>

static ssize_t sysv_file_write(struct file *, const char *, size_t, loff_t *);

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the coh filesystem.
 */
static struct file_operations sysv_file_operations = {
	NULL,			/* lseek - default */
	sysv_file_read,		/* read */
	sysv_file_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	generic_file_mmap,	/* mmap */
	NULL,			/* no special open is needed */
	NULL,			/* flush */
	NULL,			/* release */
	sysv_sync_file		/* fsync */
};

struct inode_operations sysv_file_inode_operations = {
	&sysv_file_operations,	/* default file operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	generic_readpage,	/* readpage */
	NULL,			/* writepage */
	sysv_bmap,		/* bmap */
	sysv_truncate,		/* truncate */
	NULL			/* permission */
};

ssize_t sysv_file_read(struct file * filp, char * buf, 
		       size_t count, loff_t *ppos)
{
	struct inode * inode = filp->f_dentry->d_inode;
	struct super_block * sb = inode->i_sb;
	ssize_t read,left,chars;
	size_t block;
	ssize_t blocks, offset;
	int bhrequest, uptodate;
	struct buffer_head ** bhb, ** bhe;
	struct buffer_head * bhreq[NBUF];
	struct buffer_head * buflist[NBUF];
	size_t size;

	if (!inode) {
		printk("sysv_file_read: inode = NULL\n");
		return -EINVAL;
	}
	if (!S_ISREG(inode->i_mode)) {
		printk("sysv_file_read: mode = %07o\n",inode->i_mode);
		return -EINVAL;
	}
	offset = *ppos;
	size = inode->i_size;
	if (offset > size)
		left = 0;
	else
		left = size - offset;
	if (left > count)
		left = count;
	if (left <= 0)
		return 0;
	read = 0;
	block = offset >> sb->sv_block_size_bits;
	offset &= sb->sv_block_size_1;
	size = (size + sb->sv_block_size_1) >> sb->sv_block_size_bits;
	blocks = (left + offset + sb->sv_block_size_1) >> sb->sv_block_size_bits;
	bhb = bhe = buflist;
	if (filp->f_reada) {
		blocks += read_ahead[MAJOR(inode->i_dev)] >> (sb->sv_block_size_bits - 9);
		if (block + blocks > size)
			blocks = size - block;
	}

	/* We do this in a two stage process.  We first try to request
	   as many blocks as we can, then we wait for the first one to
	   complete, and then we try to wrap up as many as are actually
	   done.  This routine is rather generic, in that it can be used
	   in a filesystem by substituting the appropriate function in
	   for getblk.

	   This routine is optimized to make maximum use of the various
	   buffers and caches.
	 */

	do {
		bhrequest = 0;
		uptodate = 1;
		while (blocks) {
			--blocks;
			*bhb = sysv_getblk(inode, block++, 0);
			if (*bhb && !buffer_uptodate(*bhb)) {
				uptodate = 0;
				bhreq[bhrequest++] = *bhb;
			}

			if (++bhb == &buflist[NBUF])
				bhb = buflist;

			/* If the block we have on hand is uptodate, go ahead
			   and complete processing. */
			if (uptodate)
				break;
			if (bhb == bhe)
				break;
		}

		/* Now request them all */
		if (bhrequest)
			ll_rw_block(READ, bhrequest, bhreq);

		do { /* Finish off all I/O that has actually completed */
			if (*bhe) {
				wait_on_buffer(*bhe);
				if (!buffer_uptodate(*bhe)) {	/* read error? */
					brelse(*bhe);
					if (++bhe == &buflist[NBUF])
						bhe = buflist;
					left = 0;
					break;
				}
			}
			if (left < sb->sv_block_size - offset)
				chars = left;
			else
				chars = sb->sv_block_size - offset;
			*ppos += chars;
			left -= chars;
			read += chars;
			if (*bhe) {
				copy_to_user(buf,offset+(*bhe)->b_data,chars);
				brelse(*bhe);
				buf += chars;
			} else {
				while (chars-- > 0)
					put_user(0,buf++);
			}
			offset = 0;
			if (++bhe == &buflist[NBUF])
				bhe = buflist;
		} while (left > 0 && bhe != bhb && (!*bhe || !buffer_locked(*bhe)));
	} while (left > 0);

/* Release the read-ahead blocks */
	while (bhe != bhb) {
		brelse(*bhe);
		if (++bhe == &buflist[NBUF])
			bhe = buflist;
	};
	if (!read)
		return -EIO;
	filp->f_reada = 1;
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		mark_inode_dirty(inode);
	}
	return read;
}

static ssize_t sysv_file_write(struct file * filp, const char * buf,
			       size_t count, loff_t *ppos)
{
	struct inode * inode = filp->f_dentry->d_inode;
	struct super_block * sb = inode->i_sb;
	off_t pos;
	ssize_t written, c;
	struct buffer_head * bh;
	char * p;

	if (!inode) {
		printk("sysv_file_write: inode = NULL\n");
		return -EINVAL;
	}
	if (!S_ISREG(inode->i_mode)) {
		printk("sysv_file_write: mode = %07o\n",inode->i_mode);
		return -EINVAL;
	}
/*
 * OK, append may not work when many processes are writing at the same time
 * but so what. That way leads to madness anyway.
 * But we need to protect against simultaneous truncate as we may end up
 * writing our data into blocks that have meanwhile been incorporated into
 * the freelist, thereby trashing the freelist.
 */
	if (filp->f_flags & O_APPEND)
		pos = inode->i_size;
	else
		pos = *ppos;
	written = 0;
	while (written<count) {
		bh = sysv_getblk (inode, pos >> sb->sv_block_size_bits, 1);
		if (!bh) {
			if (!written)
				written = -ENOSPC;
			break;
		}
		c = sb->sv_block_size - (pos & sb->sv_block_size_1);
		if (c > count-written)
			c = count-written;
		if (c != sb->sv_block_size && !buffer_uptodate(bh)) {
			ll_rw_block(READ, 1, &bh);
			wait_on_buffer(bh);
			if (!buffer_uptodate(bh)) {
				brelse(bh);
				if (!written)
					written = -EIO;
				break;
			}
		}
		/* now either c==sb->sv_block_size or buffer_uptodate(bh) */
		p = (pos & sb->sv_block_size_1) + bh->b_data;
		copy_from_user(p, buf, c);
		update_vm_cache(inode, pos, p, c);
		pos += c;
		if (pos > inode->i_size) {
			inode->i_size = pos;
			mark_inode_dirty(inode);
		}
		written += c;
		buf += c;
		mark_buffer_uptodate(bh, 1);
		mark_buffer_dirty(bh, 0);
		brelse(bh);
	}
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	*ppos = pos;
	mark_inode_dirty(inode);
	return written;
}

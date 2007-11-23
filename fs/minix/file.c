/*
 *  linux/fs/minix/file.c
 *
 *  (C) 1991 Linus Torvalds
 *
 *  minix regular file handling primitives
 */

#include <errno.h>
#include <fcntl.h>

#include <sys/dirent.h>

#include <asm/segment.h>
#include <asm/system.h>

#include <linux/sched.h>
#include <linux/minix_fs.h>
#include <linux/kernel.h>
#include <linux/stat.h>

#define	NBUF	16

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#include <linux/fs.h>
#include <linux/minix_fs.h>

int minix_file_read(struct inode *, struct file *, char *, int);
static int minix_file_write(struct inode *, struct file *, char *, int);

/*
 * We have mostly NULL's here: the current defaults are ok for
 * the minix filesystem.
 */
static struct file_operations minix_file_operations = {
	NULL,			/* lseek - default */
	minix_file_read,	/* read */
	minix_file_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	NULL,			/* no special open is needed */
	NULL			/* release */
};

struct inode_operations minix_file_inode_operations = {
	&minix_file_operations,	/* default file operations */
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
	minix_bmap,		/* bmap */
	minix_truncate		/* truncate */
};

static inline void wait_on_buffer(struct buffer_head * bh)
{
	cli();
	while (bh->b_lock)
		sleep_on(&bh->b_wait);
	sti();
}

/*
 * minix_file_read() is also needed by the directory read-routine,
 * so it's not static. NOTE! reading directories directly is a bad idea,
 * but has to be supported for now for compatability reasons with older
 * versions.
 */
int minix_file_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	int read,left,chars,nr;
	int block, blocks, offset;
	struct buffer_head ** bhb, ** bhe;
	struct buffer_head * buflist[NBUF];

	if (!inode) {
		printk("minix_file_read: inode = NULL\n");
		return -EINVAL;
	}
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode))) {
		printk("minix_file_read: mode = %07o\n",inode->i_mode);
		return -EINVAL;
	}
	if (filp->f_pos > inode->i_size)
		left = 0;
	else
		left = inode->i_size - filp->f_pos;
	if (left > count)
		left = count;
	if (left <= 0)
		return 0;
	read = 0;
	block = filp->f_pos >> BLOCK_SIZE_BITS;
	offset = filp->f_pos & (BLOCK_SIZE-1);
	blocks = (left + offset + BLOCK_SIZE - 1) / BLOCK_SIZE;
	bhb = bhe = buflist;
	do {
		if (blocks) {
			--blocks;
			if (nr = minix_bmap(inode,block++)) {
				*bhb = getblk(inode->i_dev,nr);
				if (!(*bhb)->b_uptodate)
					ll_rw_block(READ,*bhb);
			} else
				*bhb = NULL;

			if (++bhb == &buflist[NBUF])
				bhb = buflist;

			if (bhb != bhe)
				continue;
		}
		if (*bhe) {
			wait_on_buffer(*bhe);
			if (!(*bhe)->b_uptodate) {
				do {
					brelse(*bhe);
					if (++bhe == &buflist[NBUF])
						bhe = buflist;
				} while (bhe != bhb);
				break;
			}
		}

		if (left < BLOCK_SIZE - offset)
			chars = left;
		else
			chars = BLOCK_SIZE - offset;
		filp->f_pos += chars;
		left -= chars;
		read += chars;
		if (*bhe) {
			memcpy_tofs(buf,offset+(*bhe)->b_data,chars);
			brelse(*bhe);
			buf += chars;
		} else {
			while (chars-->0)
				put_fs_byte(0,buf++);
		}
		offset = 0;
		if (++bhe == &buflist[NBUF])
			bhe = buflist;
	} while (left > 0);
	if (!read)
		return -EIO;
	inode->i_atime = CURRENT_TIME;
	inode->i_dirt = 1;
	return read;
}

static int minix_file_write(struct inode * inode, struct file * filp, char * buf, int count)
{
	off_t pos;
	int written,block,c;
	struct buffer_head * bh;
	char * p;

	if (!inode) {
		printk("minix_file_write: inode = NULL\n");
		return -EINVAL;
	}
	if (!S_ISREG(inode->i_mode)) {
		printk("minix_file_write: mode = %07o\n",inode->i_mode);
		return -EINVAL;
	}
/*
 * ok, append may not work when many processes are writing at the same time
 * but so what. That way leads to madness anyway.
 */
	if (filp->f_flags & O_APPEND)
		pos = inode->i_size;
	else
		pos = filp->f_pos;
	written = 0;
	while (written<count) {
		if (!(block = minix_create_block(inode,pos/BLOCK_SIZE))) {
			if (!written)
				written = -ENOSPC;
			break;
		}
		c = BLOCK_SIZE - (pos % BLOCK_SIZE);
		if (c > count-written)
			c = count-written;
		if (c == BLOCK_SIZE)
			bh = getblk(inode->i_dev, block);
		else
			bh = bread(inode->i_dev,block);
		if (!bh) {
			if (!written)
				written = -EIO;
			break;
		}
		p = (pos % BLOCK_SIZE) + bh->b_data;
		pos += c;
		if (pos > inode->i_size) {
			inode->i_size = pos;
			inode->i_dirt = 1;
		}
		written += c;
		memcpy_fromfs(p,buf,c);
		buf += c;
		bh->b_uptodate = 1;
		bh->b_dirt = 1;
		brelse(bh);
	}
	inode->i_mtime = CURRENT_TIME;
	inode->i_ctime = CURRENT_TIME;
	filp->f_pos = pos;
	inode->i_dirt = 1;
	return written;
}

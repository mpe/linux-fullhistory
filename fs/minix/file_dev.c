/*
 *  linux/fs/file_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <fcntl.h>
#include <sys/dirent.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/minix_fs.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

int minix_readdir(struct inode * inode, struct file * filp, struct dirent * dirent)
{
	unsigned int block,offset,i;
	char c;
	struct buffer_head * bh;
	struct minix_dir_entry * de;

	if (!S_ISDIR(inode->i_mode))
		return -EBADF;
	if (filp->f_pos & 15)
		return -EBADF;
	while (filp->f_pos < inode->i_size) {
		offset = filp->f_pos & 1023;
		block = minix_bmap(inode,(filp->f_pos)>>BLOCK_SIZE_BITS);
		if (!block || !(bh = bread(inode->i_dev,block))) {
			filp->f_pos += 1024-offset;
			continue;
		}
		de = (struct minix_dir_entry *) (offset + bh->b_data);
		while (offset < 1024 && filp->f_pos < inode->i_size) {
			offset += 16;
			filp->f_pos += 16;
			if (de->inode) {
				for (i = 0; i < 14; i++)
					if (c = de->name[i])
						put_fs_byte(c,i+dirent->d_name);
					else
						break;
				if (i) {
					put_fs_long(de->inode,&dirent->d_ino);
					put_fs_byte(0,i+dirent->d_name);
					put_fs_word(i,&dirent->d_reclen);
					brelse(bh);
					return i;
				}
			}
			de++;
		}
		brelse(bh);
	}
	return 0;
}

int minix_file_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	int read,left,chars,nr;
	struct buffer_head * bh;

	if (filp->f_pos > inode->i_size)
		left = 0;
	else
		left = inode->i_size - filp->f_pos;
	if (left > count)
		left = count;
	read = 0;
	while (left > 0) {
		if (nr = minix_bmap(inode,(filp->f_pos)>>BLOCK_SIZE_BITS)) {
			if (!(bh=bread(inode->i_dev,nr)))
				return read?read:-EIO;
		} else
			bh = NULL;
		nr = filp->f_pos & (BLOCK_SIZE-1);
		chars = MIN( BLOCK_SIZE-nr , left );
		filp->f_pos += chars;
		left -= chars;
		read += chars;
		if (bh) {
			memcpy_tofs(buf,nr+bh->b_data,chars);
			buf += chars;
			brelse(bh);
		} else {
			while (chars-->0)
				put_fs_byte(0,buf++);
		}
	}
	inode->i_atime = CURRENT_TIME;
	return read;
}

int minix_file_write(struct inode * inode, struct file * filp, char * buf, int count)
{
	off_t pos;
	int written,block,c;
	struct buffer_head * bh;
	char * p;

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
		if (!(bh=bread(inode->i_dev,block))) {
			if (!written)
				written = -EIO;
			break;
		}
		c = pos % BLOCK_SIZE;
		p = c + bh->b_data;
		c = BLOCK_SIZE-c;
		if (c > count-written)
			c = count-written;
		pos += c;
		if (pos > inode->i_size) {
			inode->i_size = pos;
			inode->i_dirt = 1;
		}
		written += c;
		memcpy_fromfs(p,buf,c);
		buf += c;
		bh->b_dirt = 1;
		brelse(bh);
	}
	inode->i_mtime = CURRENT_TIME;
	if (!(filp->f_flags & O_APPEND)) {
		filp->f_pos = pos;
		inode->i_ctime = CURRENT_TIME;
	}
	inode->i_dirt = 1;
	return written;
}

/*
 *  linux/fs/ext2/file.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext2 fs regular file handling primitives
 */

#include <asm/segment.h>
#include <asm/system.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/locks.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#define	NBUF	32

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#include <linux/fs.h>
#include <linux/ext2_fs.h>

static int ext2_readpage(struct inode *, unsigned long, char *);
static int ext2_file_read (struct inode *, struct file *, char *, int);
static int ext2_file_write (struct inode *, struct file *, const char *, int);
static void ext2_release_file (struct inode *, struct file *);

/*
 * We have mostly NULL's here: the current defaults are ok for
 * the ext2 filesystem.
 */
static struct file_operations ext2_file_operations = {
	NULL,			/* lseek - default */
	ext2_file_read,		/* read */
	ext2_file_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	ext2_ioctl,		/* ioctl */
	generic_mmap,  		/* mmap */
	NULL,			/* no special open is needed */
	ext2_release_file,	/* release */
	ext2_sync_file,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

struct inode_operations ext2_file_inode_operations = {
	&ext2_file_operations,/* default file operations */
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
	ext2_readpage,		/* readpage */
	NULL,			/* writepage */
	ext2_bmap,		/* bmap */
	ext2_truncate,		/* truncate */
	ext2_permission,	/* permission */
	NULL			/* smap */
};

static int ext2_readpage(struct inode * inode, unsigned long offset, char * page)
{
	int *p, nr[PAGE_SIZE/512];
	int i;

	i = PAGE_SIZE >> inode->i_sb->s_blocksize_bits;
	offset >>= inode->i_sb->s_blocksize_bits;
	p = nr;
	do {
		*p = ext2_bmap(inode, offset);
		i--;
		offset++;
		p++;
	} while (i > 0);
	return bread_page((unsigned long) page, inode->i_dev, nr, inode->i_sb->s_blocksize);
}

/*
 * This is a generic file read routine, and uses the
 * inode->i_op->readpage() function for the actual low-level
 * stuff. We can put this into the other filesystems too
 * once we've debugged it a bit more.
 */
static int ext2_file_read (struct inode * inode, struct file * filp,   
		char * buf, int count)
{
	int read = 0;
	unsigned long pos;
	unsigned long addr;
	unsigned long cached_page = 0;
	struct page *page;

	if (count <= 0)
		return 0;

	pos = filp->f_pos;
	
	for (;;) {
		unsigned long offset, nr;

		if (pos >= inode->i_size)
			break;

		offset = pos & ~PAGE_MASK;
		nr = PAGE_SIZE - offset;
		if (nr > count)
			nr = count;

		/* is it already cached? */
		page = find_page(inode, pos & PAGE_MASK);
		if (page)
			goto found_page;

		/* not cached, have to read it in.. */
		if (!(addr = cached_page)) {
			addr = cached_page = __get_free_page(GFP_KERNEL);
			if (!addr) {
				if (!read)
					read = -ENOMEM;
				break;
			}
		}
		inode->i_op->readpage(inode, pos & PAGE_MASK, (char *) addr);

		/* while we did that, things may have changed.. */
		if (pos >= inode->i_size)
			break;
		page = find_page(inode, pos & PAGE_MASK);
		if (page)
			goto found_page;

		/* nope, this is the only copy.. */
		cached_page = 0;
		page = mem_map + MAP_NR(addr);
		page->offset = pos & PAGE_MASK;
		add_page_to_inode_queue(inode, page);
		add_page_to_hash_queue(inode, page);

found_page:
		if (nr > inode->i_size - pos)
			nr = inode->i_size - pos;
		page->count++;
		addr = page_address(page);
		memcpy_tofs(buf, (void *) (addr + offset), nr);
		free_page(addr);
		buf += nr;
		pos += nr;
		read += nr;
		count -= nr;
		if (!count)
			break;
	}
	filp->f_pos = pos;
	if (cached_page)
		free_page(cached_page);
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
	return read;
}

static inline void update_vm_cache(struct inode * inode, unsigned long pos,
	char * buf, int count)
{
	struct page * page;

	page = find_page(inode, pos & PAGE_MASK);
	if (page) {
		pos = (pos & ~PAGE_MASK) + page_address(page);
		memcpy((void *) pos, buf, count);
	}
}

static int ext2_file_write (struct inode * inode, struct file * filp,
			    const char * buf, int count)
{
	const loff_t two_gb = 2147483647;
	loff_t pos;
	off_t pos2;
	long block;
	int offset;
	int written, c;
	struct buffer_head * bh, *bufferlist[NBUF];
	struct super_block * sb;
	int err;
	int i,buffercount,write_error;

	write_error = buffercount = 0;
	if (!inode) {
		printk("ext2_file_write: inode = NULL\n");
		return -EINVAL;
	}
	sb = inode->i_sb;
	if (sb->s_flags & MS_RDONLY)
		/*
		 * This fs has been automatically remounted ro because of errors
		 */
		return -ENOSPC;

	if (!S_ISREG(inode->i_mode)) {
		ext2_warning (sb, "ext2_file_write", "mode = %07o",
			      inode->i_mode);
		return -EINVAL;
	}
	if (filp->f_flags & O_APPEND)
		pos = inode->i_size;
	else
		pos = filp->f_pos;
	pos2 = (off_t) pos;
	/*
	 * If a file has been opened in synchronous mode, we have to ensure
	 * that meta-data will also be written synchronously.  Thus, we
	 * set the i_osync field.  This field is tested by the allocation
	 * routines.
	 */
	if (filp->f_flags & O_SYNC)
		inode->u.ext2_i.i_osync++;
	block = pos2 >> EXT2_BLOCK_SIZE_BITS(sb);
	offset = pos2 & (sb->s_blocksize - 1);
	c = sb->s_blocksize - offset;
	written = 0;
	while (count > 0) {
		if (pos > two_gb) {
			if (!written)
				written = -EFBIG;
			break;
		}
		bh = ext2_getblk (inode, block, 1, &err);
		if (!bh) {
			if (!written)
				written = err;
			break;
		}
		count -= c;
		if (count < 0)
			c += count;
		if (c != sb->s_blocksize && !buffer_uptodate(bh)) {
			ll_rw_block (READ, 1, &bh);
			wait_on_buffer (bh);
			if (!buffer_uptodate(bh)) {
				brelse (bh);
				if (!written)
					written = -EIO;
				break;
			}
		}
		memcpy_fromfs (bh->b_data + offset, buf, c);
		update_vm_cache(inode, pos, bh->b_data + offset, c);
		pos2 += c;
		pos += c;
		written += c;
		buf += c;
		mark_buffer_uptodate(bh, 1);
		mark_buffer_dirty(bh, 0);
		if (filp->f_flags & O_SYNC)
			bufferlist[buffercount++] = bh;
		else
			brelse(bh);
		if (buffercount == NBUF){
			ll_rw_block(WRITE, buffercount, bufferlist);
			for(i=0; i<buffercount; i++){
				wait_on_buffer(bufferlist[i]);
				if (!buffer_uptodate(bufferlist[i]))
					write_error=1;
				brelse(bufferlist[i]);
			}
			buffercount=0;
		}
		if(write_error)
			break;
		block++;
		offset = 0;
		c = sb->s_blocksize;
	}
	if ( buffercount ){
		ll_rw_block(WRITE, buffercount, bufferlist);
		for(i=0; i<buffercount; i++){
			wait_on_buffer(bufferlist[i]);
			if (!buffer_uptodate(bufferlist[i]))
				write_error=1;
			brelse(bufferlist[i]);
		}
	}		
	if (pos > inode->i_size)
		inode->i_size = pos;
	if (filp->f_flags & O_SYNC)
		inode->u.ext2_i.i_osync--;
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
	filp->f_pos = pos;
	inode->i_dirt = 1;
	return written;
}

/*
 * Called when a inode is released. Note that this is different
 * from ext2_open: open gets called at every open, but release
 * gets called only when /all/ the files are closed.
 */
static void ext2_release_file (struct inode * inode, struct file * filp)
{
	if (filp->f_mode & 2)
		ext2_discard_prealloc (inode);
}

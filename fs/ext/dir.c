/*
 *  linux/fs/ext/dir.c
 *
 *  Copyright (C) 1992 Remy Card (card@masi.ibp.fr)
 *
 *  from
 *
 *  linux/fs/minix/dir.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  ext directory handling functions
 */

#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/ext_fs.h>
#include <linux/stat.h>

static int ext_dir_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	return -EISDIR;
}

static int ext_readdir(struct inode *, struct file *, void *, filldir_t);

static struct file_operations ext_dir_operations = {
	NULL,			/* lseek - default */
	ext_dir_read,		/* read */
	NULL,			/* write - bad */
	ext_readdir,		/* readdir */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	file_fsync		/* fsync */
};

/*
 * directories can handle most operations...
 */
struct inode_operations ext_dir_inode_operations = {
	&ext_dir_operations,	/* default directory file-ops */
	ext_create,		/* create */
	ext_lookup,		/* lookup */
	ext_link,		/* link */
	ext_unlink,		/* unlink */
	ext_symlink,		/* symlink */
	ext_mkdir,		/* mkdir */
	ext_rmdir,		/* rmdir */
	ext_mknod,		/* mknod */
	ext_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	ext_truncate,		/* truncate */
	NULL			/* permission */
};

static int ext_readdir(struct inode * inode, struct file * filp,
	void * dirent, filldir_t filldir)
{
	int error;
	unsigned int i;
	off_t offset;
	struct buffer_head * bh;
	struct ext_dir_entry * de;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;
	if ((filp->f_pos & 7) != 0)
		return -EBADF;
	error = 0;
	while (!error && filp->f_pos < inode->i_size) {
		offset = filp->f_pos & 1023;
		bh = ext_bread(inode,(filp->f_pos)>>BLOCK_SIZE_BITS,0);
		if (!bh) {
			filp->f_pos += 1024-offset;
			continue;
		}
		for (i = 0; i < 1024 && i < offset; ) {
			de = (struct ext_dir_entry *) (bh->b_data + i);
			if (!de->rec_len)
				break;
			i += de->rec_len;
		}
		offset = i;
		de = (struct ext_dir_entry *) (offset + bh->b_data);
		while (offset < 1024 && filp->f_pos < inode->i_size) {
			if (de->rec_len < 8 || de->rec_len % 8 != 0 ||
			    de->rec_len < de->name_len + 8 ||
			    (de->rec_len + (off_t) filp->f_pos - 1) / 1024 > ((off_t) filp->f_pos / 1024)) {
				printk ("ext_readdir: bad dir entry, skipping\n");
				printk ("dev=%s, dir=%ld, "
				    "offset=%ld, rec_len=%d, name_len=%d\n",
				    kdevname(inode->i_dev), inode->i_ino,
				    offset, de->rec_len, de->name_len);
				filp->f_pos += 1024-offset;
				if (filp->f_pos > inode->i_size)
					filp->f_pos = inode->i_size;
				continue;
			}
			if (de->inode) {
				error = filldir(dirent, de->name, de->name_len, filp->f_pos, de->inode);
				if (error)
					break;
			}
			offset += de->rec_len;
			filp->f_pos += de->rec_len;
			((char *) de) += de->rec_len;
		}
		brelse(bh);
	}
	return 0;
}

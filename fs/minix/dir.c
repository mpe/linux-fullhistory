/*
 *  linux/fs/minix/dir.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  minix directory handling functions
 */

#include <linux/string.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/minix_fs.h>
#include <linux/stat.h>

#include <asm/segment.h>

static int minix_dir_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	return -EISDIR;
}

static int minix_readdir(struct inode *, struct file *, void *, filldir_t);

static struct file_operations minix_dir_operations = {
	NULL,			/* lseek - default */
	minix_dir_read,		/* read */
	NULL,			/* write - bad */
	minix_readdir,		/* readdir */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	file_fsync		/* default fsync */
};

/*
 * directories can handle most operations...
 */
struct inode_operations minix_dir_inode_operations = {
	&minix_dir_operations,	/* default directory file-ops */
	minix_create,		/* create */
	minix_lookup,		/* lookup */
	minix_link,		/* link */
	minix_unlink,		/* unlink */
	minix_symlink,		/* symlink */
	minix_mkdir,		/* mkdir */
	minix_rmdir,		/* rmdir */
	minix_mknod,		/* mknod */
	minix_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	minix_truncate,		/* truncate */
	NULL			/* permission */
};

static int minix_readdir(struct inode * inode, struct file * filp,
	void * dirent, filldir_t filldir)
{
	unsigned int offset;
	struct buffer_head * bh;
	struct minix_dir_entry * de;
	struct minix_sb_info * info;

	if (!inode || !inode->i_sb || !S_ISDIR(inode->i_mode))
		return -EBADF;
	info = &inode->i_sb->u.minix_sb;
	if (filp->f_pos & (info->s_dirsize - 1))
		return -EBADF;
	while (filp->f_pos < inode->i_size) {
		offset = filp->f_pos & 1023;
		bh = minix_bread(inode,(filp->f_pos)>>BLOCK_SIZE_BITS,0);
		if (!bh) {
			filp->f_pos += 1024-offset;
			continue;
		}
		do {
			de = (struct minix_dir_entry *) (offset + bh->b_data);
			if (de->inode) {
				int size = strnlen(de->name, info->s_namelen);
				if (filldir(dirent, de->name, size, filp->f_pos, de->inode) < 0) {
					brelse(bh);
					return 0;
				}
			}
			offset += info->s_dirsize;
			filp->f_pos += info->s_dirsize;
		} while (offset < 1024 && filp->f_pos < inode->i_size);
		brelse(bh);
	}
	return 0;
}

/*
 * file.c
 *
 * Copyright (c) 1999 Al Smith
 *
 * Portions derived from work (c) 1995,1996 Christian Vogelgsang.
 */

#include <linux/efs_fs.h>

static struct file_operations efs_file_operations = {
	NULL,			/* lseek */
	generic_file_read,	/* read */
	NULL,			/* write */
	NULL,			/* readdir */
	NULL,			/* poll */
	NULL,			/* ioctl */
	generic_file_mmap,	/* mmap */
	NULL,			/* open */
	NULL,			/* flush */
	NULL,			/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

struct inode_operations efs_file_inode_operations = {
	&efs_file_operations,	/* default file operations */
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
	efs_bmap,		/* bmap */
	block_read_full_page,	/* readpage */
	NULL,			/* writepage */
	NULL,			/* flushpage */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL			/* revalidate */
};
 
int efs_bmap(struct inode *inode, efs_block_t block) {

	if (block < 0) {
		printk(KERN_WARNING "EFS: bmap(): block < 0\n");
		return 0;
	}

	/* are we about to read past the end of a file ? */
	if (!(block < inode->i_blocks)) {
#ifdef DEBUG
		/*
		 * i have no idea why this happens as often as it does
		 */
		printk(KERN_WARNING "EFS: bmap(): block %d >= %ld (filesize %ld)\n",
			block,
			inode->i_blocks,
			inode->i_size);
#endif
		return 0;
	}

	return efs_map_block(inode, block);
}


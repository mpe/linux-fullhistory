/*
 *  linux/fs/ufs/ufs_symlink.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * Ported to 2.1.62 by Francois-Rene Rideau <rideau@issy.cnet.fr> 19971109
 *
 */

#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/sched.h>

#include <asm/uaccess.h>

extern int ufs_bmap (struct inode *, int);

static int
ufs_readlink(struct inode * inode, char * buffer, int buflen)
{
	unsigned long int block;
	struct buffer_head * bh = NULL;
	char * link;
	int i;
	char c;

	if (inode->i_sb->u.ufs_sb.s_flags & (UFS_DEBUG|UFS_DEBUG_LINKS)) {
	        printk("ufs_readlink: called on ino %lu dev %u/%u\n",
	               inode->i_ino, MAJOR(inode->i_dev), MINOR(inode->i_dev));
	}

	if (!S_ISLNK(inode->i_mode)) {
		return -EINVAL;
	}
   
	if (buflen > inode->i_sb->s_blocksize - 1)
		buflen = inode->i_sb->s_blocksize - 1;
	if (inode->i_blocks) {
	        /* XXX - error checking */
	        block = ufs_bmap(inode, 0);
	        if (inode->i_sb->u.ufs_sb.s_flags &(UFS_DEBUG|UFS_DEBUG_LINKS)) {
	                printk("ufs_readlink: bmap got %lu for ino %lu\n",
	                       block, inode->i_ino);
		}
	        bh = bread(inode->i_dev, block, BLOCK_SIZE);
		if (!bh) {
	                printk("ufs_readlink: can't read block 0 for ino %lu on dev %u/%u\n",
	                       inode->i_ino, MAJOR(inode->i_dev),
	                       MINOR(inode->i_dev));
			return 0;
		}
		link = bh->b_data;
		/* no need to bswap */
	}
	else {
	        link = (char *)&(inode->u.ufs_i.i_data[0]);
	}
	i = 0;
	while (i < buflen && (c = link[i])) {
		i++;
		put_user (c, buffer++);
	}
	brelse (bh);
	return i;
}

/*
 * XXX - blatantly stolen from minix fs
 */
static struct dentry *
ufs_follow_link(struct inode * inode, struct dentry * base)
{
	unsigned long int block;
	struct buffer_head * bh = NULL;
	char * link;

	if (inode->i_sb->u.ufs_sb.s_flags & (UFS_DEBUG|UFS_DEBUG_LINKS)) {
	        printk("ufs_follow_link: called on ino %lu dev %u/%u\n",
	               inode->i_ino, MAJOR(inode->i_dev), MINOR(inode->i_dev));
	}

	if (inode->i_blocks) {
	        /* read the link from disk */
	        /* XXX - error checking */
	        block = ufs_bmap(inode, 0);
	        bh = bread(inode->i_dev, block, BLOCK_SIZE);
	        if (bh == NULL) {
	                printk("ufs_follow_link: can't read block 0 for ino %lu on dev %u/%u\n",
	                       inode->i_ino, MAJOR(inode->i_dev),
	                       MINOR(inode->i_dev));
			dput(base);
	                return ERR_PTR(-EIO);
	        }
	        link = bh->b_data;
	} else {
	        /* fast symlink */
	        link = (char *)&(inode->u.ufs_i.i_data[0]);
	}
	base = lookup_dentry(link, base, 1);
	brelse (bh);
	return base;
}


static struct file_operations ufs_symlink_operations = {
	NULL,			/* lseek */
	NULL,			/* read */
	NULL,			/* write */
	NULL,			/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	NULL,			/* open */
	NULL,			/* release */
	NULL,			/* fsync */  /* XXX - is this ok? */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL,			/* revalidate */
};

struct inode_operations ufs_symlink_inode_operations = {
	&ufs_symlink_operations,	/* default directory file operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	&ufs_readlink,		/* readlink */
	&ufs_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
};

/*
 *  linux/fs/ufs/ufs_symlink.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * $Id: ufs_symlink.c,v 1.3 1996/04/25 09:12:11 davem Exp $
 *
 */

#include <linux/fs.h>
#include <linux/sched.h>

#include <asm/segment.h>

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
		iput (inode);
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
			iput (inode);
	                printk("ufs_readlink: can't read block 0 for ino %lu on dev %u/%u\n",
	                       inode->i_ino, MAJOR(inode->i_dev),
	                       MINOR(inode->i_dev));
			return 0;
		}
		link = bh->b_data;
	}
	else {
	        link = (char *)&(inode->u.ufs_i.ui_db[0]);
	}
	i = 0;
	while (i < buflen && (c = link[i])) {
		i++;
		put_user (c, buffer++);
	}
	iput (inode);
	if (bh)
		brelse (bh);
	return i;
}

/*
 * XXX - blatantly stolen from ext2fs
 */
static int
ufs_follow_link(struct inode * dir, struct inode * inode,
	        int flag, int mode, struct inode ** res_inode)
{
	unsigned long int block;
	int error;
	struct buffer_head * bh;
	char * link;

	bh = NULL;

	if (inode->i_sb->u.ufs_sb.s_flags & (UFS_DEBUG|UFS_DEBUG_LINKS)) {
	        printk("ufs_follow_link: called on ino %lu dev %u/%u\n",
	               dir->i_ino, MAJOR(dir->i_dev), MINOR(dir->i_dev));
	}

	*res_inode = NULL;
	if (!dir) {
	        dir = current->fs->root;
	        dir->i_count++;
	}
	if (!inode) {
		iput (dir);
		return -ENOENT;
	}
	if (!S_ISLNK(inode->i_mode)) {
		iput (dir);
		*res_inode = inode;
		return 0;
	}
	if (current->link_count > 5) {
		iput (dir);
		iput (inode);
		return -ELOOP;
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
	                iput(dir);
	                iput(inode);
	                return(-EIO);
	        }
	        link = bh->b_data;
	} else {
	        /* fast symlink */
	        link = (char *)&(inode->u.ufs_i.ui_db[0]);
	}
	current->link_count++;
	error = open_namei (link, flag, mode, res_inode, dir);
	current->link_count--;
	iput (inode);
	if (bh) {
		brelse (bh);
	}
	return(error);
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
	NULL,			/* smap */
};

/*
 * Local Variables: ***
 * c-indent-level: 8 ***
 * c-continued-statement-offset: 8 ***
 * c-brace-offset: -8 ***
 * c-argdecl-indent: 0 ***
 * c-label-offset: -8 ***
 * End: ***
 */

/* 
 * QNX4 file system, Linux implementation.
 * 
 * Version : 0.1
 * 
 * Using parts of the xiafs filesystem.
 * 
 * History :
 * 
 * 28-05-1998 by Richard Frowijn : first release.
 * 21-06-1998 by Frank Denis : ugly changes to make it compile on Linux 2.1.99+
 */

/* THIS FILE HAS TO BE REWRITTEN */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/qnx4_fs.h>
#include <linux/stat.h>

#include <asm/segment.h>
#include <asm/uaccess.h>

static int qnx4_readlink(struct dentry *, char *, int);
static struct dentry *qnx4_follow_link(struct dentry *, struct dentry *, unsigned int follow);

/*
 * symlinks can't do much...
 */
struct inode_operations qnx4_symlink_inode_operations =
{
	NULL,			/* no file-operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	qnx4_readlink,		/* readlink */
	qnx4_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static struct dentry *qnx4_follow_link(struct dentry *dentry,
				       struct dentry *base, unsigned int follow)
{
#if 0
	struct inode *inode = dentry->d_inode;
	struct buffer_head *bh;

	if (!inode) {
		return ERR_PTR(-ENOENT);
	}
	if (current->link_count > 5) {
		return ERR_PTR(-ELOOP);
	}
	if (!(bh = qnx4_bread(inode, 0, 0))) {
		return ERR_PTR(-EIO);
	}
	current->link_count++;
	current->link_count--;
	brelse(bh);
	return 0;
#else
	printk("qnx4: qnx4_follow_link needs to be fixed.\n");
	return ERR_PTR(-EIO);
#endif
}

static int qnx4_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
	struct buffer_head *bh;
	int i;
	char c;
	struct qnx4_inode_info *qnx4_ino;

	QNX4DEBUG(("qnx4: qnx4_readlink() called\n"));

	if (buffer == NULL || inode == NULL || !S_ISLNK(inode->i_mode)) {
		return -EINVAL;
	}
	qnx4_ino = &inode->u.qnx4_i;
	if (buflen > 1023) {
		buflen = 1023;
	}
	bh = bread(inode->i_dev, qnx4_ino->i_first_xtnt.xtnt_blk,
		   QNX4_BLOCK_SIZE);
	QNX4DEBUG(("qnx4: qnx4_bread sym called -> [%s]\n",
		   bh->b_data));
	if (bh == NULL) {
		QNX4DEBUG(("qnx4: NULL symlink bh\n"));
		return 0;
	}
	if (bh->b_data[0] != 0) {
		i = 0;
		while (i < buflen && (c = bh->b_data[i])) {
			i++;
			put_user(c, buffer++);
		}
		brelse(bh);
		return i;
	} else {
		brelse(bh);
		memcpy(buffer, "fixme", 5);
		return 5;
	}
}

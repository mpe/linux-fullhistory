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
 * 20-06-1998 by Frank Denis : Linux 2.1.99+ & dcache support.
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/qnx4_fs.h>
#include <linux/stat.h>

#include <asm/segment.h>

static int qnx4_readdir(struct file *filp, void *dirent, filldir_t filldir);

static int qnx4_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	unsigned int offset;
	struct buffer_head *bh;
	struct qnx4_inode_entry *de;
	long blknum;
	int i;
	int size;

	blknum = inode->u.qnx4_i.i_first_xtnt.xtnt_blk - 1 +
	    ((filp->f_pos >> 6) >> 3);

	if (!inode || !inode->i_sb || !S_ISDIR(inode->i_mode)) {
		return -EBADF;
	}
	QNX4DEBUG(("qnx4_readdir:i_size = %ld\n", (long) inode->i_size));
	QNX4DEBUG(("filp->f_pos         = %ld\n", (long) filp->f_pos));
	QNX4DEBUG(("BlkNum              = %ld\n", (long) blknum));

	while (filp->f_pos < inode->i_size) {
		bh = bread(inode->i_dev, blknum, QNX4_BLOCK_SIZE);
		i = (filp->f_pos - (((filp->f_pos >> 6) >> 3) << 9)) & 0x3f;
		while (i < QNX4_INODES_PER_BLOCK) {
			offset = i * QNX4_DIR_ENTRY_SIZE;
			de = (struct qnx4_inode_entry *) (bh->b_data + offset);
			size = strlen(de->di_fname);
			if (size) {

				QNX4DEBUG(("qnx4_readdir:%s\n", de->di_fname));

				if ((de->di_mode) || (de->di_status == QNX4_FILE_LINK)) {
					if (de->di_status) {
						if (filldir(dirent, de->di_fname, size, filp->f_pos, de->di_first_xtnt.xtnt_blk) < 0) {
							brelse(bh);
							return 0;
						}
					}
				}
			}
			i++;
			filp->f_pos += QNX4_DIR_ENTRY_SIZE;
		}
		brelse(bh);
		blknum++;
	}
	UPDATE_ATIME(inode);

	return 0;
}

static struct file_operations qnx4_dir_operations =
{
	NULL,			/* lseek - default */
	NULL,			/* read */
	NULL,			/* write - bad */
	qnx4_readdir,		/* readdir */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
        NULL,                   /* no special flush code */
	NULL,			/* no special release code */
	file_fsync,		/* default fsync */
	NULL,			/* default fasync */
	NULL,			/* default check_media_change */
	NULL,			/* default revalidate */
};

struct inode_operations qnx4_dir_inode_operations =
{
	&qnx4_dir_operations,
#ifdef CONFIG_QNX4FS_RW
	qnx4_create,
#else
	NULL,			/* create */
#endif
	qnx4_lookup,
	NULL,			/* link */
#ifdef CONFIG_QNX4FS_RW
	qnx4_unlink,		/* unlink */
#else
	NULL,
#endif
	NULL,			/* symlink */
	NULL,			/* mkdir */
#ifdef CONFIG_QNX4FS_RW
	qnx4_rmdir,		/* rmdir */
#else
	NULL,
#endif
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

/*
 * linux/fs/minix/minix_op.c
 *
 * structures for the minix super_block/inode/file-operations
 */

#include <linux/fs.h>
#include <linux/minix_fs.h>

void minix_put_inode(struct inode *inode)
{
	inode->i_size = 0;
	minix_truncate(inode);
	minix_free_inode(inode);
}

/*
 * These are the low-level inode operations for minix filesystem inodes.
 */
struct inode_operations minix_inode_operations = {
	minix_create,
	minix_lookup,
	minix_link,
	minix_unlink,
	minix_symlink,
	minix_mkdir,
	minix_rmdir,
	minix_mknod,
	minix_rename,
	minix_readlink,
	minix_open,
	minix_release,
	minix_follow_link,
	minix_bmap,
	minix_truncate,
	minix_write_inode,
	minix_put_inode
};

/*
 * We have mostly NULL's here: the current defaults are ok for
 * the minix filesystem.
 */
struct file_operations minix_file_operations = {
	NULL,			/* lseek - default */
	minix_file_read,	/* read */
	minix_file_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* close - default */
	NULL,			/* select - default */
	NULL			/* ioctl - default */
};
	
struct file_operations minix_dir_operations = {
	NULL,			/* lseek - default */
	minix_file_read,	/* read */
	NULL,			/* write - bad */
	minix_readdir,		/* readdir */
	NULL,			/* close - default */
	NULL,			/* select - default */
	NULL			/* ioctl - default */
};

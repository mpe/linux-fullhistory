/*
 *  linux/fs/fat/file.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  regular file handling primitives for fat-based filesystems
 */

#define ASC_LINUX_VERSION(V, P, S)	(((V) * 65536) + ((P) * 256) + (S))
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/locks.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/pagemap.h>
#include <linux/fat_cvf.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include "msbuffer.h"

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

#define PRINTK(x)
#define Printk(x) printk x

static struct file_operations fat_file_operations = {
	NULL,			/* lseek - default */
	fat_file_read,		/* read */
	fat_file_write,		/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select v2.0.x/poll v2.1.x - default */
	NULL,			/* ioctl - default */
	generic_file_mmap,	/* mmap */
	NULL,			/* no special open is needed */
	NULL,			/* flush */
	NULL,			/* release */
	file_fsync		/* fsync */
};

struct inode_operations fat_file_inode_operations = {
	&fat_file_operations,	/* default file operations */
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
	fat_get_block,		/* get_block */
	block_read_full_page,	/* readpage */
	NULL,			/* writepage */
	fat_truncate,		/* truncate */
	NULL,			/* permission */
	NULL			/* revalidate */
};

ssize_t fat_file_read(
	struct file *filp,
	char *buf,
	size_t count,
	loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	return MSDOS_SB(inode->i_sb)->cvf_format
			->cvf_file_read(filp,buf,count,ppos);
}


int fat_get_block(struct inode *inode, long iblock, struct buffer_head *bh_result, int create) {
	unsigned long phys;
	phys = fat_bmap(inode, iblock);
	if (phys) {
		bh_result->b_dev = inode->i_dev;
		bh_result->b_blocknr = phys;
		bh_result->b_state |= (1UL << BH_Mapped);
		return 0;
	}
	if (!create)
		return 0;
	if (iblock<<9 != MSDOS_I(inode)->i_realsize) {
		BUG();
		return -EIO;
	}
	if (!(iblock % MSDOS_SB(inode->i_sb)->cluster_size)) {
		if (fat_add_cluster(inode))
			return -ENOSPC;
	}
	MSDOS_I(inode)->i_realsize+=SECTOR_SIZE;
	phys=fat_bmap(inode, iblock);
	if (!phys)
		BUG();
	bh_result->b_dev = inode->i_dev;
	bh_result->b_blocknr = phys;
	bh_result->b_state |= (1UL << BH_Mapped);
	bh_result->b_state |= (1UL << BH_New);
	return 0;
}

static int fat_write_partial_page(struct file *file, struct page *page, unsigned long offset, unsigned long bytes, const char * buf)
{
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct page *new_page;
	unsigned long pgpos;
	long status;

	while((pgpos=MSDOS_I(inode)->i_realsize>>PAGE_CACHE_SHIFT)<page->index){
		status = -ENOMEM;
		new_page = grab_cache_page(&inode->i_data, pgpos);
		if (!new_page)
			goto out;
		status = block_write_cont_page(file, new_page, PAGE_SIZE, 0, NULL);
		UnlockPage(new_page);
		page_cache_release(new_page);
		if (status < 0)
			goto out;
	}
	status = block_write_cont_page(file, page, offset, bytes, buf);
out:
	return status;
}

ssize_t fat_file_write(
	struct file *filp,
	const char *buf,
	size_t count,
	loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	return MSDOS_SB(sb)->cvf_format
			->cvf_file_write(filp,buf,count,ppos);
}

ssize_t default_fat_file_write(
	struct file *filp,
	const char *buf,
	size_t count,
	loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	int retval;

	retval = generic_file_write(filp, buf, count, ppos,
					fat_write_partial_page);
	if (retval > 0) {
		inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		MSDOS_I(inode)->i_attrs |= ATTR_ARCH;
		mark_inode_dirty(inode);
	}
	return retval;
}

void fat_truncate(struct inode *inode)
{
	struct msdos_sb_info *sbi = MSDOS_SB(inode->i_sb);
	int cluster;

	/* Why no return value?  Surely the disk could fail... */
	if (IS_RDONLY (inode))
		return /* -EPERM */;
	if (IS_IMMUTABLE(inode))
		return /* -EPERM */;
	cluster = SECTOR_SIZE*sbi->cluster_size;
	MSDOS_I(inode)->i_realsize = ((inode->i_size-1) | (SECTOR_SIZE-1)) + 1;
	fat_free(inode,(inode->i_size+(cluster-1))>>sbi->cluster_bits);
	MSDOS_I(inode)->i_attrs |= ATTR_ARCH;
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
	mark_inode_dirty(inode);
}

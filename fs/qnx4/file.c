/*
 * QNX4 file system, Linux implementation.
 *
 * Version : 0.2.1
 *
 * Using parts of the xiafs filesystem.
 *
 * History :
 *
 * 25-05-1998 by Richard Frowijn : first release.
 * 21-06-1998 by Frank Denis : wrote qnx4_readpage to use generic_file_read.
 * 27-06-1998 by Frank Denis : file overwriting.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/qnx4_fs.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/locks.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))


#ifdef CONFIG_QNX4FS_RW
static ssize_t qnx4_file_write(struct file *filp, const char *buf,
			       size_t count, loff_t * ppos)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct qnx4_inode_info *qnx4_ino;
	struct buffer_head *bh;
	ssize_t result = -EBUSY, c;
	off_t pos;
	unsigned long start, block, extent_end;
	char *p;

	QNX4DEBUG(("qnx4: file_write(%s/%s (%d), %lu@%lu)\n",
		   dentry->d_parent->d_name.name, dentry->d_name.name,
	  inode->i_count, (unsigned long) count, (unsigned long) *ppos));
	if (inode == NULL) {
		printk("qnx4: NULL inode for file_write\n");
		return -EINVAL;
	}
	qnx4_ino = &inode->u.qnx4_i;
	if (S_ISREG(inode->i_mode) == 0) {
		printk("qnx4: write to non-file, mode %07o\n", inode->i_mode);
		return -EINVAL;
	}
	if (count == 0) {
		goto out;
	}
	if (filp->f_flags & O_APPEND) {
		pos = inode->i_size;
	} else {
		pos = *ppos;
	}
	start = qnx4_ino->i_first_xtnt.xtnt_blk + ((pos >> 9) * 0) - 1;
	result = 0;
	extent_end = start + qnx4_ino->i_first_xtnt.xtnt_size - 1;
	QNX4DEBUG(("qnx4: extent length : [%lu] bytes\n",
		   qnx4_ino->i_first_xtnt.xtnt_size));
	while (result < count) {
		block = start + pos / QNX4_BLOCK_SIZE;
		if (block > extent_end) {
			if (qnx4_is_free(inode->i_sb, block) <= 0) {
				printk("qnx4: next inode is busy -> write aborted.\n");
				result = -ENOSPC;
				break;
			}
		}
		if ((bh = bread(inode->i_dev, block,
				QNX4_BLOCK_SIZE)) == NULL) {
			printk("qnx4: I/O error on write.\n");
			result = -EIO;
			goto out;
		}
		if (bh == NULL) {
			if (result != 0) {
				result = -ENOSPC;
			}
			break;
		}
		if (block > extent_end) {
			qnx4_set_bitmap(inode->i_sb, block, 1);
			extent_end++;
			qnx4_ino->i_first_xtnt.xtnt_size = extent_end - start + 1;
		}
		c = QNX4_BLOCK_SIZE - (pos % QNX4_BLOCK_SIZE);
		if (c > count - result) {
			c = count - result;
		}
		if (c != QNX4_BLOCK_SIZE && buffer_uptodate(bh) == 0) {
			ll_rw_block(WRITE, 1, &bh);
			wait_on_buffer(bh);
			if (buffer_uptodate(bh) == 0) {
				brelse(bh);
				if (result != 0) {
					result = -EIO;
				}
				break;
			}
		}
		p = bh->b_data + (pos % QNX4_BLOCK_SIZE);
		c -= copy_from_user(p, buf, c);
		if (c == 0) {
			brelse(bh);
			if (result == 0) {
				result = -EFAULT;
			}
			break;
		}
//		update_vm_cache(inode, pos, p, c);
		mark_buffer_uptodate(bh, 1);
		mark_buffer_dirty(bh, 0);
		brelse(bh);
		pos += c;
		buf += c;
		result += c;
	}
	if (pos > inode->i_size) {
		inode->i_size = pos;
	}
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	*ppos = pos;
	mark_inode_dirty(inode);

      out:
	return result;
}
#endif

/*
 * We have mostly NULL's here: the current defaults are ok for
 * the qnx4 filesystem.
 */
static struct file_operations qnx4_file_operations =
{
	read:			generic_file_read,
#ifdef CONFIG_QNX4FS_RW
	write:			qnx4_file_write,
#endif
	mmap:			generic_file_mmap,
#ifdef CONFIG_QNX4FS_RW
	fsync:			qnx4_sync_file,
#endif
};

struct inode_operations qnx4_file_inode_operations =
{
	default_file_ops:	&qnx4_file_operations,
	get_block:		qnx4_get_block,
	readpage:		block_read_full_page,
#ifdef CONFIG_QNX4FS_RW
	writepage:		block_write_full_page,
	truncate:		qnx4_truncate,
#endif
};

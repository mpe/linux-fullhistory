/* 
 * QNX4 file system, Linux implementation.
 * 
 * Version : 0.1
 * 
 * Using parts of the xiafs filesystem.
 * 
 * History :
 * 
 * 01-06-1998 by Richard Frowijn : first release.
 * 21-06-1998 by Frank Denis : dcache support, fixed error codes.
 * 04-07-1998 by Frank Denis : first step for rmdir/unlink.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/qnx4_fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/errno.h>

#include <asm/segment.h>

/*
 * check if the filename is correct. For some obscure reason, qnx writes a
 * new file twice in the directory entry, first with all possible options at 0
 * and for a second time the way it is, they want us not to access the qnx
 * filesystem when whe are using linux.
 */
static int qnx4_match(int len, const char *name,
		      struct buffer_head *bh, unsigned long *offset)
{
	struct qnx4_inode_entry *de;
	int namelen;

	if (bh == NULL) {
		printk("qnx4: matching unassigned buffer !\n");
		return 0;
	}
	de = (struct qnx4_inode_entry *) (bh->b_data + *offset);
	*offset += QNX4_DIR_ENTRY_SIZE;
	if ((de->di_status & 0x08) == 0x08) {
		namelen = QNX4_NAME_MAX;
	} else {
		namelen = _SHORT_NAME_MAX;
	}
	/* "" means "." ---> so paths like "/usr/lib//libc.a" work */
	if (!len && (de->di_fname[0] == '.') && (de->di_fname[1] == '\0')) {
		return 1;
	}
	if (len != strlen(de->di_fname)) {
		return 0;
	}
	if (strncmp(name, de->di_fname, len) == 0) {
		if ((de->di_mode) || (de->di_status == QNX4_FILE_LINK)) {
			if (de->di_status) {
				return 1;
			}
		}
	}
	return 0;
}

static struct buffer_head *qnx4_find_entry(int len, struct inode *dir,
	   const char *name, struct qnx4_inode_entry **res_dir, int *ino)
{
	unsigned long block, offset, blkofs;
	struct buffer_head *bh;

	*res_dir = NULL;
	if (!dir || !dir->i_sb) {
		if (!dir) {
			printk("qnx4: NULL dir.\n");
		} else {
			printk("qnx4: no superblock on dir.\n");
		}
		return NULL;
	}
	bh = NULL;
	blkofs = dir->u.qnx4_i.i_first_xtnt.xtnt_blk - 1;
	offset = block = 0;
	while (block * QNX4_BLOCK_SIZE + offset < dir->i_size) {
		if (!bh) {
			bh = qnx4_bread(dir, block + blkofs, 0);
			if (!bh) {
				block++;
				continue;
			}
		}
		*res_dir = (struct qnx4_inode_entry *) (bh->b_data + offset);
		if (qnx4_match(len, name, bh, &offset)) {
			*ino = (block + blkofs) * QNX4_INODES_PER_BLOCK +
			    (offset / QNX4_DIR_ENTRY_SIZE) - 1;
			return bh;
		}
		if (offset < bh->b_size) {
			continue;
		}
		brelse(bh);
		bh = NULL;
		offset = 0;
		block++;
	}
	brelse(bh);
	*res_dir = NULL;
	return NULL;
}

int qnx4_lookup(struct inode *dir, struct dentry *dentry)
{
	int ino;
	struct qnx4_inode_entry *de;
	struct qnx4_link_info *lnk;
	struct buffer_head *bh;
	const char *name = dentry->d_name.name;
	int len = dentry->d_name.len;
	struct inode *foundinode;

	if (!dir) {
		return -EBADF;
	}
	if (!S_ISDIR(dir->i_mode)) {
		return -EBADF;
	}
	if (!(bh = qnx4_find_entry(len, dir, name, &de, &ino))) {
		return -ENOENT;
	}
	/* The entry is linked, let's get the real info */
	if ((de->di_status & QNX4_FILE_LINK) == QNX4_FILE_LINK) {
		lnk = (struct qnx4_link_info *) de;
		ino = (lnk->dl_inode_blk - 1) * QNX4_INODES_PER_BLOCK +
		    lnk->dl_inode_ndx;
	}
	brelse(bh);

	if ((foundinode = iget(dir->i_sb, ino)) == NULL) {
		QNX4DEBUG(("qnx4: lookup->iget -> NULL\n"));
		return -EACCES;
	}
	d_add(dentry, foundinode);

	return 0;
}

#ifdef CONFIG_QNX4FS_RW
int qnx4_create(struct inode *dir, struct dentry *dentry, int mode)
{
	QNX4DEBUG(("qnx4: qnx4_create\n"));
	if (dir == NULL) {
		return -ENOENT;
	}
	return -ENOSPC;
}

int qnx4_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct buffer_head *bh;
	struct qnx4_inode_entry *de;
	struct inode *inode;
	int retval;
	int ino;

	QNX4DEBUG(("qnx4: qnx4_rmdir [%s]\n", dentry->d_name.name));
	bh = qnx4_find_entry(dentry->d_name.len, dir, dentry->d_name.name,
			     &de, &ino);
	if (bh == NULL) {
		return -ENOENT;
	}
	inode = dentry->d_inode;
	if (inode->i_ino != ino) {
		retval = -EIO;
		goto end_rmdir;
	}
#if 0
	if (!empty_dir(inode)) {
		retval = -ENOTEMPTY;
		goto end_rmdir;
	}
#endif
	if (!list_empty(&dentry->d_hash)) {
		retval = -EBUSY;
		goto end_rmdir;
	}
	if (inode->i_nlink != 2) {
		QNX4DEBUG(("empty directory has nlink!=2 (%d)\n", inode->i_nlink));
	}
	QNX4DEBUG(("qnx4: deleting directory\n"));
	de->di_status = 0;
	memset(de->di_fname, 0, sizeof de->di_fname);
	de->di_mode = 0;
	mark_buffer_dirty(bh, 1);
	inode->i_nlink = 0;
	mark_inode_dirty(inode);
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_nlink--;
	mark_inode_dirty(dir);
	d_delete(dentry);
	retval = 0;

      end_rmdir:
	brelse(bh);

	return retval;
}

int qnx4_unlink(struct inode *dir, struct dentry *dentry)
{
	struct buffer_head *bh;
	struct qnx4_inode_entry *de;
	struct inode *inode;
	int retval;
	int ino;

	QNX4DEBUG(("qnx4: qnx4_unlink [%s]\n", dentry->d_name.name));
	bh = qnx4_find_entry(dentry->d_name.len, dir, dentry->d_name.name,
			     &de, &ino);
	if (bh == NULL) {
		return -ENOENT;
	}
	inode = dentry->d_inode;
	if (inode->i_ino != ino) {
		retval = -EIO;
		goto end_unlink;
	}
	retval = -EPERM;
	if (!inode->i_nlink) {
		QNX4DEBUG(("Deleting nonexistent file (%s:%lu), %d\n",
			   kdevname(inode->i_dev),
			   inode->i_ino, inode->i_nlink));
		inode->i_nlink = 1;
	}
	de->di_status = 0;
	memset(de->di_fname, 0, sizeof de->di_fname);
	de->di_mode = 0;
	mark_buffer_dirty(bh, 1);
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(dir);
	inode->i_nlink--;
	inode->i_ctime = dir->i_ctime;
	mark_inode_dirty(inode);
	d_delete(dentry);
	retval = 0;

      end_unlink:
	brelse(bh);

	return retval;
}
#endif

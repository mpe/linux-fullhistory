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
 * 20-06-1998 by Frank Denis : Linux 2.1.99+ support, boot signature, misc.
 * 30-06-1998 by Frank Denis : first step to write inodes.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/qnx4_fs.h>
#include <linux/fs.h>
#include <linux/locks.h>
#include <linux/init.h>

#include <asm/uaccess.h>

#define QNX4_VERSION  4
#define QNX4_BMNAME   ".bitmap"
#define CHECK_BOOT_SIGNATURE 0

static struct super_operations qnx4_sops;

#ifdef CONFIG_QNX4FS_RW

int qnx4_sync_inode(struct inode *inode)
{
	int err = 0;
# if 0
	struct buffer_head *bh;

   	bh = qnx4_update_inode(inode);
	if (bh && buffer_dirty(bh))
	{
		ll_rw_block(WRITE, 1, &bh);
		wait_on_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh))
		{
			printk ("IO error syncing qnx4 inode [%s:%08lx]\n",
				kdevname(inode->i_dev), inode->i_ino);
			err = -1;
		}
	        brelse (bh);	   
	} else if (!bh) {
		err = -1;
	}
# endif

	return err;
}

static void qnx4_delete_inode(struct inode *inode)
{
	QNX4DEBUG(("qnx4: deleting inode [%lu]\n", (unsigned long) inode->i_ino));
	inode->i_size = 0;
	qnx4_truncate(inode);
	qnx4_free_inode(inode);
}

static void qnx4_write_super(struct super_block *sb)
{
	QNX4DEBUG(("qnx4: write_super\n"));
	sb->s_dirt = 0;
}

static void qnx4_put_inode(struct inode *inode)
{
	if (inode->i_nlink != 0) {
		return;
	}
	inode->i_size = 0;
}

static void qnx4_write_inode(struct inode *inode)
{
	struct qnx4_inode_entry *raw_inode;
	int block, ino;
	struct buffer_head *bh;
	ino = inode->i_ino;

	QNX4DEBUG(("qnx4: write inode 1.\n"));
	if (inode->i_nlink == 0) {
		return;
	}
	if (!ino) {
		printk("qnx4: bad inode number on dev %s: %d is out of range\n",
		       kdevname(inode->i_dev), ino);
		return;
	}
	QNX4DEBUG(("qnx4: write inode 2.\n"));
	block = ino / QNX4_INODES_PER_BLOCK;
	if (!(bh = bread(inode->i_dev, block, QNX4_BLOCK_SIZE))) {
		printk("qnx4: major problem: unable to read inode from dev "
		       "%s\n", kdevname(inode->i_dev));
		return;
	}
	raw_inode = ((struct qnx4_inode_entry *) bh->b_data) +
	    (ino % QNX4_INODES_PER_BLOCK);
	raw_inode->di_mode = inode->i_mode;
	raw_inode->di_uid = inode->i_uid;
	raw_inode->di_gid = inode->i_gid;
	raw_inode->di_nlink = inode->i_nlink;
	raw_inode->di_size = inode->i_size;
	raw_inode->di_mtime = inode->i_mtime;
	raw_inode->di_atime = inode->i_atime;
	raw_inode->di_ctime = inode->i_ctime;
	raw_inode->di_first_xtnt.xtnt_size = inode->i_blocks;
	mark_buffer_dirty(bh, 1);
	brelse(bh);
}

#endif

static struct super_block *qnx4_read_super(struct super_block *, void *, int);
static void qnx4_put_super(struct super_block *sb);
static void qnx4_read_inode(struct inode *);
static int qnx4_remount(struct super_block *sb, int *flags, char *data);
static int qnx4_statfs(struct super_block *, struct statfs *, int);

static struct super_operations qnx4_sops =
{
	qnx4_read_inode,
#ifdef CONFIG_QNX4FS_RW
	qnx4_write_inode,
#else
	NULL,
#endif
#ifdef CONFIG_QNX4FS_RW
	qnx4_put_inode,
	qnx4_delete_inode,
	NULL,			/* notify_change */
#else
	NULL,			/* put_inode */
	NULL,			/* delete_inode */
	NULL,			/* notify_change */
#endif
	qnx4_put_super,
#ifdef CONFIG_QNX4FS_RW
	qnx4_write_super,
#else
	NULL,
#endif
	qnx4_statfs,
	qnx4_remount,
	NULL			/* clear_inode */
};

static int qnx4_remount(struct super_block *sb, int *flags, char *data)
{
	struct qnx4_sb_info *qs;

	qs = &sb->u.qnx4_sb;
	qs->Version = QNX4_VERSION;
	if (*flags & MS_RDONLY) {
		return 0;
	}
	mark_buffer_dirty(qs->sb_buf, 1);

	return 0;
}

struct buffer_head *inode_getblk(struct inode *inode, int nr,
				 int create)
{
	int tmp;
	int tst;
	struct buffer_head *result = NULL;

	tst = nr;
      repeat:
	tmp = tst;
	if (tmp) {
		result = getblk(inode->i_dev, tmp, QNX4_BLOCK_SIZE);
		if (tmp == tst) {
			return result;
		}
		brelse(result);
		goto repeat;
	}
	if (!create) {
		return NULL;
	}
#if 0
	tmp = qnx4_new_block(inode->i_sb);
	if (!tmp) {
		return NULL;
	}
	result = getblk(inode->i_dev, tmp, QNX4_BLOCK_SIZE);
	if (tst) {
		qnx4_free_block(inode->i_sb, tmp);
		brelse(result);
		goto repeat;
	}
	tst = tmp;
#endif
	inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
	return result;
}

struct buffer_head *qnx4_bread(struct inode *inode, int block, int create)
{
	struct buffer_head *bh;

	bh = inode_getblk(inode, block, create);
	if (!bh || buffer_uptodate(bh)) {
		return bh;
	}
	ll_rw_block(READ, 1, &bh);
	wait_on_buffer(bh);
	if (buffer_uptodate(bh)) {
		return bh;
	}
	brelse(bh);

	return NULL;
}

static int qnx4_statfs(struct super_block *sb,
		       struct statfs *buf, int bufsize)
{
	struct statfs tmp;

	memset(&tmp, 0, sizeof tmp);
	tmp.f_type = sb->s_magic;
	tmp.f_bsize = sb->s_blocksize;
	tmp.f_blocks = le32_to_cpu(sb->u.qnx4_sb.BitMap->di_size) * 8;
	tmp.f_bfree = qnx4_count_free_blocks(sb);
	tmp.f_bavail = tmp.f_bfree;
	tmp.f_files = 0x00;	/* change this !!! */
	tmp.f_ffree = qnx4_count_free_inodes(sb);
	tmp.f_namelen = QNX4_NAME_MAX;

	return copy_to_user(buf, &tmp, bufsize) ? -EFAULT : 0;
}

/*
 * Check the root directory of the filesystem to make sure
 * it really _is_ a qnx4 filesystem, and to check the size
 * of the directory entry.
 */
static const char *qnx4_checkroot(struct super_block *s)
{
	struct buffer_head *bh;
	struct qnx4_inode_entry *rootdir;
	int rd, rl;
	int i, j;
	int found = 0;

	if (s == NULL) {
		return "no qnx4 filesystem (null superblock).";
	}
	if (*(s->u.qnx4_sb.sb->RootDir.di_fname) != '/') {
		return "no qnx4 filesystem (no root dir).";
	} else {
		QNX4DEBUG(("QNX4 filesystem found on dev %s.\n", kdevname(s->s_dev)));
		rd = s->u.qnx4_sb.sb->RootDir.di_first_xtnt.xtnt_blk - 1;
		rl = s->u.qnx4_sb.sb->RootDir.di_first_xtnt.xtnt_size;
		for (j = 0; j < rl; j++) {
			bh = bread(s->s_dev, rd + j, QNX4_BLOCK_SIZE);	/* root dir, first block */
			if (bh == NULL) {
				return "unable to read root entry.";
			}
			for (i = 0; i < QNX4_INODES_PER_BLOCK; i++) {
				rootdir = (struct qnx4_inode_entry *) (bh->b_data + i * QNX4_DIR_ENTRY_SIZE);
				if (rootdir->di_fname != NULL) {
					QNX4DEBUG(("Rootdir entry found : [%s]\n", rootdir->di_fname));
					if (!strncmp(rootdir->di_fname, QNX4_BMNAME, sizeof QNX4_BMNAME)) {
						found = 1;
						s->u.qnx4_sb.BitMap = rootdir;	/* keep bitmap inode known */
						break;
					}
				}
			}
			brelse(bh);
			if (found != 0) {
				break;
			}
		}
		if (found == 0) {
			return "bitmap file not found.";
		}
	}
	return NULL;
}

static struct super_block *qnx4_read_super(struct super_block *s, 
					   void *data, int silent)
{
	struct buffer_head *bh;
	kdev_t dev = s->s_dev;
#if CHECK_BOOT_SIGNATURE
	char *tmpc;
#endif
	const char *errmsg;

	MOD_INC_USE_COUNT;
	lock_super(s);
	set_blocksize(dev, QNX4_BLOCK_SIZE);
	s->s_blocksize = QNX4_BLOCK_SIZE;
	s->s_blocksize_bits = 9;
	s->s_dev = dev;

#if CHECK_BOOT_SIGNATURE
	bh = bread(dev, 0, QNX4_BLOCK_SIZE);
	if (!bh) {
		printk("qnx4: unable to read the boot sector\n");
		goto outnobh;
	}
	tmpc = (char *) bh->b_data;
	if (tmpc[4] != 'Q' || tmpc[5] != 'N' || tmpc[6] != 'X' ||
	    tmpc[7] != '4' || tmpc[8] != 'F' || tmpc[9] != 'S') {
		printk("qnx4: wrong fsid in boot sector.\n");
		goto out;
	}
	brelse(bh);
#endif
	bh = bread(dev, 1, QNX4_BLOCK_SIZE);
	if (!bh) {
		printk("qnx4: unable to read the superblock\n");
		goto outnobh;
	}
	s->s_op = &qnx4_sops;
	s->s_magic = QNX4_SUPER_MAGIC;
#ifndef CONFIG_QNX4FS_RW
	s->s_flags |= MS_RDONLY;	/* Yup, read-only yet */
#endif
	s->u.qnx4_sb.sb_buf = bh;
	s->u.qnx4_sb.sb = (struct qnx4_super_block *) bh->b_data;
	s->s_root =
	    d_alloc_root(iget(s, QNX4_ROOT_INO * QNX4_INODES_PER_BLOCK), NULL);
	if (s->s_root == NULL) {
		printk("qnx4: get inode failed\n");
		goto out;
	}
	errmsg = qnx4_checkroot(s);
	if (errmsg != NULL) {
		printk("qnx4: %s\n", errmsg);
		goto out;
	}
	brelse(bh);
	unlock_super(s);
	s->s_dirt = 1;

	return s;

      out:
	brelse(bh);
      outnobh:
	s->s_dev = 0;
	unlock_super(s);
	MOD_DEC_USE_COUNT;

	return NULL;
}

static void qnx4_put_super(struct super_block *sb)
{
	MOD_DEC_USE_COUNT;
	return;
}

static void qnx4_read_inode(struct inode *inode)
{
	struct buffer_head *bh;
	struct qnx4_inode_entry *raw_inode;
	int block, ino;

	ino = inode->i_ino;
	inode->i_op = NULL;
	inode->i_mode = 0;

	QNX4DEBUG(("Reading inode : [%d]\n", ino));
	if (!ino) {
		printk("qnx4: bad inode number on dev %s: %d is out of range\n",
		       kdevname(inode->i_dev), ino);
		return;
	}
	block = ino / QNX4_INODES_PER_BLOCK;

	if (!(bh = bread(inode->i_dev, block, QNX4_BLOCK_SIZE))) {
		printk("qnx4: major problem: unable to read inode from dev "
		       "%s\n", kdevname(inode->i_dev));
		return;
	}
	raw_inode = ((struct qnx4_inode_entry *) bh->b_data) +
	    (ino % QNX4_INODES_PER_BLOCK);

	inode->i_mode = raw_inode->di_mode;
	inode->i_uid = raw_inode->di_uid;
	inode->i_gid = raw_inode->di_gid;
	inode->i_nlink = raw_inode->di_nlink;
	inode->i_size = raw_inode->di_size;
	inode->i_mtime = raw_inode->di_mtime;
	inode->i_atime = raw_inode->di_atime;
	inode->i_ctime = raw_inode->di_ctime;
	inode->i_blocks = raw_inode->di_first_xtnt.xtnt_size;
	inode->i_blksize = QNX4_DIR_ENTRY_SIZE;

	memcpy(&inode->u.qnx4_i, (struct qnx4_inode_info *) raw_inode, QNX4_DIR_ENTRY_SIZE);
	inode->i_op = &qnx4_file_inode_operations;
	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &qnx4_file_inode_operations;
	} else {
		if (S_ISDIR(inode->i_mode)) {
			inode->i_op = &qnx4_dir_inode_operations;
		} else {
			if (S_ISLNK(inode->i_mode)) {
				inode->i_op = &qnx4_symlink_inode_operations;
			} else {
				if (S_ISCHR(inode->i_mode)) {
					inode->i_op = &chrdev_inode_operations;
				} else {
					if (S_ISBLK(inode->i_mode)) {
						inode->i_op = &blkdev_inode_operations;
					} else {
						if (S_ISFIFO(inode->i_mode)) {
							init_fifo(inode);
						}
					}
				}
			}
		}
	}
	brelse(bh);
}

static struct file_system_type qnx4_fs_type =
{
	"qnx4",
	FS_REQUIRES_DEV,
	qnx4_read_super,
	NULL
};

__initfunc(int init_qnx4_fs(void))
{
	printk("QNX4 filesystem v0.2 registered.\n");
	return register_filesystem(&qnx4_fs_type);
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	return init_qnx4_fs();
}

void cleanup_module(void)
{
	unregister_filesystem(&qnx4_fs_type);
}

#endif

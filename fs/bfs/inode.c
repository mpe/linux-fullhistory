/*
 *	fs/bfs/inode.c
 *	BFS superblock and inode operations.
 *	Copyright (C) 1999 Tigran Aivazian <tigran@ocston.org>
 *	From fs/minix, Copyright (C) 1991, 1992 Linus Torvalds.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/locks.h>
#include <linux/bfs_fs.h>

#include <asm/uaccess.h>

#include "bfs_defs.h"

MODULE_AUTHOR("Tigran A. Aivazian");
MODULE_DESCRIPTION("UnixWare BFS filesystem for Linux");
EXPORT_NO_SYMBOLS;

#undef DEBUG

#ifdef DEBUG
#define DBG(x...)	printk(x)
#else
#define DBG(x...)
#endif

void dump_imap(const char *prefix, struct super_block * s);

static void bfs_read_inode(struct inode * inode)
{
	unsigned long ino = inode->i_ino;
	kdev_t dev = inode->i_dev;
	struct bfs_inode * di;
	struct buffer_head * bh;
	int block, off;

	if (ino < BFS_ROOT_INO || ino > inode->i_sb->su_lasti) {
		printk(KERN_ERR "BFS-fs: %s(): Bad inode number %s:%08lx\n", 
			__FUNCTION__, bdevname(dev), ino);
		make_bad_inode(inode);
		return;
	}

	block = (ino - BFS_ROOT_INO)/BFS_INODES_PER_BLOCK + 1;
	bh = bread(dev, block, BFS_BSIZE);
	if (!bh) {
		printk(KERN_ERR "BFS-fs: %s(): Unable to read inode %s:%08lx\n",
			__FUNCTION__, bdevname(dev), ino);
		make_bad_inode(inode);
		return;
	}

	off = (ino - BFS_ROOT_INO) % BFS_INODES_PER_BLOCK;
	di = (struct bfs_inode *)bh->b_data + off;

	inode->i_mode = 0x0000FFFF & di->i_mode;
	if (di->i_vtype == BFS_VDIR) {
		inode->i_mode |= S_IFDIR;
		inode->i_op = &bfs_dir_inops;
	} else if (di->i_vtype == BFS_VREG) {
		inode->i_mode |= S_IFREG;
		inode->i_op = &bfs_file_inops;
	} else 
		inode->i_op = NULL;

	inode->i_uid = di->i_uid;
	inode->i_gid = di->i_gid;
	inode->i_nlink = di->i_nlink;
	inode->i_size = BFS_FILESIZE(di);
	inode->i_blocks = BFS_FILEBLOCKS(di);
	inode->i_blksize = PAGE_SIZE;
	inode->i_atime = di->i_atime;
	inode->i_mtime = di->i_mtime;
	inode->i_ctime = di->i_ctime;
	inode->i_rdev = 0; /* BFS doesn't have special nodes */
	inode->iu_dsk_ino = di->i_ino; /* can be 0 so we store a copy */
	inode->iu_sblock = di->i_sblock;
	inode->iu_eblock = di->i_eblock;

	brelse(bh);
}

static void bfs_write_inode(struct inode * inode)
{
	unsigned long ino = inode->i_ino;
	kdev_t dev = inode->i_dev;
	struct bfs_inode * di;
	struct buffer_head * bh;
	int block, off;

	if (ino < BFS_ROOT_INO || ino > inode->i_sb->su_lasti) {
		printk(KERN_ERR "BFS-fs: %s(): Bad inode number %s:%08lx\n", 
			__FUNCTION__, bdevname(dev), ino);
		return;
	}

	block = (ino - BFS_ROOT_INO)/BFS_INODES_PER_BLOCK + 1;
	bh = bread(dev, block, BFS_BSIZE);
	if (!bh) {
		printk(KERN_ERR "BFS-fs: %s(): Unable to read inode %s:%08lx\n",
			__FUNCTION__, bdevname(dev), ino);
		return;
	}

	off = (ino - BFS_ROOT_INO)%BFS_INODES_PER_BLOCK;
	di = (struct bfs_inode *)bh->b_data + off;

	if (inode->i_ino == BFS_ROOT_INO)
		di->i_vtype = BFS_VDIR;
	else
		di->i_vtype = BFS_VREG;

	di->i_ino = inode->i_ino;
	di->i_mode = inode->i_mode;
	di->i_uid = inode->i_uid;
	di->i_gid = inode->i_gid;
	di->i_nlink = inode->i_nlink;
	di->i_atime = inode->i_atime;
	di->i_mtime = inode->i_mtime;
	di->i_ctime = inode->i_ctime;
	di->i_sblock = inode->iu_sblock;
	di->i_eblock = inode->iu_eblock;
	di->i_eoffset = di->i_sblock * BFS_BSIZE + inode->i_size - 1;

	mark_buffer_dirty(bh, 1);
	brelse(bh);
}

static void bfs_delete_inode(struct inode * inode)
{
	unsigned long ino = inode->i_ino;
	kdev_t dev = inode->i_dev;
	struct bfs_inode * di;
	struct buffer_head * bh;
	int block, off;
	struct super_block * s = inode->i_sb;

	DBG(KERN_ERR "%s(ino=%08lx)\n", __FUNCTION__, inode->i_ino);

	if (!inode)
		return;
	if (!inode->i_dev) {
		printk(KERN_ERR "BFS-fs: free_inode(%08lx) !dev\n", inode->i_ino);
		return;
	}
	if (inode->i_count > 1) {
		printk(KERN_ERR "BFS-fs: free_inode(%08lx) count=%d\n", 
			inode->i_ino, inode->i_count);
		return;
	}
	if (inode->i_nlink) {
		printk(KERN_ERR "BFS-fs: free_inode(%08lx) nlink=%d\n", 
			inode->i_ino, inode->i_nlink);
		return;
	}
	if (!inode->i_sb) {
		printk(KERN_ERR "BFS-fs: free_inode(%08lx) !sb\n", inode->i_ino);
		return;
	}
	if (inode->i_ino < BFS_ROOT_INO || inode->i_ino > inode->i_sb->su_lasti) {
		printk(KERN_ERR "BFS-fs: free_inode(%08lx) invalid ino\n", inode->i_ino);
		return;
	}
	
	inode->i_size = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
	block = (ino - BFS_ROOT_INO)/BFS_INODES_PER_BLOCK + 1;
	bh = bread(dev, block, BFS_BSIZE);
	if (!bh) {
		printk(KERN_ERR "BFS-fs: %s(): Unable to read inode %s:%08lx\n",
			__FUNCTION__, bdevname(dev), ino);
		return;
	}
	off = (ino - BFS_ROOT_INO)%BFS_INODES_PER_BLOCK;
	di = (struct bfs_inode *)bh->b_data + off;
	if (di->i_ino) {
		s->su_freeb += BFS_FILEBLOCKS(di);
		s->su_freei++;
		clear_bit(di->i_ino, s->su_imap);
		dump_imap("delete_inode", s);
	}
	di->i_ino = 0;
	di->i_sblock = 0;
	mark_buffer_dirty(bh, 1);
	brelse(bh);
	clear_inode(inode);
}

static void bfs_put_super(struct super_block *s)
{
	brelse(s->su_sbh);
	kfree(s->su_imap);
	MOD_DEC_USE_COUNT;
}

static int bfs_statfs(struct super_block *s, struct statfs *buf, int bufsiz)
{
	struct statfs tmp;

	tmp.f_type = BFS_MAGIC;
	tmp.f_bsize = s->s_blocksize;
	tmp.f_blocks = s->su_blocks;
	tmp.f_bfree = tmp.f_bavail = s->su_freeb;
	tmp.f_files = s->su_lasti + 1 - BFS_ROOT_INO;
	tmp.f_ffree = s->su_freei;
	tmp.f_fsid.val[0] = s->s_dev;
	tmp.f_namelen = BFS_NAMELEN;
	return copy_to_user(buf, &tmp, bufsiz) ? -EFAULT : 0;
}

static void bfs_write_super(struct super_block *s)
{
	if (!(s->s_flags & MS_RDONLY))
		mark_buffer_dirty(s->su_sbh, 1);
	s->s_dirt = 0;
}

static struct super_operations bfs_sops = {
	read_inode:	bfs_read_inode,
	write_inode:	bfs_write_inode,
	put_inode:	NULL,
	delete_inode:	bfs_delete_inode,
	notify_change:	NULL,
	put_super:	bfs_put_super,
	write_super:	bfs_write_super,
	statfs:		bfs_statfs,
	remount_fs:	NULL,
	clear_inode:	NULL,
	umount_begin:	NULL
};

void dump_imap(const char *prefix, struct super_block * s)
{
#if 0
	int i, hibit = 8 * (s->su_imap_len) - 1;
	char tmpbuf[400];

	memset(tmpbuf, 0, 400);
	for (i=hibit; i>=0; i--) {
		if (i>390) break;
		if (test_bit(i, s->su_imap))
			strcat(tmpbuf, "1");
		else
			strcat(tmpbuf, "0");
	}
	printk(KERN_ERR "BFS-fs: %s: lasti=%d <%s> (%d*8 bits)\n", 
		prefix, s->su_lasti, tmpbuf, s->su_imap_len);
#endif
}

static struct super_block * bfs_read_super(struct super_block * s, 
	void * data, int silent)
{
	kdev_t dev;
	struct buffer_head * bh;
	struct bfs_super_block * bfs_sb;
	struct inode * inode;
	unsigned long i;

	MOD_INC_USE_COUNT;
	lock_super(s);
	dev = s->s_dev;
	set_blocksize(dev, BFS_BSIZE);
	s->s_blocksize = BFS_BSIZE;
	s->s_blocksize_bits = BFS_BSIZE_BITS;

	/* read ahead 8K to get inodes as we'll need them in a tick */
	bh = breada(dev, 0, BFS_BSIZE, 0, 8192);
	if(!bh)
		goto out;
	bfs_sb = (struct bfs_super_block *)bh->b_data;
	if (bfs_sb->s_magic != BFS_MAGIC) {
		if (!silent)
			printk(KERN_ERR "BFS-fs: No BFS filesystem on %s (magic=%08x)\n", 
					bdevname(dev), bfs_sb->s_magic);
		goto out;
	}
	if (BFS_UNCLEAN(bfs_sb, s) && !silent)
		printk(KERN_WARNING "BFS-fs: %s is unclean\n", bdevname(dev));

#ifndef CONFIG_BFS_FS_WRITE
	s->s_flags |= MS_RDONLY; 
#endif
	s->s_magic = BFS_MAGIC;
	s->su_bfs_sb = bfs_sb;
	s->su_sbh = bh;
	s->su_lasti = (bfs_sb->s_start - BFS_BSIZE)/sizeof(struct bfs_inode) 
			+ BFS_ROOT_INO - 1;

	s->su_imap_len = s->su_lasti/8 + 1; /* 1 byte is 8 bit */
	s->su_imap = kmalloc(s->su_imap_len, GFP_KERNEL);
	if (!s->su_imap)
		goto out;
	memset(s->su_imap, 0, s->su_imap_len);
	for (i=0; i<BFS_ROOT_INO; i++)
		set_bit(i, s->su_imap);

	s->s_op = &bfs_sops;
	inode = iget(s, BFS_ROOT_INO);
	if (!inode) {
		kfree(s->su_imap);
		goto out;
	}
	s->s_root = d_alloc_root(inode);
	if (!s->s_root) {
		iput(inode);
		kfree(s->su_imap);
		goto out;
	}

	s->su_blocks = (bfs_sb->s_end + 1)>>BFS_BSIZE_BITS; /* for statfs(2) */
	s->su_freeb = (bfs_sb->s_end + 1 - bfs_sb->s_start)>>BFS_BSIZE_BITS;
	s->su_freei = 0;
	s->su_lf_eblk = 0;
	s->su_lf_sblk = 0;
	s->su_lf_ioff = 0;
	for (i=BFS_ROOT_INO; i<=s->su_lasti; i++) {
		inode = iget(s,i);
		if (inode->iu_dsk_ino == 0)
			s->su_freei++;
		else {
			set_bit(i, s->su_imap);
			s->su_freeb -= inode->i_blocks;
			if (inode->iu_eblock > s->su_lf_eblk) {
				s->su_lf_eblk = inode->iu_eblock;
				s->su_lf_sblk = inode->iu_sblock;
				s->su_lf_ioff = BFS_INO2OFF(i);
			}
		}
		iput(inode);
	}
	if (!(s->s_flags & MS_RDONLY)) {
		mark_buffer_dirty(bh, 1);
		s->s_dirt = 1;
	} 
	dump_imap("read_super", s);
	unlock_super(s);
	return s;

out:
	brelse(bh);
	s->s_dev = 0;
	unlock_super(s);
	MOD_DEC_USE_COUNT;
	return NULL;
}

static struct file_system_type bfs_fs_type = {
	name:		"bfs",
	fs_flags:	FS_REQUIRES_DEV,
	read_super:	bfs_read_super,
	next:		NULL
};

#ifdef MODULE
#define init_bfs_fs init_module

void cleanup_module(void)
{
	unregister_filesystem(&bfs_fs_type);
}
#endif

int __init init_bfs_fs(void)
{
	return register_filesystem(&bfs_fs_type);
}

/*
 *  linux/fs/ext/inode.c
 *
 *  (C) 1992  Remy Card (card@masi.ibp.fr)
 *
 *  from
 *
 *  linux/fs/minix/inode.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/string.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/ext_fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/system.h>
#include <asm/segment.h>

int sync_dev(int dev);

void ext_put_inode(struct inode *inode)
{
	inode->i_size = 0;
	ext_truncate(inode);
	ext_free_inode(inode);
}

void ext_put_super(struct super_block *sb)
{
#ifdef EXTFS_BITMAP
	int i;
#endif

	lock_super(sb);
	sb->s_dev = 0;
#ifdef EXTFS_BITMAP
	for(i = 0 ; i < EXT_I_MAP_SLOTS ; i++)
		brelse(sb->s_imap[i]);
	for(i = 0 ; i < EXT_Z_MAP_SLOTS ; i++)
		brelse(sb->s_zmap[i]);
#endif
#ifdef EXTFS_FREELIST
	if (sb->s_imap[1])
		brelse (sb->s_imap[1]);
	if (sb->s_zmap[1])
		brelse (sb->s_zmap[1]);
#endif
	free_super(sb);
	return;
}

static struct super_operations ext_sops = { 
	ext_read_inode,
	ext_write_inode,
	ext_put_inode,
	ext_put_super,
	ext_write_super,
	ext_statfs
};

struct super_block *ext_read_super(struct super_block *s,void *data)
{
	struct buffer_head *bh;
	struct ext_super_block *es;
	int dev=s->s_dev,block;
#ifdef EXTFS_BITMAP
	int i;
#endif

	lock_super(s);
	if (!(bh = bread(dev,1))) {
		s->s_dev=0;
		free_super(s);
		printk("bread failed\n");
		return NULL;
	}
/*	*((struct ext_super_block *) s) =
		*((struct ext_super_block *) bh->b_data); */
	es = (struct ext_super_block *) bh->b_data;
	s->s_ninodes = es->s_ninodes;
	s->s_nzones = es->s_nzones;
#ifdef EXTFS_BITMAP
	s->s_imap_blocks = es->s_imap_blocks;
	s->s_zmap_blocks = es->s_zmap_blocks;
#endif
	s->s_firstdatazone = es->s_firstdatazone;
	s->s_log_zone_size = es->s_log_zone_size;
	s->s_max_size = es->s_max_size;
	s->s_magic = es->s_magic;
#ifdef EXTFS_FREELIST
	s->s_zmap[0] = (struct buffer_head *) es->s_firstfreeblock;
	s->s_zmap[2] = (struct buffer_head *) es->s_freeblockscount;
	s->s_imap[0] = (struct buffer_head *) es->s_firstfreeinode;
	s->s_imap[2] = (struct buffer_head *) es->s_freeinodescount;
#endif
	brelse(bh);
	if (s->s_magic != EXT_SUPER_MAGIC) {
		s->s_dev = 0;
		free_super(s);
		printk("magic match failed\n");
		return NULL;
	}
#ifdef EXTFS_BITMAP
	for (i=0;i < EXT_I_MAP_SLOTS;i++)
		s->s_imap[i] = NULL;
	for (i=0;i < EXT_Z_MAP_SLOTS;i++)
		s->s_zmap[i] = NULL;
	block=2;
	for (i=0 ; i < s->s_imap_blocks ; i++)
		if (s->s_imap[i]=bread(dev,block))
			block++;
		else
			break;
	for (i=0 ; i < s->s_zmap_blocks ; i++)
		if (s->s_zmap[i]=bread(dev,block))
			block++;
		else
			break;
	if (block != 2+s->s_imap_blocks+s->s_zmap_blocks) {
		for(i=0;i<EXT_I_MAP_SLOTS;i++)
			brelse(s->s_imap[i]);
		for(i=0;i<EXT_Z_MAP_SLOTS;i++)
			brelse(s->s_zmap[i]);
		s->s_dev=0;
		free_super(s);
		printk("block failed\n");
		return NULL;
	}
	s->s_imap[0]->b_data[0] |= 1;
	s->s_zmap[0]->b_data[0] |= 1;
#endif
#ifdef EXTFS_FREELIST
	if (!s->s_zmap[0])
		s->s_zmap[1] = NULL;
	else
		if (!(s->s_zmap[1] = bread (dev, (unsigned long) s->s_zmap[0]))) {
			printk ("ext_read_super: unable to read first free block\n");
			s->s_dev = 0;
			free_super(s);
			return NULL;
		}
	if (!s->s_imap[0])
		s->s_imap[1] = NULL;
	else {
		block = 2 + (((unsigned long) s->s_imap[0]) - 1) / EXT_INODES_PER_BLOCK;
		if (!(s->s_imap[1] = bread (dev, block))) {
			printk ("ext_read_super: unable to read first free inode block\n");
			brelse(s->s_zmap[1]);
			s->s_dev = 0;
			free_super (s);
			return NULL;
		}
	}
#endif

	free_super(s);
	/* set up enough so that it can read an inode */
	s->s_dev = dev;
	s->s_op = &ext_sops;
	if (!(s->s_mounted = iget(dev,EXT_ROOT_INO))) {
		s->s_dev=0;
		printk("get root inode failed\n");
		return NULL;
	}
	return s;
}

void ext_write_super (struct super_block *sb)
{
#ifdef EXTFS_FREELIST
	struct buffer_head * bh;
	struct ext_super_block * es;

#ifdef EXTFS_DEBUG
	printk ("ext_write_super called\n");
#endif
	if (!(bh = bread (sb->s_dev, 1))) {
		printk ("ext_write_super: bread failed\n");
		return;
	}
	es = (struct ext_super_block *) bh->b_data;
	es->s_firstfreeblock = (unsigned long) sb->s_zmap[0];
	es->s_freeblockscount = (unsigned long) sb->s_zmap[2];
	es->s_firstfreeinode = (unsigned long) sb->s_imap[0];
	es->s_freeinodescount = (unsigned long) sb->s_imap[2];
	bh->b_dirt = 1;
	brelse (bh);
	sb->s_dirt = 0;
#endif
}

void ext_statfs (struct super_block *sb, struct statfs *buf)
{
	long tmp;

	put_fs_long(EXT_SUPER_MAGIC, &buf->f_type);
	put_fs_long(1024, &buf->f_bsize);
	put_fs_long(sb->s_nzones << sb->s_log_zone_size, &buf->f_blocks);
	tmp = ext_count_free_blocks(sb);
	put_fs_long(tmp, &buf->f_bfree);
	put_fs_long(tmp, &buf->f_bavail);
	put_fs_long(sb->s_ninodes, &buf->f_files);
	put_fs_long(ext_count_free_inodes(sb), &buf->f_ffree);
	/* Don't know what value to put in buf->f_fsid */
}

static int _ext_bmap(struct inode * inode,int block,int create)
{
	struct buffer_head * bh;
	int i;

	if (block<0) {
		printk("_ext_bmap: block<0");
		return 0;
	}
	if (block >= 9+256+256*256+256*256*256) {
		printk("_ext_bmap: block>big");
		return 0;
	}
	if (block<9) {
		if (create && !inode->i_data[block])
			if (inode->i_data[block]=ext_new_block(inode->i_dev)) {
				inode->i_ctime=CURRENT_TIME;
				inode->i_dirt=1;
			}
		return inode->i_data[block];
	}
	block -= 9;
	if (block<256) {
		if (create && !inode->i_data[9])
			if (inode->i_data[9]=ext_new_block(inode->i_dev)) {
				inode->i_dirt=1;
				inode->i_ctime=CURRENT_TIME;
			}
		if (!inode->i_data[9])
			return 0;
		if (!(bh = bread(inode->i_dev,inode->i_data[9])))
			return 0;
		i = ((unsigned long *) (bh->b_data))[block];
		if (create && !i)
			if (i=ext_new_block(inode->i_dev)) {
				((unsigned long *) (bh->b_data))[block]=i;
				bh->b_dirt=1;
			}
		brelse(bh);
		return i;
	}
	block -= 256;
	if (block<256*256) {
		if (create && !inode->i_data[10])
			if (inode->i_data[10]=ext_new_block(inode->i_dev)) {
				inode->i_dirt=1;
				inode->i_ctime=CURRENT_TIME;
			}
		if (!inode->i_data[10])
			return 0;
		if (!(bh=bread(inode->i_dev,inode->i_data[10])))
			return 0;
		i = ((unsigned long *)bh->b_data)[block>>8];
		if (create && !i)
			if (i=ext_new_block(inode->i_dev)) {
				((unsigned long *) (bh->b_data))[block>>8]=i;
				bh->b_dirt=1;
			}
		brelse(bh);
		if (!i)
			return 0;
		if (!(bh=bread(inode->i_dev,i)))
			return 0;
		i = ((unsigned long *)bh->b_data)[block&255];
		if (create && !i)
			if (i=ext_new_block(inode->i_dev)) {
				((unsigned long *) (bh->b_data))[block&255]=i;
				bh->b_dirt=1;
			}
		brelse(bh);
		return i;
	}
	printk("ext_bmap: triple indirection not yet implemented\n");
	return 0;
}

int ext_bmap(struct inode * inode,int block)
{
	return _ext_bmap(inode,block,0);
}

int ext_create_block(struct inode * inode, int block)
{
	return _ext_bmap(inode,block,1);
}

void ext_read_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct ext_inode * raw_inode;
	int block;

#ifdef EXTFS_BITMAP
	block = 2 + inode->i_sb->s_imap_blocks + inode->i_sb->s_zmap_blocks +
		(inode->i_ino-1)/EXT_INODES_PER_BLOCK;
#endif
#ifdef EXTFS_FREELIST
	block = 2 + (inode->i_ino-1)/EXT_INODES_PER_BLOCK;
#endif
	if (!(bh=bread(inode->i_dev,block)))
		panic("unable to read i-node block");
	raw_inode = ((struct ext_inode *) bh->b_data) +
		(inode->i_ino-1)%EXT_INODES_PER_BLOCK;
	inode->i_mode = raw_inode->i_mode;
	inode->i_uid = raw_inode->i_uid;
	inode->i_gid = raw_inode->i_gid;
	inode->i_nlink = raw_inode->i_nlinks;
	inode->i_size = raw_inode->i_size;
	inode->i_mtime = inode->i_atime = inode->i_ctime = raw_inode->i_time;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		inode->i_rdev = raw_inode->i_zone[0];
	else for (block = 0; block < 12; block++)
		inode->i_data[block] = raw_inode->i_zone[block];
	brelse(bh);
	inode->i_op = NULL;
	if (S_ISREG(inode->i_mode))
		inode->i_op = &ext_file_inode_operations;
	else if (S_ISDIR(inode->i_mode))
		inode->i_op = &ext_dir_inode_operations;
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &ext_symlink_inode_operations;
	else if (S_ISCHR(inode->i_mode))
		inode->i_op = &ext_chrdev_inode_operations;
	else if (S_ISBLK(inode->i_mode))
		inode->i_op = &ext_blkdev_inode_operations;
	else if (S_ISFIFO(inode->i_mode)) {
		inode->i_op = &ext_fifo_inode_operations;
		inode->i_size = 0;
		inode->i_pipe = 1;
		PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0;
		PIPE_READERS(*inode) = PIPE_WRITERS(*inode) = 0;
	}
}

void ext_write_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct ext_inode * raw_inode;
	int block;

#ifdef EXTFS_BITMAP
	block = 2 + inode->i_sb->s_imap_blocks + inode->i_sb->s_zmap_blocks +
		(inode->i_ino-1)/EXT_INODES_PER_BLOCK;
#endif
#ifdef EXTFS_FREELIST
	block = 2 + (inode->i_ino-1)/EXT_INODES_PER_BLOCK;
#endif
	if (!(bh=bread(inode->i_dev,block)))
		panic("unable to read i-node block");
	raw_inode = ((struct ext_inode *)bh->b_data) +
		(inode->i_ino-1)%EXT_INODES_PER_BLOCK;
	raw_inode->i_mode = inode->i_mode;
	raw_inode->i_uid = inode->i_uid;
	raw_inode->i_gid = inode->i_gid;
	raw_inode->i_nlinks = inode->i_nlink;
	raw_inode->i_size = inode->i_size;
	raw_inode->i_time = inode->i_mtime;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->i_zone[0] = inode->i_rdev;
	else for (block = 0; block < 12; block++)
		raw_inode->i_zone[block] = inode->i_data[block];
	bh->b_dirt=1;
	inode->i_dirt=0;
	brelse(bh);
}

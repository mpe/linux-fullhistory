/*
 *  linux/fs/minix/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/minix_fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>

#include <asm/system.h>
#include <asm/segment.h>

int sync_dev(int dev);

void minix_put_inode(struct inode *inode)
{
	inode->i_size = 0;
	minix_truncate(inode);
	minix_free_inode(inode);
}

void minix_put_super(struct super_block *sb)
{
	int i;

	lock_super(sb);
	sb->s_dev = 0;
	for(i = 0 ; i < MINIX_I_MAP_SLOTS ; i++)
		brelse(sb->u.minix_sb.s_imap[i]);
	for(i = 0 ; i < MINIX_Z_MAP_SLOTS ; i++)
		brelse(sb->u.minix_sb.s_zmap[i]);
	free_super(sb);
	return;
}

static struct super_operations minix_sops = { 
	minix_read_inode,
	minix_write_inode,
	minix_put_inode,
	minix_put_super,
	NULL,
	minix_statfs
};

struct super_block *minix_read_super(struct super_block *s,void *data)
{
	struct buffer_head *bh;
	struct minix_super_block *ms;
	int i,dev=s->s_dev,block;

	lock_super(s);
	if (!(bh = bread(dev,1,BLOCK_SIZE))) {
		s->s_dev=0;
		free_super(s);
		printk("bread failed\n");
		return NULL;
	}
	ms = (struct minix_super_block *) bh->b_data;
	s->s_blocksize = 1024;
	s->u.minix_sb.s_ninodes = ms->s_ninodes;
	s->u.minix_sb.s_nzones = ms->s_nzones;
	s->u.minix_sb.s_imap_blocks = ms->s_imap_blocks;
	s->u.minix_sb.s_zmap_blocks = ms->s_zmap_blocks;
	s->u.minix_sb.s_firstdatazone = ms->s_firstdatazone;
	s->u.minix_sb.s_log_zone_size = ms->s_log_zone_size;
	s->u.minix_sb.s_max_size = ms->s_max_size;
	s->s_magic = ms->s_magic;
	brelse(bh);
	if (s->s_magic != MINIX_SUPER_MAGIC) {
		s->s_dev = 0;
		free_super(s);
		printk("magic match failed\n");
		return NULL;
	}
	for (i=0;i < MINIX_I_MAP_SLOTS;i++)
		s->u.minix_sb.s_imap[i] = NULL;
	for (i=0;i < MINIX_Z_MAP_SLOTS;i++)
		s->u.minix_sb.s_zmap[i] = NULL;
	block=2;
	for (i=0 ; i < s->u.minix_sb.s_imap_blocks ; i++)
		if (s->u.minix_sb.s_imap[i]=bread(dev,block,BLOCK_SIZE))
			block++;
		else
			break;
	for (i=0 ; i < s->u.minix_sb.s_zmap_blocks ; i++)
		if (s->u.minix_sb.s_zmap[i]=bread(dev,block,BLOCK_SIZE))
			block++;
		else
			break;
	if (block != 2+s->u.minix_sb.s_imap_blocks+s->u.minix_sb.s_zmap_blocks) {
		for(i=0;i<MINIX_I_MAP_SLOTS;i++)
			brelse(s->u.minix_sb.s_imap[i]);
		for(i=0;i<MINIX_Z_MAP_SLOTS;i++)
			brelse(s->u.minix_sb.s_zmap[i]);
		s->s_dev=0;
		free_super(s);
		printk("block failed\n");
		return NULL;
	}
	s->u.minix_sb.s_imap[0]->b_data[0] |= 1;
	s->u.minix_sb.s_zmap[0]->b_data[0] |= 1;
	free_super(s);
	/* set up enough so that it can read an inode */
	s->s_dev = dev;
	s->s_op = &minix_sops;
	if (!(s->s_mounted = iget(dev,MINIX_ROOT_INO))) {
		s->s_dev=0;
		printk("get root inode failed\n");
		return NULL;
	}
	return s;
}

void minix_statfs (struct super_block *sb, struct statfs *buf)
{
	long tmp;

	put_fs_long(MINIX_SUPER_MAGIC, &buf->f_type);
	put_fs_long(1024, &buf->f_bsize);
	put_fs_long(sb->u.minix_sb.s_nzones << sb->u.minix_sb.s_log_zone_size, &buf->f_blocks);
	tmp = minix_count_free_blocks(sb);
	put_fs_long(tmp, &buf->f_bfree);
	put_fs_long(tmp, &buf->f_bavail);
	put_fs_long(sb->u.minix_sb.s_ninodes, &buf->f_files);
	put_fs_long(minix_count_free_inodes(sb), &buf->f_ffree);
	/* Don't know what value to put in buf->f_fsid */
}

static int _minix_bmap(struct inode * inode,int block,int create)
{
	struct buffer_head * bh;
	int i;

	if (block<0) {
		printk("_minix_bmap: block<0");
		return 0;
	}
	if (block >= 7+512+512*512) {
		printk("_minix_bmap: block>big");
		return 0;
	}
	if (block<7) {
		if (create && !inode->i_data[block])
			if (inode->i_data[block]=minix_new_block(inode->i_dev)) {
				inode->i_ctime=CURRENT_TIME;
				inode->i_dirt=1;
			}
		return inode->i_data[block];
	}
	block -= 7;
	if (block<512) {
		if (create && !inode->i_data[7])
			if (inode->i_data[7]=minix_new_block(inode->i_dev)) {
				inode->i_dirt=1;
				inode->i_ctime=CURRENT_TIME;
			}
		if (!inode->i_data[7])
			return 0;
		if (!(bh = bread(inode->i_dev,inode->i_data[7],BLOCK_SIZE)))
			return 0;
		i = ((unsigned short *) (bh->b_data))[block];
		if (create && !i)
			if (i=minix_new_block(inode->i_dev)) {
				((unsigned short *) (bh->b_data))[block]=i;
				bh->b_dirt=1;
			}
		brelse(bh);
		return i;
	}
	block -= 512;
	if (create && !inode->i_data[8])
		if (inode->i_data[8]=minix_new_block(inode->i_dev)) {
			inode->i_dirt=1;
			inode->i_ctime=CURRENT_TIME;
		}
	if (!inode->i_data[8])
		return 0;
	if (!(bh=bread(inode->i_dev,inode->i_data[8], BLOCK_SIZE)))
		return 0;
	i = ((unsigned short *)bh->b_data)[block>>9];
	if (create && !i)
		if (i=minix_new_block(inode->i_dev)) {
			((unsigned short *) (bh->b_data))[block>>9]=i;
			bh->b_dirt=1;
		}
	brelse(bh);
	if (!i)
		return 0;
	if (!(bh=bread(inode->i_dev,i,BLOCK_SIZE)))
		return 0;
	i = ((unsigned short *)bh->b_data)[block&511];
	if (create && !i)
		if (i=minix_new_block(inode->i_dev)) {
			((unsigned short *) (bh->b_data))[block&511]=i;
			bh->b_dirt=1;
		}
	brelse(bh);
	return i;
}

int minix_bmap(struct inode * inode,int block)
{
	return _minix_bmap(inode,block,0);
}

int minix_create_block(struct inode * inode, int block)
{
	return _minix_bmap(inode,block,1);
}

void minix_read_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix_inode * raw_inode;
	int block;

	block = 2 + inode->i_sb->u.minix_sb.s_imap_blocks + inode->i_sb->u.minix_sb.s_zmap_blocks +
		(inode->i_ino-1)/MINIX_INODES_PER_BLOCK;
	if (!(bh=bread(inode->i_dev,block, BLOCK_SIZE)))
		panic("unable to read i-node block");
	raw_inode = ((struct minix_inode *) bh->b_data) +
		(inode->i_ino-1)%MINIX_INODES_PER_BLOCK;
	inode->i_mode = raw_inode->i_mode;
	inode->i_uid = raw_inode->i_uid;
	inode->i_gid = raw_inode->i_gid;
	inode->i_nlink = raw_inode->i_nlinks;
	inode->i_size = raw_inode->i_size;
	inode->i_mtime = inode->i_atime = inode->i_ctime = raw_inode->i_time;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		inode->i_rdev = raw_inode->i_zone[0];
	else for (block = 0; block < 9; block++)
		inode->i_data[block] = raw_inode->i_zone[block];
	brelse(bh);
	inode->i_op = NULL;
	if (S_ISREG(inode->i_mode))
		inode->i_op = &minix_file_inode_operations;
	else if (S_ISDIR(inode->i_mode))
		inode->i_op = &minix_dir_inode_operations;
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &minix_symlink_inode_operations;
	else if (S_ISCHR(inode->i_mode))
		inode->i_op = &minix_chrdev_inode_operations;
	else if (S_ISBLK(inode->i_mode))
		inode->i_op = &minix_blkdev_inode_operations;
	else if (S_ISFIFO(inode->i_mode)) {
		inode->i_op = &minix_fifo_inode_operations;
		inode->i_size = 0;
		inode->i_pipe = 1;
		PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0;
		PIPE_READERS(*inode) = PIPE_WRITERS(*inode) = 0;
	}
}

void minix_write_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix_inode * raw_inode;
	int block;

	block = 2 + inode->i_sb->u.minix_sb.s_imap_blocks + inode->i_sb->u.minix_sb.s_zmap_blocks +
		(inode->i_ino-1)/MINIX_INODES_PER_BLOCK;
	if (!(bh=bread(inode->i_dev, block, BLOCK_SIZE)))
		panic("unable to read i-node block");
	raw_inode = ((struct minix_inode *)bh->b_data) +
		(inode->i_ino-1)%MINIX_INODES_PER_BLOCK;
	raw_inode->i_mode = inode->i_mode;
	raw_inode->i_uid = inode->i_uid;
	raw_inode->i_gid = inode->i_gid;
	raw_inode->i_nlinks = inode->i_nlink;
	raw_inode->i_size = inode->i_size;
	raw_inode->i_time = inode->i_mtime;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->i_zone[0] = inode->i_rdev;
	else for (block = 0; block < 9; block++)
		raw_inode->i_zone[block] = inode->i_data[block];
	bh->b_dirt=1;
	inode->i_dirt=0;
	brelse(bh);
}

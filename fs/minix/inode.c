/*
 *  linux/fs/minix/inode.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/minix_fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/system.h>

int sync_dev(int dev);

void minix_put_super(struct super_block *sb)
{
	int i;

	lock_super(sb);
	sb->s_dev = 0;
	for(i = 0 ; i < MINIX_I_MAP_SLOTS ; i++)
		brelse(sb->s_imap[i]);
	for(i = 0 ; i < MINIX_Z_MAP_SLOTS ; i++)
		brelse(sb->s_zmap[i]);
	free_super(sb);
	return;
}

static struct super_operations minix_sops = { 
	minix_read_inode,
	minix_put_super
};

struct super_block *minix_read_super(struct super_block *s,void *data)
{
	struct buffer_head *bh;
	int i,dev=s->s_dev,block;

	lock_super(s);
	if (!(bh = bread(dev,1))) {
		s->s_dev=0;
		free_super(s);
		printk("bread failed\n");
		return NULL;
	}
	*((struct minix_super_block *) s) =
		*((struct minix_super_block *) bh->b_data);
	brelse(bh);
	if (s->s_magic != MINIX_SUPER_MAGIC) {
		s->s_dev = 0;
		free_super(s);
		printk("magic match failed\n");
		return NULL;
	}
	for (i=0;i < MINIX_I_MAP_SLOTS;i++)
		s->s_imap[i] = NULL;
	for (i=0;i < MINIX_Z_MAP_SLOTS;i++)
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
		for(i=0;i<MINIX_I_MAP_SLOTS;i++)
			brelse(s->s_imap[i]);
		for(i=0;i<MINIX_Z_MAP_SLOTS;i++)
			brelse(s->s_zmap[i]);
		s->s_dev=0;
		free_super(s);
		printk("block failed\n");
		return NULL;
	}
	s->s_imap[0]->b_data[0] |= 1;
	s->s_zmap[0]->b_data[0] |= 1;
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
		if (!(bh = bread(inode->i_dev,inode->i_data[7])))
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
	if (!(bh=bread(inode->i_dev,inode->i_data[8])))
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
	if (!(bh=bread(inode->i_dev,i)))
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

	block = 2 + inode->i_sb->s_imap_blocks + inode->i_sb->s_zmap_blocks +
		(inode->i_ino-1)/MINIX_INODES_PER_BLOCK;
	if (!(bh=bread(inode->i_dev,block)))
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
	inode->i_op = &minix_inode_operations;
}

void minix_write_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix_inode * raw_inode;
	int block;

	block = 2 + inode->i_sb->s_imap_blocks + inode->i_sb->s_zmap_blocks +
		(inode->i_ino-1)/MINIX_INODES_PER_BLOCK;
	if (!(bh=bread(inode->i_dev,block)))
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

/*
 *  linux/fs/minix/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Copyright (C) 1996  Gertjan van Wingerde    (gertjan@cs.vu.nl)
 *	Minix V2 fs support.
 *
 *  Modified for 680x0 by Andreas Schwab
 */

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/locks.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>

#include <linux/minix_fs.h>

static void minix_read_inode(struct inode * inode);
static void minix_write_inode(struct inode * inode);
static int minix_statfs(struct super_block *sb, struct statfs *buf, int bufsiz);
static int minix_remount (struct super_block * sb, int * flags, char * data);

static void minix_delete_inode(struct inode *inode)
{
	inode->i_size = 0;
	minix_truncate(inode);
	minix_free_inode(inode);
}

static void minix_commit_super (struct super_block * sb,
			       struct minix_super_block * ms)
{
	mark_buffer_dirty(sb->u.minix_sb.s_sbh, 1);
	sb->s_dirt = 0;
}

static void minix_write_super (struct super_block * sb)
{
	struct minix_super_block * ms;

	if (!(sb->s_flags & MS_RDONLY)) {
		ms = sb->u.minix_sb.s_ms;

		if (ms->s_state & MINIX_VALID_FS)
			ms->s_state &= ~MINIX_VALID_FS;
		minix_commit_super (sb, ms);
	}
	sb->s_dirt = 0;
}


static void minix_put_super(struct super_block *sb)
{
	int i;

	if (!(sb->s_flags & MS_RDONLY)) {
		sb->u.minix_sb.s_ms->s_state = sb->u.minix_sb.s_mount_state;
		mark_buffer_dirty(sb->u.minix_sb.s_sbh, 1);
	}
	for (i = 0; i < sb->u.minix_sb.s_imap_blocks; i++)
		brelse(sb->u.minix_sb.s_imap[i]);
	for (i = 0; i < sb->u.minix_sb.s_zmap_blocks; i++)
		brelse(sb->u.minix_sb.s_zmap[i]);
	brelse (sb->u.minix_sb.s_sbh);
	kfree(sb->u.minix_sb.s_imap);

	MOD_DEC_USE_COUNT;
	return;
}

static struct super_operations minix_sops = {
	minix_read_inode,
	minix_write_inode,
	NULL,			/* put_inode */
	minix_delete_inode,
	NULL,			/* notify_change */
	minix_put_super,
	minix_write_super,
	minix_statfs,
	minix_remount
};

static int minix_remount (struct super_block * sb, int * flags, char * data)
{
	struct minix_super_block * ms;

	ms = sb->u.minix_sb.s_ms;
	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		return 0;
	if (*flags & MS_RDONLY) {
		if (ms->s_state & MINIX_VALID_FS ||
		    !(sb->u.minix_sb.s_mount_state & MINIX_VALID_FS))
			return 0;
		/* Mounting a rw partition read-only. */
		ms->s_state = sb->u.minix_sb.s_mount_state;
		mark_buffer_dirty(sb->u.minix_sb.s_sbh, 1);
		sb->s_dirt = 1;
		minix_commit_super (sb, ms);
	}
	else {
	  	/* Mount a partition which is read-only, read-write. */
		sb->u.minix_sb.s_mount_state = ms->s_state;
		ms->s_state &= ~MINIX_VALID_FS;
		mark_buffer_dirty(sb->u.minix_sb.s_sbh, 1);
		sb->s_dirt = 1;

		if (!(sb->u.minix_sb.s_mount_state & MINIX_VALID_FS))
			printk ("MINIX-fs warning: remounting unchecked fs, "
				"running fsck is recommended.\n");
		else if ((sb->u.minix_sb.s_mount_state & MINIX_ERROR_FS))
			printk ("MINIX-fs warning: remounting fs with errors, "
				"running fsck is recommended.\n");
	}
	return 0;
}

/*
 * Check the root directory of the filesystem to make sure
 * it really _is_ a Minix filesystem, and to check the size
 * of the directory entry.
 */
static const char * minix_checkroot(struct super_block *s, struct inode *dir)
{
	struct buffer_head *bh;
	struct minix_dir_entry *de;
	const char * errmsg;
	int dirsize;

	if (!S_ISDIR(dir->i_mode))
		return "root directory is not a directory";

	bh = minix_bread(dir, 0, 0);
	if (!bh)
		return "unable to read root directory";

	de = (struct minix_dir_entry *) bh->b_data;
	errmsg = "bad root directory '.' entry";
	dirsize = BLOCK_SIZE;
	if (de->inode == MINIX_ROOT_INO && strcmp(de->name, ".") == 0) {
		errmsg = "bad root directory '..' entry";
		dirsize = 8;
	}

	while ((dirsize <<= 1) < BLOCK_SIZE) {
		de = (struct minix_dir_entry *) (bh->b_data + dirsize);
		if (de->inode != MINIX_ROOT_INO)
			continue;
		if (strcmp(de->name, ".."))
			continue;
		s->u.minix_sb.s_dirsize = dirsize;
		s->u.minix_sb.s_namelen = dirsize - 2;
		errmsg = NULL;
		break;
	}
	brelse(bh);
	return errmsg;
}

static struct super_block *minix_read_super(struct super_block *s, void *data,
				     int silent)
{
	struct buffer_head *bh;
	struct buffer_head **map;
	struct minix_super_block *ms;
	int i, block;
	kdev_t dev = s->s_dev;
	const char * errmsg;
	struct inode *root_inode;
	
	/* N.B. These should be compile-time tests.
	   Unfortunately that is impossible. */
	if (32 != sizeof (struct minix_inode))
		panic("bad V1 i-node size");
	if (64 != sizeof(struct minix2_inode))
		panic("bad V2 i-node size");

	MOD_INC_USE_COUNT;
	lock_super(s);
	set_blocksize(dev, BLOCK_SIZE);
	if (!(bh = bread(dev,1,BLOCK_SIZE)))
		goto out_bad_sb;

	ms = (struct minix_super_block *) bh->b_data;
	s->u.minix_sb.s_ms = ms;
	s->u.minix_sb.s_sbh = bh;
	s->u.minix_sb.s_mount_state = ms->s_state;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->u.minix_sb.s_ninodes = ms->s_ninodes;
	s->u.minix_sb.s_nzones = ms->s_nzones;
	s->u.minix_sb.s_imap_blocks = ms->s_imap_blocks;
	s->u.minix_sb.s_zmap_blocks = ms->s_zmap_blocks;
	s->u.minix_sb.s_firstdatazone = ms->s_firstdatazone;
	s->u.minix_sb.s_log_zone_size = ms->s_log_zone_size;
	s->u.minix_sb.s_max_size = ms->s_max_size;
	s->s_magic = ms->s_magic;
	if (s->s_magic == MINIX_SUPER_MAGIC) {
		s->u.minix_sb.s_version = MINIX_V1;
		s->u.minix_sb.s_dirsize = 16;
		s->u.minix_sb.s_namelen = 14;
		s->u.minix_sb.s_link_max = MINIX_LINK_MAX;
	} else if (s->s_magic == MINIX_SUPER_MAGIC2) {
		s->u.minix_sb.s_version = MINIX_V1;
		s->u.minix_sb.s_dirsize = 32;
		s->u.minix_sb.s_namelen = 30;
		s->u.minix_sb.s_link_max = MINIX_LINK_MAX;
	} else if (s->s_magic == MINIX2_SUPER_MAGIC) {
		s->u.minix_sb.s_version = MINIX_V2;
		s->u.minix_sb.s_nzones = ms->s_zones;
		s->u.minix_sb.s_dirsize = 16;
		s->u.minix_sb.s_namelen = 14;
		s->u.minix_sb.s_link_max = MINIX2_LINK_MAX;
	} else if (s->s_magic == MINIX2_SUPER_MAGIC2) {
		s->u.minix_sb.s_version = MINIX_V2;
		s->u.minix_sb.s_nzones = ms->s_zones;
		s->u.minix_sb.s_dirsize = 32;
		s->u.minix_sb.s_namelen = 30;
		s->u.minix_sb.s_link_max = MINIX2_LINK_MAX;
	} else
		goto out_no_fs;

	/*
	 * Allocate the buffer map to keep the superblock small.
	 */
	i = (s->u.minix_sb.s_imap_blocks + s->u.minix_sb.s_zmap_blocks) * sizeof(bh);
	map = kmalloc(i, GFP_KERNEL);
	if (!map)
		goto out_no_map;
	memset(map, 0, i);
	s->u.minix_sb.s_imap = &map[0];
	s->u.minix_sb.s_zmap = &map[s->u.minix_sb.s_imap_blocks];

	block=2;
	for (i=0 ; i < s->u.minix_sb.s_imap_blocks ; i++) {
		if (!(s->u.minix_sb.s_imap[i]=bread(dev,block,BLOCK_SIZE)))
			goto out_no_bitmap;
		block++;
	}
	for (i=0 ; i < s->u.minix_sb.s_zmap_blocks ; i++) {
		if (!(s->u.minix_sb.s_zmap[i]=bread(dev,block,BLOCK_SIZE)))
			goto out_no_bitmap;
		block++;
	}

	minix_set_bit(0,s->u.minix_sb.s_imap[0]->b_data);
	minix_set_bit(0,s->u.minix_sb.s_zmap[0]->b_data);
	/* set up enough so that it can read an inode */
	s->s_op = &minix_sops;
	root_inode = iget(s, MINIX_ROOT_INO);
	if (!root_inode)
		goto out_no_root;
	/*
	 * Check the fs before we get the root dentry ...
	 */
	errmsg = minix_checkroot(s, root_inode);
	if (errmsg)
		goto out_bad_root;

	s->s_root = d_alloc_root(root_inode, NULL);
	if (!s->s_root)
		goto out_iput;

	s->s_root->d_op = &minix_dentry_operations;

	if (!(s->s_flags & MS_RDONLY)) {
		ms->s_state &= ~MINIX_VALID_FS;
		mark_buffer_dirty(bh, 1);
		s->s_dirt = 1;
	}
	unlock_super(s);
	if (!(s->u.minix_sb.s_mount_state & MINIX_VALID_FS))
		printk ("MINIX-fs: mounting unchecked file system, "
			"running fsck is recommended.\n");
 	else if (s->u.minix_sb.s_mount_state & MINIX_ERROR_FS)
		printk ("MINIX-fs: mounting file system with errors, "
			"running fsck is recommended.\n");
	return s;

out_bad_root:
	if (!silent)
		printk("MINIX-fs: %s\n", errmsg);
out_iput:
	iput(root_inode);
	goto out_freemap;

out_no_root:
	if (!silent)
		printk("MINIX-fs: get root inode failed\n");
	goto out_freemap;

out_no_bitmap:
	printk("MINIX-fs: bad superblock or unable to read bitmaps\n");
    out_freemap:
	for (i = 0; i < s->u.minix_sb.s_imap_blocks; i++)
		brelse(s->u.minix_sb.s_imap[i]);
	for (i = 0; i < s->u.minix_sb.s_zmap_blocks; i++)
		brelse(s->u.minix_sb.s_zmap[i]);
	kfree(s->u.minix_sb.s_imap);
	goto out_release;

out_no_map:
	if (!silent)
		printk ("MINIX-fs: can't allocate map\n");
	goto out_release;

out_no_fs:
	if (!silent)
		printk("VFS: Can't find a Minix or Minix V2 filesystem on device "
		       "%s.\n", kdevname(dev));
    out_release:
	brelse(bh);
	goto out_unlock;

out_bad_sb:
	printk("MINIX-fs: unable to read superblock\n");
    out_unlock:
	s->s_dev = 0;
	unlock_super(s);
	MOD_DEC_USE_COUNT;
	return NULL;
}

static int minix_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	struct statfs tmp;

	tmp.f_type = sb->s_magic;
	tmp.f_bsize = sb->s_blocksize;
	tmp.f_blocks = (sb->u.minix_sb.s_nzones - sb->u.minix_sb.s_firstdatazone) << sb->u.minix_sb.s_log_zone_size;
	tmp.f_bfree = minix_count_free_blocks(sb);
	tmp.f_bavail = tmp.f_bfree;
	tmp.f_files = sb->u.minix_sb.s_ninodes;
	tmp.f_ffree = minix_count_free_inodes(sb);
	tmp.f_namelen = sb->u.minix_sb.s_namelen;
	return copy_to_user(buf, &tmp, bufsiz) ? -EFAULT : 0;
}

/*
 * The minix V1 fs bmap functions.
 */
#define V1_inode_bmap(inode,nr) (((unsigned short *)(inode)->u.minix_i.u.i1_data)[(nr)])

static int V1_block_bmap(struct buffer_head * bh, int nr)
{
	int tmp;

	if (!bh)
		return 0;
	tmp = ((unsigned short *) bh->b_data)[nr];
	brelse(bh);
	return tmp;
}

static int V1_minix_bmap(struct inode * inode,int block)
{
	int i;

	if (block<0) {
		printk("minix_bmap: block<0");
		return 0;
	}
	if (block >= (inode->i_sb->u.minix_sb.s_max_size/BLOCK_SIZE)) {
		printk("minix_bmap: block>big");
		return 0;
	}
	if (block < 7)
		return V1_inode_bmap(inode,block);
	block -= 7;
	if (block < 512) {
		i = V1_inode_bmap(inode,7);
		if (!i)
			return 0;
		return V1_block_bmap(bread(inode->i_dev,i,BLOCK_SIZE),block);
	}
	block -= 512;
	i = V1_inode_bmap(inode,8);
	if (!i)
		return 0;
	i = V1_block_bmap(bread(inode->i_dev,i,BLOCK_SIZE),block>>9);
	if (!i)
		return 0;
	return V1_block_bmap(bread(inode->i_dev,i,BLOCK_SIZE),block & 511);
}

/*
 * The minix V2 fs bmap functions.
 */
#define V2_inode_bmap(inode,nr) (((unsigned long  *)(inode)->u.minix_i.u.i2_data)[(nr)])
static int V2_block_bmap(struct buffer_head * bh, int nr)
{
	int tmp;

	if (!bh)
		return 0;
	tmp = ((unsigned long *) bh->b_data)[nr];
	brelse(bh);
	return tmp;
}

static int V2_minix_bmap(struct inode * inode,int block)
{
	int i;

	if (block<0) {
		printk("minix_bmap: block<0");
		return 0;
	}
	if (block >= (inode->i_sb->u.minix_sb.s_max_size/BLOCK_SIZE)) {
		printk("minix_bmap: block>big");
		return 0;
	}
	if (block < 7)
		return V2_inode_bmap(inode,block);
	block -= 7;
	if (block < 256) {
		i = V2_inode_bmap(inode,7);
		if (!i)
			return 0;
		return V2_block_bmap(bread(inode->i_dev,i,BLOCK_SIZE),block);
	}
	block -= 256;
	if (block < 256*256) {
		i = V2_inode_bmap(inode,8);
		if (!i)
			return 0;
		i = V2_block_bmap(bread(inode->i_dev,i,BLOCK_SIZE),block >> 8);
		if (!i)
			return 0;
		return V2_block_bmap(bread(inode->i_dev,i,BLOCK_SIZE),block & 255);
	}
	block -= 256*256;
	i = V2_inode_bmap(inode,9);
	if (!i)
		return 0;
	i = V2_block_bmap(bread(inode->i_dev,i,BLOCK_SIZE),block >> 16);
	if (!i)
		return 0;
	i = V2_block_bmap(bread(inode->i_dev,i,BLOCK_SIZE),(block >> 8) & 255);
	if (!i)
		return 0;
	return V2_block_bmap(bread(inode->i_dev,i,BLOCK_SIZE),block & 255);
}

/*
 * The global minix fs bmap function.
 */
int minix_bmap(struct inode * inode,int block)
{
	if (INODE_VERSION(inode) == MINIX_V1)
		return V1_minix_bmap(inode, block);
	else
		return V2_minix_bmap(inode, block);
}

/*
 * The minix V1 fs getblk functions.
 */
static struct buffer_head * V1_inode_getblk(struct inode * inode, int nr,
					    int create)
{
	int tmp;
	unsigned short *p;
	struct buffer_head * result;

	p = inode->u.minix_i.u.i1_data + nr;
repeat:
	tmp = *p;
	if (tmp) {
		result = getblk(inode->i_dev, tmp, BLOCK_SIZE);
		if (tmp == *p)
			return result;
		brelse(result);
		goto repeat;
	}
	if (!create)
		return NULL;
	tmp = minix_new_block(inode->i_sb);
	if (!tmp)
		return NULL;
	result = getblk(inode->i_dev, tmp, BLOCK_SIZE);
	if (*p) {
		minix_free_block(inode->i_sb,tmp);
		brelse(result);
		goto repeat;
	}
	*p = tmp;
	inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
	return result;
}

static struct buffer_head * V1_block_getblk(struct inode * inode,
	struct buffer_head * bh, int nr, int create)
{
	int tmp;
	unsigned short *p;
	struct buffer_head * result;

	if (!bh)
		return NULL;
	if (!buffer_uptodate(bh)) {
		ll_rw_block(READ, 1, &bh);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			brelse(bh);
			return NULL;
		}
	}
	p = nr + (unsigned short *) bh->b_data;
repeat:
	tmp = *p;
	if (tmp) {
		result = getblk(bh->b_dev, tmp, BLOCK_SIZE);
		if (tmp == *p) {
			brelse(bh);
			return result;
		}
		brelse(result);
		goto repeat;
	}
	if (!create) {
		brelse(bh);
		return NULL;
	}
	tmp = minix_new_block(inode->i_sb);
	if (!tmp) {
		brelse(bh);
		return NULL;
	}
	result = getblk(bh->b_dev, tmp, BLOCK_SIZE);
	if (*p) {
		minix_free_block(inode->i_sb,tmp);
		brelse(result);
		goto repeat;
	}
	*p = tmp;
	mark_buffer_dirty(bh, 1);
	brelse(bh);
	return result;
}

static struct buffer_head * V1_minix_getblk(struct inode * inode, int block,
					    int create)
{
	struct buffer_head * bh;

	if (block<0) {
		printk("minix_getblk: block<0");
		return NULL;
	}
	if (block >= inode->i_sb->u.minix_sb.s_max_size/BLOCK_SIZE) {
		printk("minix_getblk: block>big");
		return NULL;
	}
	if (block < 7)
		return V1_inode_getblk(inode,block,create);
	block -= 7;
	if (block < 512) {
		bh = V1_inode_getblk(inode,7,create);
		return V1_block_getblk(inode, bh, block, create);
	}
	block -= 512;
	bh = V1_inode_getblk(inode,8,create);
	bh = V1_block_getblk(inode, bh, (block>>9) & 511, create);
	return V1_block_getblk(inode, bh, block & 511, create);
}

/*
 * The minix V2 fs getblk functions.
 */
static struct buffer_head * V2_inode_getblk(struct inode * inode, int nr,
					    int create)
{
	int tmp;
	unsigned long *p;
	struct buffer_head * result;

	p = (unsigned long *) inode->u.minix_i.u.i2_data + nr;
repeat:
	tmp = *p;
	if (tmp) {
		result = getblk(inode->i_dev, tmp, BLOCK_SIZE);
		if (tmp == *p)
			return result;
		brelse(result);
		goto repeat;
	}
	if (!create)
		return NULL;
	tmp = minix_new_block(inode->i_sb);
	if (!tmp)
		return NULL;
	result = getblk(inode->i_dev, tmp, BLOCK_SIZE);
	if (*p) {
		minix_free_block(inode->i_sb,tmp);
		brelse(result);
		goto repeat;
	}
	*p = tmp;
	inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
	return result;
}

static struct buffer_head * V2_block_getblk(struct inode * inode,
	struct buffer_head * bh, int nr, int create)
{
	int tmp;
	unsigned long *p;
	struct buffer_head * result;

	if (!bh)
		return NULL;
	if (!buffer_uptodate(bh)) {
		ll_rw_block(READ, 1, &bh);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			brelse(bh);
			return NULL;
		}
	}
	p = nr + (unsigned long *) bh->b_data;
repeat:
	tmp = *p;
	if (tmp) {
		result = getblk(bh->b_dev, tmp, BLOCK_SIZE);
		if (tmp == *p) {
			brelse(bh);
			return result;
		}
		brelse(result);
		goto repeat;
	}
	if (!create) {
		brelse(bh);
		return NULL;
	}
	tmp = minix_new_block(inode->i_sb);
	if (!tmp) {
		brelse(bh);
		return NULL;
	}
	result = getblk(bh->b_dev, tmp, BLOCK_SIZE);
	if (*p) {
		minix_free_block(inode->i_sb,tmp);
		brelse(result);
		goto repeat;
	}
	*p = tmp;
	mark_buffer_dirty(bh, 1);
	brelse(bh);
	return result;
}

static struct buffer_head * V2_minix_getblk(struct inode * inode, int block,
					    int create)
{
	struct buffer_head * bh;

	if (block<0) {
		printk("minix_getblk: block<0");
		return NULL;
	}
	if (block >= inode->i_sb->u.minix_sb.s_max_size/BLOCK_SIZE) {
		printk("minix_getblk: block>big");
		return NULL;
	}
	if (block < 7)
		return V2_inode_getblk(inode,block,create);
	block -= 7;
	if (block < 256) {
		bh = V2_inode_getblk(inode,7,create);
		return V2_block_getblk(inode, bh, block, create);
	}
	block -= 256;
	if (block < 256*256) {
		bh = V2_inode_getblk(inode,8,create);
		bh = V2_block_getblk(inode, bh, (block>>8) & 255, create);
		return V2_block_getblk(inode, bh, block & 255, create);
	}
	block -= 256*256;
	bh = V2_inode_getblk(inode,9,create);
	bh = V2_block_getblk(inode, bh, (block >> 16) & 255, create);
	bh = V2_block_getblk(inode, bh, (block >> 8) & 255, create);
	return V2_block_getblk(inode, bh, block & 255, create);
}

/*
 * the global minix fs getblk function.
 */
struct buffer_head * minix_getblk(struct inode * inode, int block, int create)
{
	if (INODE_VERSION(inode) == MINIX_V1)
		return V1_minix_getblk(inode,block,create);
	else
		return V2_minix_getblk(inode,block,create);
}

struct buffer_head * minix_bread(struct inode * inode, int block, int create)
{
	struct buffer_head * bh;

	bh = minix_getblk(inode,block,create);
	if (!bh || buffer_uptodate(bh))
		return bh;
	ll_rw_block(READ, 1, &bh);
	wait_on_buffer(bh);
	if (buffer_uptodate(bh))
		return bh;
	brelse(bh);
	return NULL;
}

/*
 * The minix V1 function to read an inode.
 */
static void V1_minix_read_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix_inode * raw_inode;
	int block, ino;

	ino = inode->i_ino;
	inode->i_op = NULL;
	inode->i_mode = 0;
	if (!ino || ino > inode->i_sb->u.minix_sb.s_ninodes) {
		printk("Bad inode number on dev %s"
		       ": %d is out of range\n",
			kdevname(inode->i_dev), ino);
		return;
	}
	block = 2 + inode->i_sb->u.minix_sb.s_imap_blocks +
		    inode->i_sb->u.minix_sb.s_zmap_blocks +
		    (ino-1)/MINIX_INODES_PER_BLOCK;
	if (!(bh=bread(inode->i_dev,block, BLOCK_SIZE))) {
		printk("Major problem: unable to read inode from dev "
		       "%s\n", kdevname(inode->i_dev));
		return;
	}
	raw_inode = ((struct minix_inode *) bh->b_data) +
		    (ino-1)%MINIX_INODES_PER_BLOCK;
	inode->i_mode = raw_inode->i_mode;
	inode->i_uid = raw_inode->i_uid;
	inode->i_gid = raw_inode->i_gid;
	inode->i_nlink = raw_inode->i_nlinks;
	inode->i_size = raw_inode->i_size;
	inode->i_mtime = inode->i_atime = inode->i_ctime = raw_inode->i_time;
	inode->i_blocks = inode->i_blksize = 0;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		inode->i_rdev = to_kdev_t(raw_inode->i_zone[0]);
	else for (block = 0; block < 9; block++)
		inode->u.minix_i.u.i1_data[block] = raw_inode->i_zone[block];
	brelse(bh);
	if (S_ISREG(inode->i_mode))
		inode->i_op = &minix_file_inode_operations;
	else if (S_ISDIR(inode->i_mode))
		inode->i_op = &minix_dir_inode_operations;
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &minix_symlink_inode_operations;
	else if (S_ISCHR(inode->i_mode))
		inode->i_op = &chrdev_inode_operations;
	else if (S_ISBLK(inode->i_mode))
		inode->i_op = &blkdev_inode_operations;
	else if (S_ISFIFO(inode->i_mode))
		init_fifo(inode);
}

/*
 * The minix V2 function to read an inode.
 */
static void V2_minix_read_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix2_inode * raw_inode;
	int block, ino;

	ino = inode->i_ino;
	inode->i_op = NULL;
	inode->i_mode = 0;
	if (!ino || ino > inode->i_sb->u.minix_sb.s_ninodes) {
		printk("Bad inode number on dev %s"
		       ": %d is out of range\n",
			kdevname(inode->i_dev), ino);
		return;
	}
	block = 2 + inode->i_sb->u.minix_sb.s_imap_blocks +
		    inode->i_sb->u.minix_sb.s_zmap_blocks +
		    (ino-1)/MINIX2_INODES_PER_BLOCK;
	if (!(bh=bread(inode->i_dev,block, BLOCK_SIZE))) {
		printk("Major problem: unable to read inode from dev "
		       "%s\n", kdevname(inode->i_dev));
		return;
	}
	raw_inode = ((struct minix2_inode *) bh->b_data) +
		    (ino-1)%MINIX2_INODES_PER_BLOCK;
	inode->i_mode = raw_inode->i_mode;
	inode->i_uid = raw_inode->i_uid;
	inode->i_gid = raw_inode->i_gid;
	inode->i_nlink = raw_inode->i_nlinks;
	inode->i_size = raw_inode->i_size;
	inode->i_mtime = raw_inode->i_mtime;
	inode->i_atime = raw_inode->i_atime;
	inode->i_ctime = raw_inode->i_ctime;
	inode->i_blocks = inode->i_blksize = 0;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		inode->i_rdev = to_kdev_t(raw_inode->i_zone[0]);
	else for (block = 0; block < 10; block++)
		inode->u.minix_i.u.i2_data[block] = raw_inode->i_zone[block];
	brelse(bh);
	if (S_ISREG(inode->i_mode))
		inode->i_op = &minix_file_inode_operations;
	else if (S_ISDIR(inode->i_mode))
		inode->i_op = &minix_dir_inode_operations;
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &minix_symlink_inode_operations;
	else if (S_ISCHR(inode->i_mode))
		inode->i_op = &chrdev_inode_operations;
	else if (S_ISBLK(inode->i_mode))
		inode->i_op = &blkdev_inode_operations;
	else if (S_ISFIFO(inode->i_mode))
		init_fifo(inode);
}

/*
 * The global function to read an inode.
 */
static void minix_read_inode(struct inode * inode)
{
	if (INODE_VERSION(inode) == MINIX_V1)
		V1_minix_read_inode(inode);
	else
		V2_minix_read_inode(inode);
}

/*
 * The minix V1 function to synchronize an inode.
 */
static struct buffer_head * V1_minix_update_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix_inode * raw_inode;
	int ino, block;

	ino = inode->i_ino;
	if (!ino || ino > inode->i_sb->u.minix_sb.s_ninodes) {
		printk("Bad inode number on dev %s"
		       ": %d is out of range\n",
			kdevname(inode->i_dev), ino);
		return 0;
	}
	block = 2 + inode->i_sb->u.minix_sb.s_imap_blocks + inode->i_sb->u.minix_sb.s_zmap_blocks +
		(ino-1)/MINIX_INODES_PER_BLOCK;
	if (!(bh=bread(inode->i_dev, block, BLOCK_SIZE))) {
		printk("unable to read i-node block\n");
		return 0;
	}
	raw_inode = ((struct minix_inode *)bh->b_data) +
		(ino-1)%MINIX_INODES_PER_BLOCK;
	raw_inode->i_mode = inode->i_mode;
	raw_inode->i_uid = inode->i_uid;
	raw_inode->i_gid = inode->i_gid;
	raw_inode->i_nlinks = inode->i_nlink;
	raw_inode->i_size = inode->i_size;
	raw_inode->i_time = inode->i_mtime;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->i_zone[0] = kdev_t_to_nr(inode->i_rdev);
	else for (block = 0; block < 9; block++)
		raw_inode->i_zone[block] = inode->u.minix_i.u.i1_data[block];
	mark_buffer_dirty(bh, 1);
	return bh;
}

/*
 * The minix V2 function to synchronize an inode.
 */
static struct buffer_head * V2_minix_update_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix2_inode * raw_inode;
	int ino, block;

	ino = inode->i_ino;
	if (!ino || ino > inode->i_sb->u.minix_sb.s_ninodes) {
		printk("Bad inode number on dev %s"
		       ": %d is out of range\n",
			kdevname(inode->i_dev), ino);
		return 0;
	}
	block = 2 + inode->i_sb->u.minix_sb.s_imap_blocks + inode->i_sb->u.minix_sb.s_zmap_blocks +
		(ino-1)/MINIX2_INODES_PER_BLOCK;
	if (!(bh=bread(inode->i_dev, block, BLOCK_SIZE))) {
		printk("unable to read i-node block\n");
		return 0;
	}
	raw_inode = ((struct minix2_inode *)bh->b_data) +
		(ino-1)%MINIX2_INODES_PER_BLOCK;
	raw_inode->i_mode = inode->i_mode;
	raw_inode->i_uid = inode->i_uid;
	raw_inode->i_gid = inode->i_gid;
	raw_inode->i_nlinks = inode->i_nlink;
	raw_inode->i_size = inode->i_size;
	raw_inode->i_mtime = inode->i_mtime;
	raw_inode->i_atime = inode->i_atime;
	raw_inode->i_ctime = inode->i_ctime;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->i_zone[0] = kdev_t_to_nr(inode->i_rdev);
	else for (block = 0; block < 10; block++)
		raw_inode->i_zone[block] = inode->u.minix_i.u.i2_data[block];
	mark_buffer_dirty(bh, 1);
	return bh;
}

static struct buffer_head *minix_update_inode(struct inode *inode)
{
	if (INODE_VERSION(inode) == MINIX_V1)
		return V1_minix_update_inode(inode);
	else
		return V2_minix_update_inode(inode);
}

static void minix_write_inode(struct inode * inode)
{
	struct buffer_head *bh;

	bh = minix_update_inode(inode);
	brelse(bh);
}

int minix_sync_inode(struct inode * inode)
{
	int err = 0;
	struct buffer_head *bh;

	bh = minix_update_inode(inode);
	if (bh && buffer_dirty(bh))
	{
		ll_rw_block(WRITE, 1, &bh);
		wait_on_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh))
		{
			printk ("IO error syncing minix inode ["
				"%s:%08lx]\n",
				kdevname(inode->i_dev), inode->i_ino);
			err = -1;
		}
	}
	else if (!bh)
		err = -1;
	brelse (bh);
	return err;
}

static struct file_system_type minix_fs_type = {
	"minix",
	FS_REQUIRES_DEV,
	minix_read_super,
	NULL
};

__initfunc(int init_minix_fs(void))
{
        return register_filesystem(&minix_fs_type);
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	return init_minix_fs();
}

void cleanup_module(void)
{
	unregister_filesystem(&minix_fs_type);
}

#endif

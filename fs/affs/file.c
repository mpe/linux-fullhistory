/*
 *  linux/fs/affs/file.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1992  Eric Youngdale Modified for ISO9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 *
 *  affs regular file handling primitives
 */

#include <asm/segment.h>
#include <asm/system.h>
#include <linux/sched.h>
#include <linux/affs_fs.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/locks.h>
#include <linux/dirent.h>
#include <linux/fs.h>
#include <linux/amigaffs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

static int affs_file_read_ofs(struct inode *inode, struct file *filp, char *buf, int count);
static int affs_file_write(struct inode *inode, struct file *filp, const char *buf, int count);
static int affs_file_write_ofs(struct inode *inode, struct file *filp, const char *buf, int count);
static void affs_release_file(struct inode *inode, struct file *filp);

static struct file_operations affs_file_operations = {
	NULL,			/* lseek - default */
	generic_file_read,	/* read */
	affs_file_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	generic_file_mmap,	/* mmap */
	NULL,			/* no special open is needed */
	affs_release_file,	/* release */
	file_fsync		/* brute force, but works */
};

struct inode_operations affs_file_inode_operations = {
	&affs_file_operations,	/* default file operations */
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
	generic_readpage,	/* readpage */
	NULL,			/* writepage */
	affs_bmap,		/* bmap */
	affs_truncate,		/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

static struct file_operations affs_file_operations_ofs = {
	NULL,			/* lseek - default */
	affs_file_read_ofs,	/* read */
	affs_file_write_ofs,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open is needed */
	affs_release_file,	/* release */
	file_fsync		/* brute force, but works */
};

struct inode_operations affs_file_inode_operations_ofs = {
	&affs_file_operations_ofs,	/* default file operations */
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
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	affs_truncate,		/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

int
affs_bmap(struct inode *inode, int block)
{
	struct buffer_head	*bh;
	int			 ext, key;
	int			 ptype, stype;

	pr_debug("AFFS: bmap(%lu,%d)\n",inode->i_ino,block);

	if (block < 0) {
		printk("affs_bmap: block < 0\n");
		return 0;
	}

	/* If this is a hard link, quietly exchange the inode with the original */

	key = inode->u.affs_i.i_original ? inode->u.affs_i.i_original : inode->i_ino;

	ext = block / AFFS_I2HSIZE(inode);
	if (ext) {
		if (ext > inode->u.affs_i.i_max_ext)
			ext = inode->u.affs_i.i_max_ext;
		if (ext)
			key = inode->u.affs_i.i_ext[ext -  1];
		block -= ext * AFFS_I2HSIZE(inode);
	}

	for (;;) {
		bh = affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode));
		if (!bh)
			return 0;
		if (affs_checksum_block(AFFS_I2BSIZE(inode),bh->b_data,&ptype,&stype) ||
		    (ptype != T_SHORT && ptype != T_LIST) || stype != ST_FILE) {
			affs_brelse(bh);
			return 0;
		}
		if (block < AFFS_I2HSIZE(inode))
			break;
		block -= AFFS_I2HSIZE(inode);
		key    = htonl(FILE_END(bh->b_data,inode)->extension);
		affs_brelse(bh);
		if (ext < EXT_CACHE_SIZE - 1) {
			inode->u.affs_i.i_ext[ext] = key;
			inode->u.affs_i.i_max_ext  = ++ext;
		}
	}
	key = AFFS_GET_HASHENTRY(bh->b_data,(AFFS_I2HSIZE(inode) - 1) - block);
	affs_brelse(bh);
	return key;
}

struct buffer_head *
affs_getblock(struct inode *inode, int block)
{
	struct buffer_head	*bh;
	struct buffer_head	*ebh;
	int			 key;
	int			 ext;
	int			 cnt, j, pt;

	pr_debug("AFFS: getblock(%lu,%d)\n",inode->i_ino,block);

	if (block < 0)
		return NULL;
	key = inode->i_ino;
	pt  = T_SHORT;

	ext = block / AFFS_I2HSIZE(inode);
	if (ext) {
		if (ext > inode->u.affs_i.i_max_ext)
			ext = inode->u.affs_i.i_max_ext;
		if (ext) {
			key    = inode->u.affs_i.i_ext[ext - 1];
			block -= ext * AFFS_I2HSIZE(inode);
			pt     = T_LIST;
		}
	}

	for (;;) {
		bh = affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode));
		if (!bh)
			return NULL;
		if (affs_checksum_block(AFFS_I2BSIZE(inode),bh->b_data,&cnt,&j) ||
		    cnt != pt || j != ST_FILE) {
		    	printk("AFFS: getblock(): inode %d is not a valid %s\n",key,
			       pt == T_SHORT ? "file header" : "extension block");
			affs_brelse(bh);
			return NULL;
		}
		j = htonl(((struct file_front *)bh->b_data)->block_count);
		while (j < AFFS_I2HSIZE(inode) && j <= block) {
			key = affs_new_data(inode);
			if (!key)
				break;
			lock_super(inode->i_sb);
			if (AFFS_BLOCK(bh->b_data,inode,j)) {
				unlock_super(inode->i_sb);
				printk("AFFS: getblock(): block already allocated\n");
				affs_free_block(inode->i_sb,key);
				j++;
				continue;
			}
			unlock_super(inode->i_sb);
			AFFS_BLOCK(bh->b_data,inode,j) = ntohl(key);
			j++;
		}
		if (pt == T_SHORT)
			((struct file_front *)bh->b_data)->first_data =
								AFFS_BLOCK(bh->b_data,inode,0);
		((struct file_front *)bh->b_data)->block_count = ntohl(j);
		affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
		mark_buffer_dirty(bh,1);

		if (block < j)
			break;
		if (j < AFFS_I2HSIZE(inode)) {
			affs_brelse(bh);
			return NULL;
		}

		block -= AFFS_I2HSIZE(inode);
		key    = htonl(FILE_END(bh->b_data,inode)->extension);
		if (!key) {
			key = affs_new_header(inode);
			if (!key) {
				affs_brelse(bh);
				return NULL;
			}
			ebh = affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode));
			if (!ebh) {
				affs_free_block(inode->i_sb,key);
				return NULL;
			}
			((struct file_front *)ebh->b_data)->primary_type = ntohl(T_LIST);
			((struct file_front *)ebh->b_data)->own_key      = ntohl(key);
			FILE_END(ebh->b_data,inode)->secondary_type      = ntohl(ST_FILE);
			FILE_END(ebh->b_data,inode)->parent              = ntohl(inode->i_ino);
			affs_fix_checksum(AFFS_I2BSIZE(inode),ebh->b_data,5);
			FILE_END(bh->b_data,inode)->extension = ntohl(key);
			affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
			mark_buffer_dirty(bh,1);
			affs_brelse(bh);
			bh = ebh;
		}
		affs_brelse(bh);
		pt = T_LIST;
		if (ext < EXT_CACHE_SIZE - 1) {
			inode->u.affs_i.i_ext[ext] = key;
			inode->u.affs_i.i_max_ext  = ++ext;
		}
	}
	key = htonl(AFFS_BLOCK(bh->b_data,inode,block));
	affs_brelse(bh);
	if (!key)
		return NULL;
	
	return affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode));
}

struct buffer_head *
affs_getblock_ofs(struct inode *inode, int block, int *blk_key)
{
	struct buffer_head	*bh;
	struct buffer_head	*pbh;
	struct buffer_head	*ebh;
	int			 key;
	int			 ext;
	int			 cnt, j, pt;

	pr_debug("AFFS: getblock_ofs(%lu,%d)\n",inode->i_ino,block);

	if (block < 0)
		return NULL;
	key = inode->i_ino;
	pt  = T_SHORT;

	ext = block / AFFS_I2HSIZE(inode);
	if (ext) {
		if (ext > inode->u.affs_i.i_max_ext)
			ext = inode->u.affs_i.i_max_ext;
		if (ext) {
			key    = inode->u.affs_i.i_ext[ext - 1];
			block -= ext * AFFS_I2HSIZE(inode);
			pt     = T_LIST;
		}
	}

	pbh = NULL;
	for (;;) {
		bh = affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode));
		if (!bh)
			return NULL;
		if (affs_checksum_block(AFFS_I2BSIZE(inode),bh->b_data,&cnt,&j) ||
		    cnt != pt || j != ST_FILE) {
		    	printk("AFFS: getblock(): inode %d is not a valid %s\n",key,
			       pt == T_SHORT ? "file header" : "extension block");
			affs_brelse(bh);
			return NULL;
		}
		j = htonl(((struct file_front *)bh->b_data)->block_count);
		while (j < AFFS_I2HSIZE(inode) && j <= block) {
			if (!pbh && inode->u.affs_i.i_lastblock >= 0) {
				if (j > 0)
					pbh = affs_bread(inode->i_dev,ntohl(AFFS_BLOCK(bh->b_data,inode,j - 1)),
							 AFFS_I2BSIZE(inode));
				else
					pbh = affs_getblock_ofs(inode,inode->u.affs_i.i_lastblock,&key);
				if (!pbh) {
					printk("AFFS: getblock(): cannot get last block in file\n");
					break;
				}
			}
			key = affs_new_data(inode);
			if (!key)
				break;
			lock_super(inode->i_sb);
			if (AFFS_BLOCK(bh->b_data,inode,j)) {
				unlock_super(inode->i_sb);
				printk("AFFS: getblock(): block already allocated\n");
				affs_free_block(inode->i_sb,key);
				j++;
				continue;
			}
			AFFS_BLOCK(bh->b_data,inode,j) = ntohl(key);
			unlock_super(inode->i_sb);
			ebh = affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode));
			if (!ebh) {
				printk("AFFS: getblock(): cannot get block %d\n",key);
				affs_free_block(inode->i_sb,key);
				AFFS_BLOCK(bh->b_data,inode,j) = 0;
				break;
			}
			inode->u.affs_i.i_lastblock++;
			DATA_FRONT(ebh)->primary_type    = ntohl(T_DATA);
			DATA_FRONT(ebh)->header_key      = ntohl(inode->i_ino);
			DATA_FRONT(ebh)->sequence_number = ntohl(inode->u.affs_i.i_lastblock + 1);
			if (pbh) {
				DATA_FRONT(pbh)->data_size = ntohl(AFFS_I2BSIZE(inode) - 24);
				DATA_FRONT(pbh)->next_data = ntohl(key);
				affs_fix_checksum(AFFS_I2BSIZE(inode),pbh->b_data,5);
				mark_buffer_dirty(pbh,0);
				mark_buffer_dirty(ebh,0);
				affs_brelse(pbh);
			}
			pbh = ebh;
			j++;
		}
		if (pt == T_SHORT)
			((struct file_front *)bh->b_data)->first_data =
								AFFS_BLOCK(bh->b_data,inode,0);
		((struct file_front *)bh->b_data)->block_count = ntohl(j);
		affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
		mark_buffer_dirty(bh,1);

		if (block < j) {
			if (pbh)
				affs_brelse(pbh);
			break;
		}
		if (j < AFFS_I2HSIZE(inode)) {
			affs_brelse(bh);
			return NULL;
		}

		block -= AFFS_I2HSIZE(inode);
		key    = htonl(FILE_END(bh->b_data,inode)->extension);
		if (!key) {
			key = affs_new_header(inode);
			if (!key) {
				affs_brelse(bh);
				return NULL;
			}
			ebh = affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode));
			if (!ebh) {
				affs_free_block(inode->i_sb,key);
				return NULL;
			}
			((struct file_front *)ebh->b_data)->primary_type = ntohl(T_LIST);
			((struct file_front *)ebh->b_data)->own_key      = ntohl(key);
			FILE_END(ebh->b_data,inode)->secondary_type      = ntohl(ST_FILE);
			FILE_END(ebh->b_data,inode)->parent              = ntohl(inode->i_ino);
			affs_fix_checksum(AFFS_I2BSIZE(inode),ebh->b_data,5);
			FILE_END(bh->b_data,inode)->extension = ntohl(key);
			affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
			mark_buffer_dirty(bh,1);
			affs_brelse(bh);
			bh = ebh;
		}
		affs_brelse(bh);
		pt = T_LIST;
		if (ext < EXT_CACHE_SIZE - 1) {
			inode->u.affs_i.i_ext[ext] = key;
			inode->u.affs_i.i_max_ext  = ++ext;
		}
	}
	key = htonl(AFFS_BLOCK(bh->b_data,inode,block));
	affs_brelse(bh);
	if (!key)
		return NULL;
	*blk_key = key;

	return affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode));
}

/* This could be made static, regardless of what the former comment said.
 * You cannot directly read affs directories.
 */

static int
affs_file_read_ofs(struct inode *inode, struct file *filp, char *buf, int count)
{
	char *start;
	int left, offset, size, sector;
	int blocksize;
	struct buffer_head *bh;
	void *data;

	pr_debug("AFFS: file_read_ofs(ino=%lu,pos=%lu,%d)\n",inode->i_ino,(long)filp->f_pos,count);

	if (!inode) {
		printk("affs_file_read: inode = NULL\n");
		return -EINVAL;
	}
	blocksize = AFFS_I2BSIZE(inode) - 24;
	if (!(S_ISREG(inode->i_mode))) {
		pr_debug("affs_file_read: mode = %07o\n",inode->i_mode);
		return -EINVAL;
	}
	if (filp->f_pos >= inode->i_size || count <= 0)
		return 0;

	start = buf;
	for (;;) {
		left = MIN (inode->i_size - filp->f_pos,count - (buf - start));
		if (!left)
			break;
		sector = affs_bmap(inode,(__u32)filp->f_pos / blocksize);
		if (!sector)
			break;
		offset = (__u32)filp->f_pos % blocksize;
		bh = affs_bread(inode->i_dev,sector,AFFS_I2BSIZE(inode));
		if (!bh)
			break;
		data = bh->b_data + 24;
		size = MIN(blocksize - offset,left);
		filp->f_pos += size;
		memcpy_tofs(buf,data + offset,size);
		buf += size;
		affs_brelse(bh);
	}
	if (start == buf)
		return -EIO;
	return buf - start;
}

static int
affs_file_write(struct inode *inode, struct file *filp, const char *buf, int count)
{
	off_t			 pos;
	int			 written;
	int			 c;
	int			 blocksize;
	struct buffer_head	*bh;
	struct inode		*ino;
	char			*p;

	pr_debug("AFFS: file_write(ino=%lu,pos=%lu,count=%d)\n",inode->i_ino,
		(unsigned long)filp->f_pos,count);

	ino = NULL;
	if (!inode) {
		printk("AFFS: file_write(): inode=NULL\n");
		return -EINVAL;
	}
	if (inode->u.affs_i.i_original) {
		ino = iget(inode->i_sb,inode->u.affs_i.i_original);
		if (!ino) {
			printk("AFFS: could not follow link from inode %lu to %d\n",
			       inode->i_ino,inode->u.affs_i.i_original);
			return -EINVAL;
		}
		inode = ino;
	}
	if (!S_ISREG(inode->i_mode)) {
		printk("AFFS: file_write(): mode=%07o\n",inode->i_mode);
		iput(inode);
		return -EINVAL;
	}
	if (filp->f_flags & O_APPEND) {
		pos = inode->i_size;
	} else
		pos = filp->f_pos;
	written   = 0;
	blocksize = AFFS_I2BSIZE(inode);

	while (written < count) {
		bh = affs_getblock(inode,pos / blocksize);
		if (!bh) {
			if (!written)
				written = -ENOSPC;
			break;
		}
		c = blocksize - (pos % blocksize);
		if (c > count - written)
			c = count - written;
		if (c != blocksize && !buffer_uptodate(bh)) {
			ll_rw_block(READ,1,&bh);
			wait_on_buffer(bh);
			if (!buffer_uptodate(bh)) {
				affs_brelse(bh);
				if (!written)
					written = -EIO;
				break;
			}
		}
		p = (pos % blocksize) + bh->b_data;
		memcpy_fromfs(p,buf,c);
		update_vm_cache(inode,pos,p,c);
		mark_buffer_uptodate(bh,1);
		mark_buffer_dirty(bh,0);
		affs_brelse(bh);
		pos     += c;
		written += c;
		buf     += c;
	}
	if (pos > inode->i_size)
		inode->i_size = pos;
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	filp->f_pos    = pos;
	inode->i_dirt  = 1;
	iput(ino);
	return written;
}

static int
affs_file_write_ofs(struct inode *inode, struct file *filp, const char *buf, int count)
{
	off_t			 pos;
	int			 written;
	int			 c;
	int			 key;
	int			 blocksize;
	struct buffer_head	*bh;
	struct inode		*ino;
	char			*p;

	pr_debug("AFFS: file_write_ofs(ino=%lu,pos=%lu,count=%d)\n",inode->i_ino,
		(unsigned long)filp->f_pos,count);

	if (!inode) {
		printk("AFFS: file_write_ofs(): inode=NULL\n");
		return -EINVAL;
	}
	ino = NULL;
	if (inode->u.affs_i.i_original) {
		ino = iget(inode->i_sb,inode->u.affs_i.i_original);
		if (!ino) {
			printk("AFFS: could not follow link from inode %lu to %d\n",
			       inode->i_ino,inode->u.affs_i.i_original);
			return -EINVAL;
		}
		inode = ino;
	}
	if (!S_ISREG(inode->i_mode)) {
		printk("AFFS: file_write_ofs(): mode=%07o\n",inode->i_mode);
		iput(inode);
		return -EINVAL;
	}
	if (filp->f_flags & O_APPEND)
		pos = inode->i_size;
	else
		pos = filp->f_pos;

	bh        = NULL;
	blocksize = AFFS_I2BSIZE(inode) - 24;
	written   = 0;
	while (written < count) {
		bh = affs_getblock_ofs(inode,pos / blocksize,&key);
		if (!bh) {
			if (!written)
				written = -ENOSPC;
			break;
		}
		c = blocksize - (pos % blocksize);
		if (c > count - written)
			c = count - written;
		if (c != blocksize && !buffer_uptodate(bh)) {
			ll_rw_block(READ,1,&bh);
			wait_on_buffer(bh);
			if (!buffer_uptodate(bh)) {
				affs_brelse(bh);
				if (!written)
					written = -EIO;
				break;
			}
		}
		p = (pos % blocksize) + bh->b_data + 24;
		memcpy_fromfs(p,buf,c);
		update_vm_cache(inode,pos,p,c);

		pos     += c;
		buf     += c;
		written += c;
		DATA_FRONT(bh)->data_size = ntohl(htonl(DATA_FRONT(bh)->data_size) + c);
		affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
		mark_buffer_uptodate(bh,1);
		mark_buffer_dirty(bh,0);
		affs_brelse(bh);
	}
	if (pos > inode->i_size)
		inode->i_size = pos;
	filp->f_pos = pos;
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_dirt  = 1;
	iput(ino);
	return written;
}

void
affs_truncate(struct inode *inode)
{
	struct buffer_head	*bh;
	struct buffer_head	*ebh;
	struct inode		*ino;
	struct affs_zone	*zone;
	int	 first;
	int	 block;
	int	 key;
	int	*keyp;
	int	 ekey;
	int	 ptype, stype;
	int	 freethis;
	int	 blocksize;
	int	 rem;
	int	 ext;

	pr_debug("AFFS: file_truncate(inode=%ld,size=%lu)\n",inode->i_ino,inode->i_size);

	ino = NULL;
	if (inode->u.affs_i.i_original) {
		ino = iget(inode->i_sb,inode->u.affs_i.i_original);
		if (!ino) {
			printk("AFFS: truncate(): cannot follow link from %lu to %u\n",
			       inode->i_ino,inode->u.affs_i.i_original);
			return;
		}
		inode = ino;
	}
	blocksize = AFFS_I2BSIZE(inode) - ((inode->i_sb->u.affs_sb.s_flags & SF_OFS) ? 24 : 0);
	first = (inode->i_size + blocksize - 1) / blocksize;
	if (inode->u.affs_i.i_lastblock < first - 1) {
		if (inode->i_sb->u.affs_sb.s_flags & SF_OFS)
			bh = affs_getblock_ofs(inode,first - 1,&ekey);
		else
			bh = affs_getblock(inode,first - 1);

		while (inode->u.affs_i.i_pa_cnt) {		/* Free any preallocated blocks */
			affs_free_block(inode->i_sb,
					inode->u.affs_i.i_data[inode->u.affs_i.i_pa_next++]);
			inode->u.affs_i.i_pa_next &= MAX_PREALLOC - 1;
			inode->u.affs_i.i_pa_cnt--;
		}
		if (inode->u.affs_i.i_zone) {
			lock_super(inode->i_sb);
			zone = &inode->i_sb->u.affs_sb.s_zones[inode->u.affs_i.i_zone];
			if (zone->z_ino == inode->i_ino)
				zone->z_ino = 0;
			unlock_super(inode->i_sb);
		}
		if (!bh) {
			printk("AFFS: truncate(): Cannot extend file\n");
			inode->i_size = blocksize * (inode->u.affs_i.i_lastblock + 1);
		} else if (inode->i_sb->u.affs_sb.s_flags & SF_OFS) {
			rem = inode->i_size % blocksize;
			DATA_FRONT(bh)->data_size = ntohl(rem ? rem : blocksize);
			affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
			mark_buffer_dirty(bh,0);
		}
		affs_brelse(bh);
		iput(ino);
		return;
	}
	ekey  = inode->i_ino;
	ext   = 0;

	while (ekey) {
		if (!(bh = affs_bread(inode->i_dev,ekey,AFFS_I2BSIZE(inode)))) {
			printk("AFFS: truncate(): Can't read block %d\n",ekey);
			break;
		}
		ptype = htonl(((struct file_front *)bh->b_data)->primary_type);
		stype = htonl(FILE_END(bh->b_data,inode)->secondary_type);
		if (ekey == inode->i_ino && ptype == T_SHORT && stype == ST_LINKFILE &&
		    LINK_END(bh->b_data,inode)->original == 0) {
			pr_debug("AFFS: truncate(): dumping link\n");
			affs_brelse(bh);
			break;
		}
		if (stype != ST_FILE || (ptype != T_SHORT && ptype != T_LIST)) {
			printk("AFFS: truncate(): bad block (ptype=%d, stype=%d)\n",
			        ptype,stype);
			affs_brelse(bh);
			break;
		}
		/* Do not throw away file header */
		freethis = first == 0 && ekey != inode->i_ino;
		for ( block = first; block < AFFS_I2HSIZE(inode); block++) {
			keyp = &AFFS_BLOCK(bh->b_data,inode,block);
			key  = htonl(*keyp);
			if (key) {
				*keyp = 0;
				affs_free_block(inode->i_sb,key);
			} else {
				block = AFFS_I2HSIZE(inode);
				break;
			}
		}
		keyp = &GET_END_PTR(struct file_end,bh->b_data,AFFS_I2BSIZE(inode))->extension;
		key  = htonl(*keyp);
		if (first <= AFFS_I2HSIZE(inode)) {
			((struct file_front *)bh->b_data)->block_count = htonl(first);
			first = 0;
			*keyp = 0;
			if ((inode->i_sb->u.affs_sb.s_flags & SF_OFS) && first > 0) {
				block = htonl(AFFS_BLOCK(bh->b_data,inode,first - 1));
				if ((ebh = affs_bread(inode->i_dev,block,AFFS_I2BSIZE(inode)))) {
					if(!(affs_checksum_block(AFFS_I2BSIZE(inode),ebh->b_data,
					     &ptype,NULL))) {
						rem = inode->i_size % blocksize;
						rem = ntohl(rem ? blocksize : rem);
						((struct data_front *)ebh->b_data)->data_size = rem;
						((struct data_front *)ebh->b_data)->next_data = 0;
						affs_fix_checksum(AFFS_I2BSIZE(inode),ebh->b_data,5);
						mark_buffer_dirty(ebh,1);
					}
					affs_brelse(ebh);
				}
			}
		} else {
			first -= AFFS_I2HSIZE(inode);
		}
		if (freethis) {		/* Don't bother fixing checksum */
			affs_brelse(bh);
			affs_free_block(inode->i_sb,ekey);
		} else {
			affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
			mark_buffer_dirty(bh,1);
			affs_brelse(bh);
		}
		ekey = key;
	}
	inode->u.affs_i.i_lastblock = ((inode->i_size + blocksize - 1) / blocksize) - 1;
	inode->u.affs_i.i_max_ext   = 0;
	iput(ino);
}

static void
affs_release_file(struct inode *inode, struct file *filp)
{
	struct affs_zone	*zone;

	pr_debug("AFFS: release_file(ino=%lu)\n",inode->i_ino);

	if (filp->f_mode & 2) {		/* Free preallocated blocks */
		while (inode->u.affs_i.i_pa_cnt) {
			affs_free_block(inode->i_sb,
					inode->u.affs_i.i_data[inode->u.affs_i.i_pa_next++]);
			inode->u.affs_i.i_pa_next &= MAX_PREALLOC - 1;
			inode->u.affs_i.i_pa_cnt--;
		}
		if (inode->u.affs_i.i_zone) {
			lock_super(inode->i_sb);
			zone = &inode->i_sb->u.affs_sb.s_zones[inode->u.affs_i.i_zone];
			if (zone->z_ino == inode->i_ino)
				zone->z_ino = 0;
			unlock_super(inode->i_sb);
		}
	}
}

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

#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/sched.h>
#include <linux/affs_fs.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/stat.h>
#include <linux/locks.h>
#include <linux/dirent.h>
#include <linux/fs.h>
#include <linux/amigaffs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#if PAGE_SIZE < 4096
#error PAGE_SIZE must be at least 4096
#endif

static long affs_file_read_ofs(struct inode *inode, struct file *filp, char *buf,
			       unsigned long count);
static long affs_file_write(struct inode *inode, struct file *filp, const char *buf,
			    unsigned long count);
static long affs_file_write_ofs(struct inode *inode, struct file *filp, const char *buf,
				unsigned long count);
static int affs_open_file(struct inode *inode, struct file *filp);
static void affs_release_file(struct inode *inode, struct file *filp);

static struct file_operations affs_file_operations = {
	NULL,			/* lseek - default */
	generic_file_read,	/* read */
	affs_file_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	generic_file_mmap,	/* mmap */
	affs_open_file,		/* special open is needed */
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
	affs_open_file,		/* special open is needed */
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

#define AFFS_ISINDEX(x)	((x < 129) ||				\
			 (x < 512 && (x & 1) == 0) ||		\
			 (x < 1024 && (x & 3) == 0) ||		\
			 (x < 2048 && (x & 15) == 0) ||		\
			 (x < 4096 && (x & 63) == 0) ||		\
			 (x < 20480 && (x & 255) == 0) ||	\
			 (x < 36864 && (x & 511) == 0))

/* The keys of the extension blocks are stored in a 512-entry
 * deep cache. In order to save memory, not every key of later
 * extension blocks is stored - the larger the file gets, the
 * bigger the holes inbetween.
 */

static int
seqnum_to_index(int seqnum)
{
	/* All of the first 127 keys are stored */
	if (seqnum < 128)
		return seqnum;
	seqnum -= 128;

	/* Of the next 384 keys, every 2nd is kept */
	if (seqnum < (192 * 2))
		return 128 + (seqnum >> 1);
	seqnum -= 192 * 2;
	
	/* Every 4th of the next 512 */
	if (seqnum < (128 * 4))
		return 128 + 192 + (seqnum >> 2);
	seqnum -= 128 * 4;

	/* Every 16th of the next 1024 */
	if (seqnum < (64 * 16))
		return 128 + 192 + 128 + (seqnum >> 4);
	seqnum -= 64 * 16;

	/* Every 64th of the next 2048 */
	if (seqnum < (32 * 64))
		return 128 + 192 + 128 + 64 + (seqnum >> 6);
	seqnum -= 32 * 64;

	/* Every 256th of the next 16384 */
	if (seqnum < (64 * 256))
		return 128 + 192 + 128 + 64 + 32 + (seqnum >> 8);
	seqnum -= 64 * 256;

	/* Every 512th upto 36479 (1.3 GB with 512 byte blocks).
	 * Seeking to positions behind this will get slower
	 * than dead snails nailed to the ground. But if
	 * someone uses files that large with 512-byte blocks,
	 * he or she deserves no better.
	 */
	
	if (seqnum > (31 * 512))
		seqnum = 31 * 512;
	return 128 + 192 + 128 + 64 + 32 + 64 + (seqnum >> 9);
}

/* Now the other way round: Calculate the sequence
 * number of a extension block of a key at the
 * given index in the cache.
 */

static int
index_to_seqnum(int index)
{
	if (index < 128)
		return index;
	index -= 128;
	if (index < 192)
		return 128 + (index << 1);
	index -= 192;
	if (index < 128)
		return 128 + 192 * 2 + (index << 2);
	index -= 128;
	if (index < 64)
		return 128 + 192 * 2 + 128 * 4 + (index << 4);
	index -= 64;
	if (index < 32)
		return 128 + 192 * 2 + 128 * 4 + 64 * 16 + (index << 6);
	index -= 32;
	if (index < 64)
		return 128 + 192 * 2 + 128 * 4 + 64 * 16 + 32 * 64 + (index << 8);
	index -= 64;
	return 128 + 192 * 2 + 128 * 4 + 64 * 16 + 32 * 64 + 64 * 256 + (index << 9);
}

static int __inline__
calc_key(struct inode *inode, int *ext)
{
	int		  index;
	struct key_cache *kc;

	for (index = 0; index < 4; index++) {
		kc = &inode->u.affs_i.i_ec->kc[index];
		if (*ext == kc->kc_this_seq) {
			return kc->kc_this_key;
		} else if (*ext == kc->kc_this_seq + 1) {
			if (kc->kc_next_key)
				return kc->kc_next_key;
			else {
				(*ext)--;
				return kc->kc_this_key;
			}
		}
	}
	index = seqnum_to_index(*ext);
	if (index > inode->u.affs_i.i_ec->max_ext)
		index = inode->u.affs_i.i_ec->max_ext;
	*ext = index_to_seqnum(index);
	return inode->u.affs_i.i_ec->ec[index];
}

int
affs_bmap(struct inode *inode, int block)
{
	struct buffer_head	*bh;
	int			 ext, key, nkey;
	int			 ptype, stype;
	int			 index;
	int			 keycount;
	struct key_cache	*kc;
	struct key_cache	*tkc;
	struct timeval		 tv;
	__s32			*keyp;
	int			 i;

	pr_debug("AFFS: bmap(%lu,%d)\n",inode->i_ino,block);

	if (block < 0) {
		printk("affs_bmap: block < 0\n");
		return 0;
	}
	if (!inode->u.affs_i.i_ec) {
		printk("affs_bmap(): No ext_cache!?\n");
		return 0;
	}

	/* Try to find the requested key in the cache.
	 * In order to speed this up as much as possible,
	 * the cache line lookup is done in a seperate
	 * step.
	 */

	for (i = 0; i < 4; i++) {
		tkc = &inode->u.affs_i.i_ec->kc[i];
		/* Look in any cache if the key is there */
		if (block <= tkc->kc_last && block >= tkc->kc_first) {
			return tkc->kc_keys[block - tkc->kc_first];
		}
	}
	kc = NULL;
	tv = xtime;
	for (i = 0; i < 4; i++) {
		tkc = &inode->u.affs_i.i_ec->kc[i];
		if (tkc->kc_lru_time.tv_sec > tv.tv_sec)
			continue;
		if (tkc->kc_lru_time.tv_sec < tv.tv_sec ||
		    tkc->kc_lru_time.tv_usec < tv.tv_usec) {
			kc = tkc;
			tv = tkc->kc_lru_time;
		}
	}
	if (!kc)	/* Really shouldn't happen */
		kc = tkc;
	kc->kc_lru_time = xtime;
	keyp            = kc->kc_keys;
	kc->kc_first    = block;
	kc->kc_last     = -1;
	keycount        = AFFS_KCSIZE;

	/* Calculate sequence number of the extension block where the
	 * number of the requested block is stored. 0 means it's in
	 * the file header.
	 */

	ext    = block / AFFS_I2HSIZE(inode);
	key    = calc_key(inode,&ext);
	block -= ext * AFFS_I2HSIZE(inode);

	for (;;) {
		bh = affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode));
		if (!bh)
			return 0;
		index = seqnum_to_index(ext);
		if (index > inode->u.affs_i.i_ec->max_ext &&
		    (affs_checksum_block(AFFS_I2BSIZE(inode),bh->b_data,&ptype,&stype) ||
		     (ptype != T_SHORT && ptype != T_LIST) || stype != ST_FILE)) {
			affs_brelse(bh);
			return 0;
		}
		nkey = htonl(FILE_END(bh->b_data,inode)->extension);
		if (block < AFFS_I2HSIZE(inode)) {
			/* Fill cache as much as possible */
			if (keycount) {
				kc->kc_first = ext * AFFS_I2HSIZE(inode) + block;
				keycount     = keycount < AFFS_I2HSIZE(inode) - block ? keycount :
						AFFS_I2HSIZE(inode) - block;
				for (i = 0; i < keycount; i++)
					kc->kc_keys[i] = htonl(AFFS_BLOCK(bh->b_data,inode,block + i));
				kc->kc_last = kc->kc_first + i - 1;
			}
			break;
		}
		block -= AFFS_I2HSIZE(inode);
		affs_brelse(bh);
		ext++;
		if (index > inode->u.affs_i.i_ec->max_ext && AFFS_ISINDEX(ext)) {
			inode->u.affs_i.i_ec->ec[index] = nkey;
			inode->u.affs_i.i_ec->max_ext   = index;
		}
		key = nkey;
	}
	kc->kc_this_key = key;
	kc->kc_this_seq = ext;
	kc->kc_next_key = nkey;
	key = htonl(AFFS_BLOCK(bh->b_data,inode,block));
	affs_brelse(bh);
	return key;
}

struct buffer_head *
affs_getblock(struct inode *inode, int block)
{
	struct buffer_head	*bh;
	struct buffer_head	*ebh;
	struct buffer_head	*pbh;
	struct key_cache	*kc;
	int			 key, nkey;
	int			 ext;
	int			 cf, j, pt;
	int			 index;
	int			 ofs;

	pr_debug("AFFS: getblock(%lu,%d)\n",inode->i_ino,block);

	if (block < 0)
		return NULL;

	/* Writers always use cache line 3. In almost all cases, files
	 * will be written by only one process at the same time, and
	 * they also will be written in strict sequential order. Thus
	 * there is not much sense in looking whether the key of the
	 * requested block is available - it won't be there.
	 */
	kc     = &inode->u.affs_i.i_ec->kc[3];
	ofs    = inode->i_sb->u.affs_sb.s_flags & SF_OFS;
	ext    = block / AFFS_I2HSIZE(inode);
	key    = calc_key(inode,&ext);
	block -= ext * AFFS_I2HSIZE(inode);
	pt     = ext ? T_LIST : T_SHORT;
	pbh    = NULL;

	for (;;) {
		bh = affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode));
		if (!bh)
			return NULL;
		if (affs_checksum_block(AFFS_I2BSIZE(inode),bh->b_data,&cf,&j) ||
		    cf != pt || j != ST_FILE) {
		    	printk("AFFS: getblock(): inode %d is not a valid %s\n",key,
			       pt == T_SHORT ? "file header" : "extension block");
			affs_brelse(bh);
			return NULL;
		}
		j  = htonl(((struct file_front *)bh->b_data)->block_count);
		cf = 0;
		while (j < AFFS_I2HSIZE(inode) && j <= block) {
			if (ofs && !pbh && inode->u.affs_i.i_lastblock >= 0) {
				if (j > 0)
					pbh = affs_bread(inode->i_dev,ntohl(AFFS_BLOCK(bh->b_data,inode,j - 1)),
							 AFFS_I2BSIZE(inode));
				else
					pbh = affs_getblock(inode,inode->u.affs_i.i_lastblock);
				if (!pbh) {
					printk("AFFS: getblock(): cannot get last block in file\n");
					break;
				}
			}
			nkey = affs_new_data(inode);
			if (!nkey)
				break;
			lock_super(inode->i_sb);
			if (AFFS_BLOCK(bh->b_data,inode,j)) {
				unlock_super(inode->i_sb);
				printk("AFFS: getblock(): block already allocated\n");
				affs_free_block(inode->i_sb,nkey);
				j++;
				continue;
			}
			unlock_super(inode->i_sb);
			AFFS_BLOCK(bh->b_data,inode,j) = ntohl(nkey);
			if (ofs) {
				ebh = affs_bread(inode->i_dev,nkey,AFFS_I2BSIZE(inode));
				if (!ebh) {
					printk("AFFS: getblock(): cannot get block %d\n",nkey);
					affs_free_block(inode->i_sb,nkey);
					AFFS_BLOCK(bh->b_data,inode,j) = 0;
					break;
				}
				inode->u.affs_i.i_lastblock++;
				DATA_FRONT(ebh)->primary_type    = ntohl(T_DATA);
				DATA_FRONT(ebh)->header_key      = ntohl(inode->i_ino);
				DATA_FRONT(ebh)->sequence_number = ntohl(inode->u.affs_i.i_lastblock + 1);
				if (pbh) {
					DATA_FRONT(pbh)->data_size = ntohl(AFFS_I2BSIZE(inode) - 24);
					DATA_FRONT(pbh)->next_data = ntohl(nkey);
					affs_fix_checksum(AFFS_I2BSIZE(inode),pbh->b_data,5);
					mark_buffer_dirty(pbh,0);
					mark_buffer_dirty(ebh,0);
					affs_brelse(pbh);
				}
				pbh = ebh;
			}
			j++;
			cf = 1;
		}
		if (cf) {
			if (pt == T_SHORT)
				((struct file_front *)bh->b_data)->first_data =
								AFFS_BLOCK(bh->b_data,inode,0);
			((struct file_front *)bh->b_data)->block_count = ntohl(j);
			affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
			mark_buffer_dirty(bh,1);
		}

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
		ext++;
		if ((index = seqnum_to_index(ext)) > inode->u.affs_i.i_ec->max_ext &&
		    AFFS_ISINDEX(ext) && inode->u.affs_i.i_ec) {
			inode->u.affs_i.i_ec->ec[index] = key;
			inode->u.affs_i.i_ec->max_ext   = index;
		}
	}
	kc->kc_this_key = key;
	kc->kc_this_seq = ext;
	kc->kc_next_key = htonl(FILE_END(bh->b_data,inode)->extension);
	key = htonl(AFFS_BLOCK(bh->b_data,inode,block));
	affs_brelse(bh);
	if (!key)
		return NULL;
	
	return affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode));
}

/* This could be made static, regardless of what the former comment said.
 * You cannot directly read affs directories.
 */

static long
affs_file_read_ofs(struct inode *inode, struct file *filp, char *buf, unsigned long count)
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
		copy_to_user(buf,data + offset,size);
		buf += size;
		affs_brelse(bh);
	}
	if (start == buf)
		return -EIO;
	return buf - start;
}

static long
affs_file_write(struct inode *inode, struct file *filp, const char *buf, unsigned long count)
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
		copy_from_user(p,buf,c);
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

static long
affs_file_write_ofs(struct inode *inode, struct file *filp, const char *buf, unsigned long count)
{
	off_t			 pos;
	int			 written;
	int			 c;
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
		p = (pos % blocksize) + bh->b_data + 24;
		copy_from_user(p,buf,c);
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
			bh = affs_getblock(inode,first - 1);

		while (inode->u.affs_i.i_pa_cnt) {		/* Free any preallocated blocks */
			affs_free_block(inode->i_sb,
					inode->u.affs_i.i_data[inode->u.affs_i.i_pa_next++]);
			inode->u.affs_i.i_pa_next &= AFFS_MAX_PREALLOC - 1;
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

	/* Invalidate cache */
	if (inode->u.affs_i.i_ec) {
		inode->u.affs_i.i_ec->max_ext = 0;
		for (key = 0; key < 3; key++) {
			inode->u.affs_i.i_ec->kc[key].kc_next_key = 0;
			inode->u.affs_i.i_ec->kc[key].kc_last     = -1;
		}
	}

	iput(ino);
}

static int
affs_open_file(struct inode *inode, struct file *filp)
{
	int	 error;
	int	 key;
	int	 i;

	pr_debug("AFFS: open_file(ino=%lu)\n",inode->i_ino);

	error = 0;
	inode->u.affs_i.i_cache_users++;
	lock_super(inode->i_sb);
	if (!inode->u.affs_i.i_ec) {
		inode->u.affs_i.i_ec = (struct ext_cache *)get_free_page(GFP_KERNEL);
		if (!inode->u.affs_i.i_ec) {
			printk("AFFS: cache allocation failed\n");
			error = ENOMEM;
		} else {
			/* We only have to initialize non-zero values.
			 * get_free_page() zeroed the page already.
			 */
			key = inode->u.affs_i.i_original ? inode->u.affs_i.i_original : inode->i_ino;
			inode->u.affs_i.i_ec->ec[0] = key;
			for (i = 0; i < 4; i++) {
				inode->u.affs_i.i_ec->kc[i].kc_this_key = key;
				inode->u.affs_i.i_ec->kc[i].kc_last     = -1;
			}
		}
	}
	unlock_super(inode->i_sb);

	return error;
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
			inode->u.affs_i.i_pa_next &= AFFS_MAX_PREALLOC - 1;
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
	lock_super(inode->i_sb);
	if (--inode->u.affs_i.i_cache_users == 0) {
		if (inode->u.affs_i.i_ec) {
			free_page((unsigned long)inode->u.affs_i.i_ec);
			inode->u.affs_i.i_ec = NULL;
		}
	}
	unlock_super(inode->i_sb);
}

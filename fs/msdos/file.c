/*
 *  linux/fs/msdos/file.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  MS-DOS regular file handling primitives
 */

#include <asm/segment.h>
#include <asm/system.h>

#include <linux/sched.h>
#include <linux/locks.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/string.h>

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

#define PRINTK(x)
#define Printk(x) printk x

static struct file_operations msdos_file_operations = {
	NULL,			/* lseek - default */
	msdos_file_read,	/* read */
	msdos_file_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	generic_mmap,		/* mmap */
	NULL,			/* no special open is needed */
	NULL,			/* release */
	file_fsync		/* fsync */
};

struct inode_operations msdos_file_inode_operations = {
	&msdos_file_operations,	/* default file operations */
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
	msdos_bmap,		/* bmap */
	msdos_truncate,		/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

/*
	Read a file into user space
*/
int msdos_file_read(
	struct inode *inode,
	struct file *filp,
	char *buf,
	int count)
{
	char *start;
	int left,offset,size,cnt;
	#define MSDOS_PREFETCH	48
	struct {
		int file_sector;/* Next sector to read in the prefetch table */
				/* This is relative to the file, not the disk */
		struct buffer_head *bhlist[MSDOS_PREFETCH];	/* All buffers needed */
		int nblist;	/* Number of buffers in bhlist */
		int nolist;	/* index in bhlist */
		int fetched_max; /* End of pre fetch area */
	}pre;
	int i;
		

	if (!inode) {
		printk("msdos_file_read: inode = NULL\n");
		return -EINVAL;
	}
	/* S_ISLNK allows for UMSDOS. Should never happen for normal MSDOS */
	if (!S_ISREG(inode->i_mode) && !S_ISLNK(inode->i_mode)) {
		printk("msdos_file_read: mode = %07o\n",inode->i_mode);
		return -EINVAL;
	}
	if (filp->f_pos >= inode->i_size || count <= 0) return 0;
	/*
		Tell the buffer cache which block we expect to read in advance
		Since we are limited with the stack, we preread only MSDOS_PREFETCH
		because we have to keep the result into the local
		arrays pre.bhlist and bhreq.
		
		Each time we process one block in bhlist, we replace
		it by a new prefetch block if needed.
	*/
	PRINTK (("#### ino %ld pos %ld size %ld count %d\n",inode->i_ino,filp->f_pos,inode->i_size,count));
	{
		struct buffer_head *bhreq[MSDOS_PREFETCH];	/* Buffers not */
													/* already read */
		int nbreq;			/* Number of buffers in bhreq */
		/*
			We must prefetch complete block, so we must
			take in account the offset in the first block.
		*/
		int count_max = (filp->f_pos & (SECTOR_SIZE-1)) + count;
		int   to_reada;	/* How many block to read all at once */
		pre.file_sector = filp->f_pos >> SECTOR_BITS;
		to_reada = count_max / SECTOR_SIZE;
		if (count_max & (SECTOR_SIZE-1)) to_reada++;
		if (filp->f_reada){
			int min_read = read_ahead[MAJOR(inode->i_dev)];
			if (min_read > to_reada) to_reada = min_read;
		}
		if (to_reada > MSDOS_PREFETCH) to_reada = MSDOS_PREFETCH;
		nbreq = pre.nblist = 0;
		for (i=0; i<to_reada; i++){
			int sector;
			struct buffer_head *bh;
			if (!(sector = msdos_smap(inode,pre.file_sector++))) break;
			PRINTK (("fsector1 %d -> %d\n",pre.file_sector-1,sector));
			bh = getblk(inode->i_dev,sector,SECTOR_SIZE);
			if (bh == NULL)	break;
			pre.bhlist[pre.nblist++] = bh;
			if (!bh->b_uptodate){
				bhreq[nbreq++] = bh;
			}
		}
		pre.fetched_max = pre.file_sector * SECTOR_SIZE;
		if (nbreq > 0) ll_rw_block (READ,nbreq,bhreq);
	}
	start = buf;
	pre.nolist = 0;
	while ((left = MIN(inode->i_size-filp->f_pos,count-(buf-start))) > 0){
		struct buffer_head *bh = pre.bhlist[pre.nolist];
		char *data;
		if (bh == NULL) break;
		PRINTK (("file_read pos %ld nblist %d %d %d\n",filp->f_pos,pre.nblist,pre.fetched,count));
		pre.bhlist[pre.nolist] = NULL;
		if (left + filp->f_pos > pre.fetched_max){
			int sector;
			if ((sector = msdos_smap(inode,pre.file_sector++))){
				struct buffer_head *bhreq[1];
				PRINTK (("fsector2 %d -> %d\n",pre.file_sector-1,sector));
				bhreq[0] = getblk(inode->i_dev,sector,SECTOR_SIZE);
				if (bhreq[0] == NULL)	break;
				pre.bhlist[pre.nolist] = bhreq[0];
				if (!bhreq[0]->b_uptodate)
					ll_rw_block (READ,1,bhreq);
				pre.fetched_max += SECTOR_SIZE;
			}else{
				/* Stop prefetching further, we have reached eof */
				pre.fetched_max = 2000000000l;
			}
		} 
		pre.nolist++;
		if (pre.nolist >= pre.nblist) pre.nolist = 0;
		wait_on_buffer(bh);
		if (!bh->b_uptodate){
			/* read error  ? */
			brelse (bh);
			break;
		}
		offset = filp->f_pos & (SECTOR_SIZE-1);
		filp->f_pos += (size = MIN(SECTOR_SIZE-offset,left));
		data = bh->b_data;
		if (MSDOS_I(inode)->i_binary) {
			memcpy_tofs(buf,data+offset,size);
			buf += size;
		}
		else for (cnt = size; cnt; cnt--) {
				char ch;
				if ((ch = *((char *) data+offset++)) == '\r')
					size--;
				else {
					if (ch != 26) put_fs_byte(ch,buf++);
					else {
						filp->f_pos = inode->i_size;
						brelse(bh);
						break;
					}
				}
			}
		brelse(bh);
	}
	PRINTK (("--- %d -> %d\n",count,(int)(buf-start)));
	for (i=0; i<pre.nblist; i++) brelse (pre.bhlist[i]);
	if (start == buf) return -EIO;
	if (!IS_RDONLY(inode))
		inode->i_atime = CURRENT_TIME;
	PRINTK (("file_read ret %d\n",(buf-start)));
	filp->f_reada = 1;	/* Will be reset if a lseek is done */
	return buf-start;
}

/*
	Write to a file either from user space
*/
int msdos_file_write(
	struct inode *inode,
	struct file *filp,
	char *buf,
	int count)
{
	int sector,offset,size,left,written;
	int error,carry;
	char *start,*to,ch;
	struct buffer_head *bh;
	int binary_mode = MSDOS_I(inode)->i_binary;

	if (!inode) {
		printk("msdos_file_write: inode = NULL\n");
		return -EINVAL;
	}
	/* S_ISLNK allows for UMSDOS. Should never happen for normal MSDOS */
	if (!S_ISREG(inode->i_mode) && !S_ISLNK(inode->i_mode)) {
		printk("msdos_file_write: mode = %07o\n",inode->i_mode);
		return -EINVAL;
	}
/*
 * ok, append may not work when many processes are writing at the same time
 * but so what. That way leads to madness anyway.
 */
	if (filp->f_flags & O_APPEND) filp->f_pos = inode->i_size;
	if (count <= 0) return 0;
	error = carry = 0;
	for (start = buf; count || carry; count -= size) {
		while (!(sector = msdos_smap(inode,filp->f_pos >> SECTOR_BITS)))
			if ((error = msdos_add_cluster(inode)) < 0) break;
		if (error) {
			msdos_truncate(inode);
			break;
		}
		offset = filp->f_pos & (SECTOR_SIZE-1);
		size = MIN(SECTOR_SIZE-offset,MAX(carry,count));
		if (binary_mode
			&& offset == 0
			&& (size == SECTOR_SIZE
				|| filp->f_pos + size >= inode->i_size)){
			/* No need to read the block first since we will */
			/* completely overwrite it */
			/* or at least write past the end of file */
			if (!(bh = getblk(inode->i_dev,sector,SECTOR_SIZE))){
				error = -EIO;
				break;
			}
		}else if (!(bh = msdos_sread(inode->i_dev,sector))) {
			error = -EIO;
			break;
		}
		if (binary_mode) {
			memcpy_fromfs(bh->b_data+offset,buf,written = size);
			buf += size;
		}
		else {
			written = left = SECTOR_SIZE-offset;
			to = (char *) bh->b_data+(filp->f_pos & (SECTOR_SIZE-1));
			if (carry) {
				*to++ = '\n';
				left--;
				carry = 0;
			}
			for (size = 0; size < count && left; size++) {
				if ((ch = get_fs_byte(buf++)) == '\n') {
					*to++ = '\r';
					left--;
				}
				if (!left) carry = 1;
				else {
					*to++ = ch;
					left--;
				}
			}
			written -= left;
		}
		filp->f_pos += written;
		if (filp->f_pos > inode->i_size) {
			inode->i_size = filp->f_pos;
			inode->i_dirt = 1;
		}
		bh->b_uptodate = 1;
		mark_buffer_dirty(bh, 0);
		brelse(bh);
	}
	if (start == buf)
		return error;
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	MSDOS_I(inode)->i_attrs |= ATTR_ARCH;
	inode->i_dirt = 1;
	return buf-start;
}

void msdos_truncate(struct inode *inode)
{
	int cluster;

	cluster = SECTOR_SIZE*MSDOS_SB(inode->i_sb)->cluster_size;
	(void) fat_free(inode,(inode->i_size+(cluster-1))/cluster);
	MSDOS_I(inode)->i_attrs |= ATTR_ARCH;
	inode->i_dirt = 1;
}

/*
 *  linux/fs/fat/file.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  regular file handling primitives for fat-based filesystems
 */

#include <linux/sched.h>
#include <linux/locks.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/pagemap.h>

#include <asm/segment.h>
#include <asm/system.h>

#include "msbuffer.h"

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

#define PRINTK(x)
#define Printk(x) printk x

static struct file_operations fat_file_operations = {
	NULL,			/* lseek - default */
	fat_file_read,		/* read */
	fat_file_write,		/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	generic_file_mmap,	/* mmap */
	NULL,			/* no special open is needed */
	NULL,			/* release */
	file_fsync		/* fsync */
};

struct inode_operations fat_file_inode_operations = {
	&fat_file_operations,	/* default file operations */
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
	fat_bmap,		/* bmap */
	fat_truncate,		/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};
/* #Specification: msdos / special devices / mmap	
	Mmapping does work because a special mmap is provide in that case.
	Note that it is much less efficient than the generic_file_mmap normally
	used since it allocate extra buffer. generic_file_mmap is used for
	normal device (512 bytes hardware sectors).
*/
static struct file_operations fat_file_operations_1024 = {
	NULL,			/* lseek - default */
	fat_file_read,		/* read */
	fat_file_write,		/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	fat_mmap,		/* mmap */
	NULL,			/* no special open is needed */
	NULL,			/* release */
	file_fsync		/* fsync */
};

/* #Specification: msdos / special devices / swap file
	Swap file can't work on special devices with a large sector
	size (1024 bytes hard sector). Those devices have a weird
	MsDOS filesystem layout. Generally a single hardware sector
	may contain 2 unrelated logical sector. This mean that there is
	no easy way to do a mapping between disk sector of a file and virtual
	memory. So swap file is difficult (not available right now)
	on those devices. Off course, Ext2 does not have this problem.
*/
struct inode_operations fat_file_inode_operations_1024 = {
	&fat_file_operations_1024,	/* default file operations */
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
	fat_truncate,		/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

#define MSDOS_PREFETCH	32
struct fat_pre {
	int file_sector;/* Next sector to read in the prefetch table */
			/* This is relative to the file, not the disk */
	struct buffer_head *bhlist[MSDOS_PREFETCH];	/* All buffers needed */
	int nblist;	/* Number of buffers in bhlist */
	int nolist;	/* index in bhlist */
};
/*
	Order the prefetch of more sectors.
*/
static void fat_prefetch (
	struct inode *inode,
	struct fat_pre *pre,
	int nb)		/* How many must we prefetch at once */
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bhreq[MSDOS_PREFETCH];	/* Buffers not */
							/* already read */
	int nbreq = 0;			/* Number of buffers in bhreq */
	int i;
	for (i=0; i<nb; i++){
		int sector = fat_smap(inode,pre->file_sector);
		if (sector != 0){
			struct buffer_head *bh;
			PRINTK (("fsector2 %d -> %d\n",pre->file_sector-1,sector));
			pre->file_sector++;
			bh = fat_getblk(sb, sector);
			if (bh == NULL)	break;
			pre->bhlist[pre->nblist++] = bh;
			if (!fat_is_uptodate(sb,bh))
				bhreq[nbreq++] = bh;
		}else{
			break;
		}
	}
	if (nbreq > 0) fat_ll_rw_block (sb,READ,nbreq,bhreq);
	for (i=pre->nblist; i<MSDOS_PREFETCH; i++) pre->bhlist[i] = NULL;
}

/*
	Read a file into user space
*/
int fat_file_read(
	struct inode *inode,
	struct file *filp,
	char *buf,
	int count)
{
	struct super_block *sb = inode->i_sb;
	char *start = buf;
	char *end   = buf + count;
	int i;
	int left_in_file;
	struct fat_pre pre;
		

	if (!inode) {
		printk("fat_file_read: inode = NULL\n");
		return -EINVAL;
	}
	/* S_ISLNK allows for UMSDOS. Should never happen for normal MSDOS */
	if (!S_ISREG(inode->i_mode) && !S_ISLNK(inode->i_mode)) {
		printk("fat_file_read: mode = %07o\n",inode->i_mode);
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
		/*
			We must prefetch complete block, so we must
			take in account the offset in the first block.
		*/
		int count_max = (filp->f_pos & (SECTOR_SIZE-1)) + count;
		int   to_reada;	/* How many block to read all at once */
		pre.file_sector = filp->f_pos >> SECTOR_BITS;
		to_reada = count_max / SECTOR_SIZE;
		if (count_max & (SECTOR_SIZE-1)) to_reada++;
		if (filp->f_reada || !MSDOS_I(inode)->i_binary){
			/* Doing a read ahead on ascii file make sure we always */
			/* pre read enough, since we don't know how many blocks */
			/* we really need */
			int ahead = read_ahead[MAJOR(inode->i_dev)];
			PRINTK (("to_reada %d ahead %d\n",to_reada,ahead));
			if (ahead == 0) ahead = 8;
			to_reada += ahead;
		}
		if (to_reada > MSDOS_PREFETCH) to_reada = MSDOS_PREFETCH;
		pre.nblist = 0;
		fat_prefetch (inode,&pre,to_reada);
	}
	pre.nolist = 0;
	PRINTK (("count %d ahead %d nblist %d\n",count,read_ahead[MAJOR(inode->i_dev)],pre.nblist));
	while ((left_in_file = inode->i_size - filp->f_pos) > 0
		&& buf < end){
		struct buffer_head *bh = pre.bhlist[pre.nolist];
		char *data;
		int size,offset;
		if (bh == NULL) break;
		pre.bhlist[pre.nolist] = NULL;
		pre.nolist++;
		if (pre.nolist == MSDOS_PREFETCH/2){
			memcpy (pre.bhlist,pre.bhlist+MSDOS_PREFETCH/2
				,(MSDOS_PREFETCH/2)*sizeof(pre.bhlist[0]));
			pre.nblist -= MSDOS_PREFETCH/2;
			fat_prefetch (inode,&pre,MSDOS_PREFETCH/2);
			pre.nolist = 0;
		}
		PRINTK (("file_read pos %ld nblist %d %d %d\n",filp->f_pos,pre.nblist,pre.fetched,count));
		wait_on_buffer(bh);
		if (!fat_is_uptodate(sb,bh)){
			/* read error  ? */
			fat_brelse (sb, bh);
			break;
		}
		offset = filp->f_pos & (SECTOR_SIZE-1);
		data = bh->b_data + offset;
		size = MIN(SECTOR_SIZE-offset,left_in_file);
		if (MSDOS_I(inode)->i_binary) {
			size = MIN(size,end-buf);
			memcpy_tofs(buf,data,size);
			buf += size;
			filp->f_pos += size;
		}else{
			for (; size && buf < end; size--) {
				char ch = *data++;
				filp->f_pos++;
				if (ch == 26){
					filp->f_pos = inode->i_size;
					break;
				}else if (ch != '\r'){
					put_user(ch,buf++);
				}
			}
		}
		fat_brelse(sb, bh);
	}
	PRINTK (("--- %d -> %d\n",count,(int)(buf-start)));
	for (i=0; i<pre.nblist; i++)
		fat_brelse (sb, pre.bhlist[i]);
	if (start == buf)
		return -EIO;
	if (!IS_RDONLY(inode))
		inode->i_atime = CURRENT_TIME;
	filp->f_reada = 1;	/* Will be reset if a lseek is done */
	return buf-start;
}

/*
	Write to a file either from user space
*/
int fat_file_write(
	struct inode *inode,
	struct file *filp,
	const char *buf,
	int count)
{
	struct super_block *sb = inode->i_sb;
	int sector,offset,size,left,written;
	int error,carry;
	const char *start;
	char *to,ch;
	struct buffer_head *bh;
	int binary_mode = MSDOS_I(inode)->i_binary;

	if (!inode) {
		printk("fat_file_write: inode = NULL\n");
		return -EINVAL;
	}
	/* S_ISLNK allows for UMSDOS. Should never happen for normal MSDOS */
	if (!S_ISREG(inode->i_mode) && !S_ISLNK(inode->i_mode)) {
		printk("fat_file_write: mode = %07o\n",inode->i_mode);
		return -EINVAL;
	}
	/* system files may be immutable */
	if (IS_IMMUTABLE(inode))
		return -EPERM;
/*
 * ok, append may not work when many processes are writing at the same time
 * but so what. That way leads to madness anyway.
 */
	if (filp->f_flags & O_APPEND)
		filp->f_pos = inode->i_size;
	if (count <= 0)
		return 0;
	error = carry = 0;
	for (start = buf; count || carry; count -= size) {
		while (!(sector = fat_smap(inode,filp->f_pos >> SECTOR_BITS)))
			if ((error = fat_add_cluster(inode)) < 0) break;
		if (error) {
			fat_truncate(inode);
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
			if (!(bh = fat_getblk(sb,sector))){
				error = -EIO;
				break;
			}
		} else if (!(bh = fat_bread(sb,sector))) {
			error = -EIO;
			break;
		}
		if (binary_mode) {
			memcpy_fromfs(bh->b_data+offset,buf,written = size);
			buf += size;
		} else {
			written = left = SECTOR_SIZE-offset;
			to = (char *) bh->b_data+(filp->f_pos & (SECTOR_SIZE-1));
			if (carry) {
				*to++ = '\n';
				left--;
				carry = 0;
			}
			for (size = 0; size < count && left; size++) {
				if ((ch = get_user(buf++)) == '\n') {
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
		update_vm_cache(inode, filp->f_pos, bh->b_data + (filp->f_pos & (SECTOR_SIZE-1)), written);
		filp->f_pos += written;
		if (filp->f_pos > inode->i_size) {
			inode->i_size = filp->f_pos;
			inode->i_dirt = 1;
		}
		fat_set_uptodate(sb, bh, 1);
		fat_mark_buffer_dirty(sb, bh, 0);
		fat_brelse(sb, bh);
	}
	if (start == buf)
		return error;
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	MSDOS_I(inode)->i_attrs |= ATTR_ARCH;
	inode->i_dirt = 1;
	return buf-start;
}

void fat_truncate(struct inode *inode)
{
	int cluster;

	/* Why no return value?  Surely the disk could fail... */
	if (IS_IMMUTABLE(inode))
		return /* -EPERM */;
	cluster = SECTOR_SIZE*MSDOS_SB(inode->i_sb)->cluster_size;
	(void) fat_free(inode,(inode->i_size+(cluster-1))/cluster);
	MSDOS_I(inode)->i_attrs |= ATTR_ARCH;
	inode->i_dirt = 1;
}

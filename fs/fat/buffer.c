/*
 * linux/fs/fat/buffer.c
 *
 *
 */

#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/fat_cvf.h>

#if 0
#  define PRINTK(x) printk x
#else
#  define PRINTK(x)
#endif

struct buffer_head *fat_bread (
	struct super_block *sb,
	int block)
{
	struct buffer_head *ret = NULL;

	PRINTK(("fat_bread: block=0x%x\n", block));
	/*
	 * Note that the blocksize is 512, 1024 or 2048, but the first read
	 * is always of size 1024 (or 2048). Doing readahead may be
	 * counterproductive or just plain wrong.
	 */

	if(MSDOS_SB(sb)->cvf_format &&
	   MSDOS_SB(sb)->cvf_format->cvf_bread)
		return MSDOS_SB(sb)->cvf_format->cvf_bread(sb,block);

	if (sb->s_blocksize == 512) {
		ret = bread (sb->s_dev,block,512);
	} else {
		struct buffer_head *real;
		if (sb->s_blocksize == 1024){
			real = bread (sb->s_dev,block>>1,1024);
		} else {
			real = bread (sb->s_dev,block>>2,2048);
		}

		if (real != NULL){
			ret = (struct buffer_head *)
				kmalloc (sizeof(struct buffer_head), GFP_KERNEL);
			if (ret != NULL) {
				/* #Specification: msdos / strategy / special device / dummy blocks
				 * Many special device (Scsi optical disk for one) use
				 * larger hardware sector size. This allows for higher
				 * capacity.

				 * Most of the time, the MS-DOS filesystem that sits
				 * on this device is totally unaligned. It use logically
				 * 512 bytes sector size, with logical sector starting
				 * in the middle of a hardware block. The bad news is
				 * that a hardware sector may hold data own by two
				 * different files. This means that the hardware sector
				 * must be read, patch and written almost all the time.

				 * Needless to say that it kills write performance
				 * on all OS.

				 * Internally the linux msdos fs is using 512 bytes
				 * logical sector. When accessing such a device, we
				 * allocate dummy buffer cache blocks, that we stuff
				 * with the information of a real one (1k large).

				 * This strategy is used to hide this difference to
				 * the core of the msdos fs. The slowdown is not
				 * hidden though!
				 */
				/*
				 * The memset is there only to catch errors. The msdos
				 * fs is only using b_data
				 */
				memset (ret,0,sizeof(*ret));
				ret->b_data = real->b_data;
				if (sb->s_blocksize == 2048) {
					if (block & 3) ret->b_data += (block & 3) << 9;
				}else{
					if (block & 1) ret->b_data += 512;
				}
				ret->b_next = real;
			}else{
				brelse (real);
			}
		}
	}
	return ret;
}

struct buffer_head *fat_getblk(struct super_block *sb, int block)
{
	struct buffer_head *ret = NULL;
	PRINTK(("fat_getblk: block=0x%x\n", block));

	if (MSDOS_SB(sb)->cvf_format &&
	    MSDOS_SB(sb)->cvf_format->cvf_getblk)
		return MSDOS_SB(sb)->cvf_format->cvf_getblk(sb,block);

	if (sb->s_blocksize == 512){
		ret = getblk (sb->s_dev,block,512);
	} else {
		/*
		 * #Specification: msdos / special device / writing
		 * A write is always preceded by a read of the complete block
		 * (large hardware sector size). This defeat write performance.
		 * There is a possibility to optimize this when writing large
		 * chunk by making sure we are filling large block. Volunteer ?
		 */
		ret = fat_bread (sb,block);
	}
	return ret;
}

void fat_brelse (
	struct super_block *sb,
	struct buffer_head *bh)
{
	if (bh != NULL) {
		if (MSDOS_SB(sb)->cvf_format &&
		    MSDOS_SB(sb)->cvf_format->cvf_brelse)
			return MSDOS_SB(sb)->cvf_format->cvf_brelse(sb,bh);

		if (sb->s_blocksize == 512){
			brelse (bh);
		}else{
			brelse (bh->b_next);
			/* We can free the dummy because a new one is allocated at
				each fat_getblk() and fat_bread().
			*/
			kfree (bh);
		}
	}
}
	
void fat_mark_buffer_dirty (
	struct super_block *sb,
	struct buffer_head *bh,
	int dirty)
{
	if (MSDOS_SB(sb)->cvf_format &&
	    MSDOS_SB(sb)->cvf_format->cvf_mark_buffer_dirty) {
		MSDOS_SB(sb)->cvf_format->cvf_mark_buffer_dirty(sb,bh,dirty);
		return;
	}
 
	if (sb->s_blocksize != 512){
		bh = bh->b_next;
	}
	mark_buffer_dirty (bh,dirty);
}

void fat_set_uptodate (
	struct super_block *sb,
	struct buffer_head *bh,
	int val)
{
	if (MSDOS_SB(sb)->cvf_format && 
	    MSDOS_SB(sb)->cvf_format->cvf_set_uptodate) {
		MSDOS_SB(sb)->cvf_format->cvf_set_uptodate(sb,bh,val);
		return;
	}
 
	if (sb->s_blocksize != 512){
		bh = bh->b_next;
	}
	mark_buffer_uptodate(bh, val);
}
int fat_is_uptodate (
	struct super_block *sb,
	struct buffer_head *bh)
{
	if(MSDOS_SB(sb)->cvf_format &&
	   MSDOS_SB(sb)->cvf_format->cvf_is_uptodate)
		return MSDOS_SB(sb)->cvf_format->cvf_is_uptodate(sb,bh);
 
	if (sb->s_blocksize != 512){
		bh = bh->b_next;
	}
	return buffer_uptodate(bh);
}

void fat_ll_rw_block (
	struct super_block *sb,
	int opr,
	int nbreq,
	struct buffer_head *bh[32])
{
        if (MSDOS_SB(sb)->cvf_format &&
	    MSDOS_SB(sb)->cvf_format->cvf_ll_rw_block) {
		MSDOS_SB(sb)->cvf_format->cvf_ll_rw_block(sb,opr,nbreq,bh);
		return;
	}
 
	if (sb->s_blocksize == 512){
		ll_rw_block(opr,nbreq,bh);
	}else{
		struct buffer_head *tmp[32];
		int i;
		for (i=0; i<nbreq; i++){
			tmp[i] = bh[i]->b_next;
		}
		ll_rw_block(opr,nbreq,tmp);
	}
}


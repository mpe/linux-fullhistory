/*
 *  linux/fs/hpfs/file.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  file VFS functions
 */

#include <linux/string.h>
#include "hpfs_fn.h"

int hpfs_open(struct inode *i, struct file *f)
{
	hpfs_lock_inode(i);
	hpfs_unlock_inode(i); /* make sure nobody is deleting the file */
	if (!i->i_nlink) return -ENOENT;
	return 0;
}

int hpfs_file_release(struct inode *inode, struct file *file)
{
	hpfs_write_if_changed(inode);
	return 0;
}

int hpfs_file_fsync(struct file *file, struct dentry *dentry)
{
	/*return file_fsync(file, dentry);*/
	return 0; /* Don't fsync :-) */
}

/*
 * generic_file_read often calls bmap with non-existing sector,
 * so we must ignore such errors.
 */

secno hpfs_bmap(struct inode *inode, unsigned file_secno)
{
	unsigned n, disk_secno;
	struct fnode *fnode;
	struct buffer_head *bh;
	if (((inode->i_size + 511) >> 9) <= file_secno) return 0;
	n = file_secno - inode->i_hpfs_file_sec;
	if (n < inode->i_hpfs_n_secs) return inode->i_hpfs_disk_sec + n;
	if (!(fnode = hpfs_map_fnode(inode->i_sb, inode->i_ino, &bh))) return 0;
	disk_secno = hpfs_bplus_lookup(inode->i_sb, inode, &fnode->btree, file_secno, bh);
	if (disk_secno == -1) return 0;
	if (hpfs_chk_sectors(inode->i_sb, disk_secno, 1, "bmap")) return 0;
	return disk_secno;
}

void hpfs_truncate(struct inode *i)
{
	if (IS_IMMUTABLE(i)) return /*-EPERM*/;
	i->i_hpfs_n_secs = 0;
	i->i_blocks = 1 + ((i->i_size + 511) >> 9);
	hpfs_truncate_btree(i->i_sb, i->i_ino, 1, ((i->i_size + 511) >> 9));
	hpfs_write_inode(i);
}

int hpfs_getblk_block(struct inode *inode, long block, int create, int *err, int *created)
{
	int add;
	int sec = 0;
	down(&inode->i_sem);
	if (err) *err = 0;
	if (created) *created = 0;
	if (!inode->i_blocks) {
		hpfs_error(inode->i_sb, "hpfs_get_block: inode %08x has no blocks", inode->i_ino);
		if (err) *err = -EFSERROR;
		up(&inode->i_sem);
		return 0;
	}
	if (block < ((add = inode->i_blocks - 1))) {
		int bm;
		if (!(bm = hpfs_bmap(inode, block))) {
			hpfs_error(inode->i_sb, "hpfs_get_block: cound not bmap block %08x, inode %08x, size %08x", (int)block, inode->i_ino, (int)inode->i_size);
			*err = -EFSERROR;
		}
		up(&inode->i_sem);
		return bm;
	}
	if (!create) {
		if (err) *err = -EFBIG;
		up(&inode->i_sem);
		return 0;
	}
	if (created) *created = 1;
	while (add <= block) {
		if ((sec = hpfs_add_sector_to_btree(inode->i_sb, inode->i_ino, 1, add)) == -1) {
			if (err) *err = -ENOSPC;
			hpfs_truncate_btree(inode->i_sb, inode->i_ino, 1, inode->i_blocks - 1);
			return 0;
		} /* FIXME: clear block */
		add++;
	}
	inode->i_blocks = add + 1;
	up(&inode->i_sem);
	return sec;
}

/* copied from ext2fs */
static int hpfs_get_block(struct inode *inode, unsigned long block, struct buffer_head *bh, int update)
{
	if (!bh->b_blocknr) {
		int error, created;
		unsigned long blocknr;

		blocknr = hpfs_getblk_block(inode, block, 1, &error, &created);
		if (!blocknr) {
			if (!error)
				error = -ENOSPC;
			return error;
		}

		bh->b_dev = inode->i_dev;
		bh->b_blocknr = blocknr;

		if (!update)
			return 0;

		if (created) {
			memset(bh->b_data, 0, bh->b_size);
			set_bit(BH_Uptodate, &bh->b_state);
			return 0;
		}
	}

	if (!update)
		return 0;

	lock_kernel();
	ll_rw_block(READ, 1, &bh);
	wait_on_buffer(bh);
	unlock_kernel();

	return buffer_uptodate(bh) ? 0 : -EIO;
}

int hpfs_writepage(struct file *file, struct page *page)
{
	return block_write_full_page(file, page, hpfs_get_block);
}

long hpfs_write_one_page (struct file *file, struct page *page, unsigned long offset, unsigned long bytes, const char *buf)
{
        return block_write_partial_page(file, page, offset, bytes, buf, hpfs_get_block);
}


ssize_t hpfs_file_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	ssize_t retval;
	struct inode *inode = file->f_dentry->d_inode;
	retval = generic_file_write(file, buf, count, ppos, hpfs_write_one_page);
	if (retval > 0) {
		/*remove_suid(inode);*/
		inode->i_mtime = CURRENT_TIME;
		inode->i_hpfs_dirty = 1;
	}
	return retval;
}


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

int hpfs_get_block(struct inode *inode, long iblock, struct buffer_head *bh_result, int create)
{
	secno s;
	if (iblock < inode->i_blocks - 1) {
		s = hpfs_bmap(inode, iblock);
		bh_result->b_dev = inode->i_dev;
		bh_result->b_blocknr = s;
		bh_result->b_state |= (1UL << BH_Mapped);
		return 0;
	}
	if (!create) return 0;
	if (iblock > inode->i_blocks - 1) {
		//hpfs_error(inode->i_sb, "hpfs_get_block beyond file end (requested %08x, inode size %08x", (int)iblock, (int)inode->i_blocks - 1);
		printk("HPFS: could not write beyond file end. This is known bug.\n");
		return -EFSERROR;
	}
	if ((s = hpfs_add_sector_to_btree(inode->i_sb, inode->i_ino, 1, inode->i_blocks - 1)) == -1) {
		hpfs_truncate_btree(inode->i_sb, inode->i_ino, 1, inode->i_blocks - 1);
		return -ENOSPC;
	}
	inode->i_blocks++;
	bh_result->b_dev = inode->i_dev;
	bh_result->b_blocknr = s;
	bh_result->b_state |= (1UL << BH_Mapped) | (1UL << BH_New);
	return 0;
}

static int hpfs_write_partial_page(struct file *file, struct page *page, unsigned long offset, unsigned long bytes, const char * buf)
{
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct page *new_page, **hash;
	unsigned long pgpos;
	struct page * page_cache = NULL;
	long status;

	printk("- off: %08x\n", (int)page->offset);
	pgpos = (inode->i_blocks - 1) * 512 & PAGE_CACHE_MASK;
	while (pgpos < page->offset) {
long pgp = pgpos;
		printk("pgpos: %08x, bl: %d\n", (int)pgpos, (int)inode->i_blocks);
		hash = page_hash(&inode->i_data, pgpos);
repeat_find:	new_page = __find_lock_page(&inode->i_data, pgpos, hash);
		if (!new_page) {
			if (!page_cache) {
				page_cache = page_cache_alloc();
				if (page_cache)
					goto repeat_find;
				status = -ENOMEM;
				goto out;
			}
			new_page = page_cache;
			if (add_to_page_cache_unique(new_page,&inode->i_data,pgpos,hash))
				goto repeat_find;
			page_cache = NULL;
		}
		printk("A\n");
		status = block_write_cont_page(file, new_page, PAGE_SIZE, 0, NULL);
		printk("B\n");
		UnlockPage(new_page);
		page_cache_release(new_page);
		if (status < 0)
			goto out;
		pgpos = (inode->i_blocks - 1) * 512 & PAGE_CACHE_MASK;
		printk("pgpos2: %08x, bl: %d\n", (int)pgpos, (int)inode->i_blocks);
		if (pgpos == pgp) {
			status = -1;
			printk("ERROR\n");
			goto out;
		}
	}
	//if ((status = block_write_cont_page(file, page, PAGE_SIZE, 0, NULL)) < 0) goto out;
	printk("C\n");
	status = block_write_cont_page(file, page, offset, bytes, buf);
	printk("D\n");
out:
	printk("O\n");
	if (page_cache)
		page_cache_free(page_cache);
	printk("E\n");
	return status;
}


ssize_t hpfs_file_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	ssize_t retval;

	retval = generic_file_write(file, buf, count,
				    ppos, /*hpfs_write_partial_page*/block_write_partial_page);
	if (retval > 0) {
		struct inode *inode = file->f_dentry->d_inode;
		inode->i_mtime = CURRENT_TIME;
		inode->i_hpfs_dirty = 1;
	}
	return retval;
}


/*
 *  linux/fs/hpfs/file.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  file VFS functions
 */

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
	hpfs_truncate_btree(i->i_sb, i->i_ino, 1, ((i->i_size + 511) >> 9));
	i->i_blocks = 1 + ((i->i_size + 511) >> 9);
	/*mark_inode_dirty(i);*/i->i_hpfs_dirty = 1;
	hpfs_write_inode(i);
}

ssize_t hpfs_file_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	int i,j;
	int a = generic_file_read(filp, buf, count, ppos);
	if (inode->i_hpfs_conv != CONV_TEXT || a < 0) {
		return a;
	}	
	for (i = 0, j = 0; i < a; i++) {
		char c;
		int error;
		if ((error = get_user(c, buf + i))) return error;
		if (c != '\r') {
			if (i != j) put_user(c, buf + j);
			j++;
		}
	}
	return j;
}

ssize_t hpfs_file_write(struct file *filp, const char *buf, size_t count,
			loff_t *ppos)
{
	struct inode *i = filp->f_dentry->d_inode;
	int carry, error = 0;
	const char *start = buf;
	if (!i) return -EINVAL;
	if (!S_ISREG(i->i_mode)) return -EINVAL;
	if (IS_IMMUTABLE(i)) return -EPERM;
	if (filp->f_flags & O_APPEND) *ppos = i->i_size;
	if (count <= 0) return 0;
	if ((unsigned)(*ppos+count) >= 0x80000000U || (unsigned)count >= 0x80000000U) return -EFBIG;
	carry = 0;
	while (count || carry) {
		int ii, add = 0;
		secno sec = 0; /* Go away, uninitialized variable warning */
		int offset, size, written;
		char ch;
		struct buffer_head *bh;
		char *data;
		offset = *ppos & 0x1ff;
		size = count > 0x200 - offset ? 0x200 - offset : count;
		if ((*ppos >> 9) < ((i->i_size + 0x1ff) >> 9)) {
			i->i_hpfs_n_secs = 0;
			if (!(sec = hpfs_bmap(i, *ppos >> 9))) {
				hpfs_error(i->i_sb, "bmap failed, file %08x, fsec %08x",
					i->i_ino, *ppos >> 9);
				error =- EFSERROR;
				break;
			}
		} else for (ii = (i->i_size + 0x1ff) >> 9, add = 1; ii <= *ppos >> 9; ii++) {
			if ((sec = hpfs_add_sector_to_btree(i->i_sb, i->i_ino, 1, ii)) == -1) {
				hpfs_truncate(i);
				return -ENOSPC;
			}
			if (*ppos != i->i_size)
				if ((data = hpfs_get_sector(i->i_sb, sec, &bh))) {
					memset(data, 0, 512);
					mark_buffer_dirty(bh, 0);
					brelse(bh);
				}
			i->i_size = 0x200 * ii + 1;
			i->i_blocks++;
			/*mark_inode_dirty(i);*/i->i_hpfs_dirty = 1;
			if (i->i_sb->s_hpfs_chk >= 2) {
				secno bsec;
				bsec = hpfs_bmap(i, ii);
				if (sec != bsec) {
					hpfs_error(i->i_sb, "sec == %08x, bmap returns %08x", sec, bsec);
					error = -EFSERROR;
					break;
				}
			}	
			PRINTK(("file_write: added %08x\n", sec));
		}
		if (!sec || sec == 15) {
			hpfs_error(i->i_sb, "bmap returned empty sector");
			error = -EFSERROR;
			break;
		}
		if (i->i_sb->s_hpfs_chk)
			if (hpfs_chk_sectors(i->i_sb, sec, 1, "data")) {
				error = -EFSERROR;
				break;
			}
		if ((!offset && size == 0x200) || add)
			data = hpfs_get_sector(i->i_sb, sec, &bh);
		else data = hpfs_map_sector(i->i_sb, sec, &bh, 0);
		if (!data) {
			error = -EIO;
			break;
		}
		if (i->i_hpfs_conv != CONV_TEXT) {
			memcpy_fromfs(data + offset, buf, written = size);
			buf += size;
		} else {
			int left;
			char *to;
			/* LF->CR/LF conversion, stolen from fat fs */
			written = left = 0x200 - offset;
			to = (char *) bh->b_data + (*ppos & 0x1ff);
			if (carry) {
				*to++ = '\n';
				left--;
				carry = 0;
			}
			for (size = 0; size < count && left; size++) {
				if ((error = get_user(ch, buf++))) break;
				if (ch == '\n') {
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
		update_vm_cache(i, *ppos, bh->b_data + (*ppos & 0x1ff), written);
		*ppos += written;
		if (*ppos > i->i_size) {
			i->i_size = *ppos;
			/*mark_inode_dirty(i);*/i->i_hpfs_dirty = 1;
		}
		mark_buffer_dirty(bh, 0);
		brelse(bh);
		count -= size;
	}
	if (start == buf) return error;
	i->i_mtime = CURRENT_TIME;
	/*mark_inode_dirty(i);*/i->i_hpfs_dirty = 1;
	return buf - start;
}

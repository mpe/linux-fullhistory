/*
 * mm/fadvise.c
 *
 * Copyright (C) 2002, Linus Torvalds
 *
 * 11Jan2003	akpm@digeo.com
 *		Initial version.
 */

#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/backing-dev.h>
#include <linux/pagevec.h>
#include <linux/fadvise.h>

/*
 * POSIX_FADV_WILLNEED could set PG_Referenced, and POSIX_FADV_NOREUSE could
 * deactivate the pages and clear PG_Referenced.
 */
long sys_fadvise64(int fd, loff_t offset, size_t len, int advice)
{
	struct file *file = fget(fd);
	struct inode *inode;
	struct address_space *mapping;
	struct backing_dev_info *bdi;
	int ret = 0;

	if (!file)
		return -EBADF;

	inode = file->f_dentry->d_inode;
	mapping = inode->i_mapping;
	if (!mapping) {
		ret = -EINVAL;
		goto out;
	}

	bdi = mapping->backing_dev_info;

	switch (advice) {
	case POSIX_FADV_NORMAL:
		file->f_ra.ra_pages = bdi->ra_pages;
		break;
	case POSIX_FADV_RANDOM:
		file->f_ra.ra_pages = 0;
		break;
	case POSIX_FADV_SEQUENTIAL:
		file->f_ra.ra_pages = bdi->ra_pages * 2;
		break;
	case POSIX_FADV_WILLNEED:
	case POSIX_FADV_NOREUSE:
		if (!mapping->a_ops->readpage) {
			ret = -EINVAL;
			break;
		}
		ret = do_page_cache_readahead(mapping, file,
				offset >> PAGE_CACHE_SHIFT,
				max_sane_readahead(len >> PAGE_CACHE_SHIFT));
		if (ret > 0)
			ret = 0;
		break;
	case POSIX_FADV_DONTNEED:
		if (!bdi_write_congested(mapping->backing_dev_info))
			filemap_flush(mapping);
		invalidate_mapping_pages(mapping, offset >> PAGE_CACHE_SHIFT,
				(len >> PAGE_CACHE_SHIFT) + 1);
		break;
	default:
		ret = -EINVAL;
	}
out:
	fput(file);
	return ret;
}

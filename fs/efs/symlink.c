/*
 * symlink.c
 *
 * Copyright (c) 1999 Al Smith
 *
 * Portions derived from work (c) 1995,1996 Christian Vogelgsang.
 */

#include <linux/string.h>
#include <linux/efs_fs.h>
#include <linux/pagemap.h>

static int efs_symlink_readpage(struct dentry *dentry, struct page *page)
{
	char *link = (char*)kmap(page);
	struct buffer_head * bh;
	struct inode * inode = dentry->d_inode;
	efs_block_t size = inode->i_size;
	int err;
  
	err = -ENAMETOOLONG;
	if (size > 2 * EFS_BLOCKSIZE)
		goto fail;
  
	/* read first 512 bytes of link target */
	err = -EIO;
	bh = bread(inode->i_dev, efs_bmap(inode, 0), EFS_BLOCKSIZE);
	if (!bh)
		goto fail;
	memcpy(link, bh->b_data, (size > EFS_BLOCKSIZE) ? EFS_BLOCKSIZE : size);
	brelse(bh);
	if (size > EFS_BLOCKSIZE) {
		bh = bread(inode->i_dev, efs_bmap(inode, 1), EFS_BLOCKSIZE);
		if (!bh)
			goto fail;
		memcpy(link + EFS_BLOCKSIZE, bh->b_data, size - EFS_BLOCKSIZE);
		brelse(bh);
	}
	link[size] = '\0';
	SetPageUptodate(page);
	kunmap(page);
	UnlockPage(page);
	return 0;
fail:
	SetPageError(page);
	kunmap(page);
	UnlockPage(page);
	return err;
}

struct inode_operations efs_symlink_inode_operations = {
	readlink:	page_readlink,
	follow_link:	page_follow_link,
	readpage:	efs_symlink_readpage
};

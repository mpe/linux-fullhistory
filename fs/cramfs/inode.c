/*
 * Compressed rom filesystem for Linux.
 *
 * Copyright (C) 1999 Linus Torvalds.
 *
 * This file is released under the GPL.
 */

/*
 * These are the VFS interfaces to the compressed rom filesystem.
 * The actual compression is based on zlib, see the other files.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/locks.h>

#include <asm/uaccess.h>

#include "cramfs.h"

static struct super_operations cramfs_ops;
static struct inode_operations cramfs_file_inode_operations;
static struct inode_operations cramfs_dir_inode_operations;
static struct inode_operations cramfs_symlink_inode_operations;

/* These two macros may change in future, to provide better st_ino
   semantics. */
#define CRAMINO(x)	((x)->offset?(x)->offset<<2:1)
#define OFFSET(x)	((x)->i_ino)

static struct inode *get_cramfs_inode(struct super_block *sb, struct cramfs_inode * cramfs_inode)
{
	struct inode * inode = get_empty_inode();

	if (inode) {
		inode->i_mode = cramfs_inode->mode;
		inode->i_uid = cramfs_inode->uid;
		inode->i_size = cramfs_inode->size;
		inode->i_gid = cramfs_inode->gid;
		inode->i_ino = CRAMINO(cramfs_inode);
		inode->i_sb = sb;
		inode->i_dev = sb->s_dev;
		inode->i_nlink = 1; /* arguably wrong for directories,
				       but it's the best we can do
				       without reading the directory
				       contents.  1 yields the right
				       result in GNU find, even
				       without -noleaf option. */
		insert_inode_hash(inode);
		if (S_ISREG(inode->i_mode))
			inode->i_op = &cramfs_file_inode_operations;
		else if (S_ISDIR(inode->i_mode))
			inode->i_op = &cramfs_dir_inode_operations;
		else if (S_ISLNK(inode->i_mode))
			inode->i_op = &cramfs_symlink_inode_operations;
		else {
			inode->i_size = 0;
			init_special_inode(inode, inode->i_mode, cramfs_inode->size);
		}
	}
	return inode;
}

/*
 * We have our own block cache: don't fill up the buffer cache
 * with the rom-image, because the way the filesystem is set
 * up the accesses should be fairly regular and cached in the
 * page cache and dentry tree anyway..
 *
 * This also acts as a way to guarantee contiguous areas of up to
 * BLKS_PER_BUF*PAGE_CACHE_SIZE, so that the caller doesn't need to
 * worry about end-of-buffer issues even when decompressing a full
 * page cache.
 */
#define READ_BUFFERS (2)
/* NEXT_BUFFER(): Loop over [0..(READ_BUFFERS-1)]. */
#define NEXT_BUFFER(_ix) ((_ix) ^ 1)

/*
 * BLKS_PER_BUF_SHIFT must be at least 1 to allow for "compressed"
 * data that takes up more space than the original.  1 is guaranteed
 * to suffice, though.  Larger values provide more read-ahead and
 * proportionally less wastage at the end of the buffer.
 */
#define BLKS_PER_BUF_SHIFT (2)
#define BLKS_PER_BUF (1 << BLKS_PER_BUF_SHIFT)
static unsigned char read_buffers[READ_BUFFERS][BLKS_PER_BUF][PAGE_CACHE_SIZE];
static unsigned buffer_blocknr[READ_BUFFERS];
static int next_buffer = 0;

/*
 * Returns a pointer to a buffer containing at least LEN bytes of
 * filesystem starting at byte offset OFFSET into the filesystem.
 */
static void *cramfs_read(struct super_block *sb, unsigned int offset, unsigned int len)
{
	struct buffer_head * bh_array[BLKS_PER_BUF];
	unsigned i, blocknr, last_blocknr, buffer;

	if (!len)
		return NULL;
	blocknr = offset >> PAGE_CACHE_SHIFT;
	last_blocknr = (offset + len - 1) >> PAGE_CACHE_SHIFT;
	if (last_blocknr - blocknr >= BLKS_PER_BUF)
		goto eek; resume:
	offset &= PAGE_CACHE_SIZE - 1;
	for (i = 0; i < READ_BUFFERS; i++) {
		if ((blocknr >= buffer_blocknr[i]) &&
		    (last_blocknr - buffer_blocknr[i] < BLKS_PER_BUF))
			return &read_buffers[i][blocknr - buffer_blocknr[i]][offset];
	}

	/* Ok, read in BLKS_PER_BUF pages completely first. */
	for (i = 0; i < BLKS_PER_BUF; i++)
		bh_array[i] = bread(sb->s_dev, blocknr + i, PAGE_CACHE_SIZE);

	/* Ok, copy them to the staging area without sleeping. */
	buffer = next_buffer;
	next_buffer = NEXT_BUFFER(buffer);
	buffer_blocknr[buffer] = blocknr;
	for (i = 0; i < BLKS_PER_BUF; i++) {
		struct buffer_head * bh = bh_array[i];
		if (bh) {
			memcpy(read_buffers[buffer][i], bh->b_data, PAGE_CACHE_SIZE);
			bforget(bh);
		} else
			memset(read_buffers[buffer][i], 0, PAGE_CACHE_SIZE);
	}
	return read_buffers[buffer][0] + offset;

 eek:
	printk(KERN_ERR
	       "cramfs (device %s): requested chunk (%u:+%u) bigger than buffer\n",
	       bdevname(sb->s_dev), offset, len);
	/* TODO: return EIO to process or kill the current process
           instead of resuming. */
	*((int *)0) = 0; /* XXX: doesn't work on all archs */
	goto resume;
}
			

static struct super_block * cramfs_read_super(struct super_block *sb, void *data, int silent)
{
	int i;
	struct cramfs_super super;
	unsigned long root_offset;
	struct super_block * retval = NULL;

	lock_super(sb);
	set_blocksize(sb->s_dev, PAGE_CACHE_SIZE);
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;

	/* Invalidate the read buffers on mount: think disk change.. */
	for (i = 0; i < READ_BUFFERS; i++)
		buffer_blocknr[i] = -1;

	/* Read the first block and get the superblock from it */
	memcpy(&super, cramfs_read(sb, 0, sizeof(super)), sizeof(super));

	/* Do sanity checks on the superblock */
	if (super.magic != CRAMFS_MAGIC) {
		printk("wrong magic\n");
		goto out;
	}
	if (memcmp(super.signature, CRAMFS_SIGNATURE, sizeof(super.signature))) {
		printk("wrong signature\n");
		goto out;
	}
	if (super.flags & ~CRAMFS_SUPPORTED_FLAGS) {
		printk("unsupported filesystem features\n");
		goto out;
	}

	/* Check that the root inode is in a sane state */
	if (!S_ISDIR(super.root.mode)) {
		printk("root is not a directory\n");
		goto out;
	}
	root_offset = super.root.offset << 2;
	if (root_offset == 0)
		printk(KERN_INFO "cramfs: note: empty filesystem");
	else if (root_offset != sizeof(struct cramfs_super)) {
		printk("bad root offset %lu\n", root_offset);
		goto out;
	}

	/* Set it all up.. */
	sb->s_op	= &cramfs_ops;
	sb->s_root = d_alloc_root(get_cramfs_inode(sb, &super.root));
	retval = sb;

out:
	unlock_super(sb);
	return retval;
}

/* Nothing to do.. */
static void cramfs_put_super(struct super_block *sb)
{
	return;
}

static int cramfs_statfs(struct super_block *sb, struct statfs *buf, int bufsize)
{
	struct statfs tmp;

	/* Unsupported fields set to -1 as per man page. */
	memset(&tmp, 0xff, sizeof(tmp));

	tmp.f_type = CRAMFS_MAGIC;
	tmp.f_bsize = PAGE_CACHE_SIZE;
	tmp.f_bfree = 0;
	tmp.f_bavail = 0;
	tmp.f_ffree = 0;
	tmp.f_namelen = 255;
	return copy_to_user(buf, &tmp, bufsize) ? -EFAULT : 0;
}

/*
 * Read a cramfs directory entry.
 */
static int cramfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	unsigned int offset;
	int copied;

	/* Offset within the thing. */
	offset = filp->f_pos;
	if (offset >= inode->i_size)
		return 0;
	/* Directory entries are always 4-byte aligned */
	if (offset & 3)
		return -EINVAL;

	copied = 0;
	while (offset < inode->i_size) {
		struct cramfs_inode *de;
		unsigned long nextoffset;
		char *name;
		int namelen, error;

		de = cramfs_read(sb, OFFSET(inode) + offset, sizeof(*de)+256);
		name = (char *)(de+1);

		/*
		 * Namelengths on disk are shifted by two
		 * and the name padded out to 4-byte boundaries
		 * with zeroes.
		 */
		namelen = de->namelen << 2;
		nextoffset = offset + sizeof(*de) + namelen;
		for (;;) {
			if (!namelen)
				return -EIO;
			if (name[namelen-1])
				break;
			namelen--;
		}
		error = filldir(dirent, name, namelen, offset, CRAMINO(de));
		if (error)
			break;

		offset = nextoffset;
		filp->f_pos = offset;
		copied++;
	}
	return 0;
}

/*
 * Lookup and fill in the inode data..
 */
static struct dentry * cramfs_lookup(struct inode *dir, struct dentry *dentry)
{
	unsigned int offset = 0;

	while (offset < dir->i_size) {
		struct cramfs_inode *de;
		char *name;
		int namelen;

		de = cramfs_read(dir->i_sb, OFFSET(dir) + offset, sizeof(*de)+256);
		name = (char *)(de+1);
		namelen = de->namelen << 2;
		offset += sizeof(*de) + namelen;

		/* Quick check that the name is roughly the right length */
		if (((dentry->d_name.len + 3) & ~3) != namelen)
			continue;

		for (;;) {
			if (!namelen)
				return ERR_PTR(-EIO);
			if (name[namelen-1])
				break;
			namelen--;
		}
		if (namelen != dentry->d_name.len)
			continue;
		if (memcmp(dentry->d_name.name, name, namelen))
			continue;
		d_add(dentry, get_cramfs_inode(dir->i_sb, de));
		return NULL;
	}
	d_add(dentry, NULL);
	return NULL;
}

static int cramfs_readpage(struct dentry *dentry, struct page * page)
{
	struct inode *inode = dentry->d_inode;
	u32 maxblock, bytes_filled;

	maxblock = (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	bytes_filled = 0;
	if (page->index < maxblock) {
		struct super_block *sb = inode->i_sb;
		u32 blkptr_offset = OFFSET(inode) + page->index*4;
		u32 start_offset, compr_len;

		start_offset = OFFSET(inode) + maxblock*4;
		if (page->index)
			start_offset = *(u32 *) cramfs_read(sb, blkptr_offset-4, 4);
		compr_len = (*(u32 *) cramfs_read(sb, blkptr_offset, 4)
			     - start_offset);
		if (compr_len == 0)
			; /* hole */
		else
			bytes_filled = cramfs_uncompress_block((void *) page_address(page),
				 PAGE_CACHE_SIZE,
				 cramfs_read(sb, start_offset, compr_len),
				 compr_len);
	}
	memset((void *) (page_address(page) + bytes_filled), 0, PAGE_CACHE_SIZE - bytes_filled);
	SetPageUptodate(page);
	UnlockPage(page);
	return 0;
}

static struct page *get_symlink_page(struct dentry *dentry)
{
	return read_cache_page(&dentry->d_inode->i_data, 0, (filler_t *)cramfs_readpage, dentry);
}

static int cramfs_readlink(struct dentry *dentry, char *buffer, int len)
{
	struct inode *inode = dentry->d_inode;
	int retval;

	if (!inode || !S_ISLNK(inode->i_mode))
		return -EBADF;

	retval = inode->i_size;
	if (retval) {
		int len;
		struct page *page = get_symlink_page(dentry);

		if (IS_ERR(page))
			return PTR_ERR(page);
		wait_on_page(page);
		len = retval;
		retval = -EIO;
		if (Page_Uptodate(page)) {
			retval = -EFAULT;
			if (!copy_to_user(buffer, (void *) page_address(page), len))
				retval = len;
		}
		page_cache_release(page);
	}
	return retval;
}

static struct dentry *cramfs_follow_link(struct dentry *dentry, struct dentry *base, unsigned int follow)
{
	struct page *page = get_symlink_page(dentry);
	struct dentry *result;

	if (IS_ERR(page)) {
		dput(base);
		return ERR_PTR(PTR_ERR(page));
	}

	result = lookup_dentry((void *) page_address(page), base, follow);
	page_cache_release(page);
	return result;
}

/*
 * Our operations:
 *
 * A regular file can be read and mmap'ed.
 */
static struct file_operations cramfs_file_operations = {
	NULL,			/* lseek - default */
	generic_file_read,	/* read */
	NULL,			/* write - bad */
	NULL,			/* readdir */
	NULL,			/* poll - default */
	NULL,			/* ioctl */
	generic_file_mmap,	/* mmap */
	NULL,			/* open */
	NULL,			/* flush */
	NULL,			/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
};

/*
 * A directory can only readdir
 */
static struct file_operations cramfs_directory_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read */
	NULL,			/* write - bad */
	cramfs_readdir,		/* readdir */
	NULL,			/* poll - default */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	NULL,			/* open */
	NULL,			/* flush */
	NULL,			/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
};

static struct inode_operations cramfs_file_inode_operations = {
	&cramfs_file_operations,
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
	NULL,			/* get_block */
	cramfs_readpage,	/* readpage */
	NULL,			/* writepage */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* revalidate */
};

static struct inode_operations cramfs_dir_inode_operations = {
	&cramfs_directory_operations,
	NULL,			/* create */
	cramfs_lookup,		/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* get_block */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* revalidate */
};

static struct inode_operations cramfs_symlink_inode_operations = {
	NULL,			/* symlinks do not have files */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	cramfs_readlink,		/* readlink */
	cramfs_follow_link,	/* follow_link */
	NULL,			/* get_block */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* revalidate */
};

static struct super_operations cramfs_ops = {
	NULL,			/* read inode */
	NULL,			/* write inode */
	NULL,			/* put inode */
	NULL,			/* delete inode */
	NULL,			/* notify change */
	cramfs_put_super,		/* put super */
	NULL,			/* write super */
	cramfs_statfs,		/* statfs */
	NULL			/* remount */
};

static struct file_system_type cramfs_fs_type = {
	"cramfs",
	FS_REQUIRES_DEV,
	cramfs_read_super,
	NULL
};

static int __init init_cramfs_fs(void)
{
	cramfs_uncompress_init();
	return register_filesystem(&cramfs_fs_type);
}

static void __exit exit_cramfs_fs(void)
{
	cramfs_uncompress_exit();
	unregister_filesystem(&cramfs_fs_type);
}

module_init(init_cramfs_fs)
module_exit(exit_cramfs_fs)

/*
 * ROMFS file system, Linux implementation
 *
 * Copyright (C) 1997  Janos Farkas <chexum@shadow.banki.hu>
 *
 * Using parts of the minix filesystem
 * Copyright (C) 1991, 1992  Linus Torvalds
 *
 * and parts of the affs filesystem additionally
 * Copyright (C) 1993  Ray Burr
 * Copyright (C) 1996  Hans-Joachim Widmaier
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Changes
 *					Changed for 2.1.19 modules
 *	Jan 1997			Initial release
 *	Jun 1997			2.1.43+ changes
 *					Proper page locking in readpage
 *					Changed to work with 2.1.45+ fs
 *	Jul 1997			Fixed follow_link
 *			2.1.47
 *					lookup shouldn't return -ENOENT
 *					from Horst von Brand:
 *					  fail on wrong checksum
 *					  double unlock_super was possible
 *					  correct namelen for statfs
 *					spotted by Bill Hawes:
 *					  readlink shouldn't iput()
 *	Jun 1998	2.1.106		from Avery Pennarun: glibc scandir()
 *					  exposed a problem in readdir
 *			2.1.107		code-freeze spellchecker run
 *	Aug 1998			2.1.118+ VFS changes
 */

/* todo:
 *	- see Documentation/filesystems/romfs.txt
 *	- use allocated, not stack memory for file names?
 *	- considering write access...
 *	- network (tftp) files?
 *	- merge back some _op tables
 */

/*
 * Sorry about some optimizations and for some goto's.  I just wanted
 * to squeeze some more bytes out of this code.. :)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/romfs_fs.h>
#include <linux/fs.h>
#include <linux/locks.h>
#include <linux/init.h>

#include <asm/uaccess.h>

static int inline min(int a, int b)
{
	return a<b ? a : b;
}

static __s32
romfs_checksum(void *data, int size)
{
	__s32 sum, *ptr;

	sum = 0; ptr = data;
	size>>=2;
	while (size>0) {
		sum += ntohl(*ptr++);
		size--;
	}
	return sum;
}

static struct super_operations romfs_ops;

static struct super_block *
romfs_read_super(struct super_block *s, void *data, int silent)
{
	struct buffer_head *bh;
	kdev_t dev = s->s_dev;
	struct romfs_super_block *rsb;
	int sz;

	MOD_INC_USE_COUNT;

	/* I would parse the options here, but there are none.. :) */

	lock_super(s);
	set_blocksize(dev, ROMBSIZE);
	s->s_blocksize = ROMBSIZE;
	s->s_blocksize_bits = ROMBSBITS;
	bh = bread(dev, 0, ROMBSIZE);
	if (!bh) {
		/* XXX merge with other printk? */
                printk ("romfs: unable to read superblock\n");
		goto outnobh;
	}

	rsb = (struct romfs_super_block *)bh->b_data;
	sz = ntohl(rsb->size);
	if (rsb->word0 != ROMSB_WORD0 || rsb->word1 != ROMSB_WORD1
	   || sz < ROMFH_SIZE) {
		if (!silent)
			printk ("VFS: Can't find a romfs filesystem on dev "
				"%s.\n", kdevname(dev));
		goto out;
	}
	if (romfs_checksum(rsb, min(sz,512))) {
		printk ("romfs: bad initial checksum on dev "
			"%s.\n", kdevname(dev));
		goto out;
	}

	s->s_magic = ROMFS_MAGIC;
	s->u.romfs_sb.s_maxsize = sz;

	s->s_flags |= MS_RDONLY;

	/* Find the start of the fs */
	sz = (ROMFH_SIZE +
	      strnlen(rsb->name, ROMFS_MAXFN) + 1 + ROMFH_PAD)
	     & ROMFH_MASK;

	brelse(bh);

	s->s_op	= &romfs_ops;
	s->s_root = d_alloc_root(iget(s, sz), NULL);

	if (!s->s_root)
		goto outnobh;

	unlock_super(s);

	/* Ehrhm; sorry.. :)  And thanks to Hans-Joachim Widmaier  :) */
	if (0) {
out:
		brelse(bh);
outnobh:
		s->s_dev = 0;
		unlock_super(s);
		MOD_DEC_USE_COUNT;
		s = NULL;
	}

	return s;
}

/* Nothing to do.. */

static void
romfs_put_super(struct super_block *sb)
{
	MOD_DEC_USE_COUNT;
	return;
}

/* That's simple too. */

static int
romfs_statfs(struct super_block *sb, struct statfs *buf, int bufsize)
{
	struct statfs tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.f_type = ROMFS_MAGIC;
	tmp.f_bsize = ROMBSIZE;
	tmp.f_blocks = (sb->u.romfs_sb.s_maxsize+ROMBSIZE-1)>>ROMBSBITS;
	tmp.f_namelen = ROMFS_MAXFN;
	return copy_to_user(buf, &tmp, bufsize) ? -EFAULT : 0;
}

/* some helper routines */

static int
romfs_strnlen(struct inode *i, unsigned long offset, unsigned long count)
{
	struct buffer_head *bh;
	unsigned long avail, maxsize, res;

	maxsize = i->i_sb->u.romfs_sb.s_maxsize;
	if (offset >= maxsize)
		return -1;

	/* strnlen is almost always valid */
	if (count > maxsize || offset+count > maxsize)
		count = maxsize-offset;

	bh = bread(i->i_dev, offset>>ROMBSBITS, ROMBSIZE);
	if (!bh)
		return -1;		/* error */

	avail = ROMBSIZE - (offset & ROMBMASK);
	maxsize = min(count, avail);
	res = strnlen(((char *)bh->b_data)+(offset&ROMBMASK), maxsize);
	brelse(bh);

	if (res < maxsize)
		return res;		/* found all of it */

	while (res < count) {
		offset += maxsize;

		bh = bread(i->i_dev, offset>>ROMBSBITS, ROMBSIZE);
		if (!bh)
			return -1;
		maxsize = min(count-res, ROMBSIZE);
		avail = strnlen(bh->b_data, maxsize);
		res += avail;
		brelse(bh);
		if (avail < maxsize)
			return res;
	}
	return res;
}

static int
romfs_copyfrom(struct inode *i, void *dest, unsigned long offset, unsigned long count)
{
	struct buffer_head *bh;
	unsigned long avail, maxsize, res;

	maxsize = i->i_sb->u.romfs_sb.s_maxsize;
	if (offset >= maxsize || count > maxsize || offset+count>maxsize)
		return -1;

	bh = bread(i->i_dev, offset>>ROMBSBITS, ROMBSIZE);
	if (!bh)
		return -1;		/* error */

	avail = ROMBSIZE - (offset & ROMBMASK);
	maxsize = min(count, avail);
	memcpy(dest, ((char *)bh->b_data) + (offset & ROMBMASK), maxsize);
	brelse(bh);

	res = maxsize;			/* all of it */

	while (res < count) {
		offset += maxsize;
		dest += maxsize;

		bh = bread(i->i_dev, offset>>ROMBSBITS, ROMBSIZE);
		if (!bh)
			return -1;
		maxsize = min(count-res, ROMBSIZE);
		memcpy(dest, bh->b_data, maxsize);
		brelse(bh);
		res += maxsize;
	}
	return res;
}

static int
romfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *i = filp->f_dentry->d_inode;
	struct romfs_inode ri;
	unsigned long offset, maxoff;
	int j, ino, nextfh;
	int stored = 0;
	char fsname[ROMFS_MAXFN];	/* XXX dynamic? */

	if (!i || !S_ISDIR(i->i_mode))
		return -EBADF;

	maxoff = i->i_sb->u.romfs_sb.s_maxsize;

	offset = filp->f_pos;
	if (!offset) {
		offset = i->i_ino & ROMFH_MASK;
		if (romfs_copyfrom(i, &ri, offset, ROMFH_SIZE) <= 0)
			return stored;
		offset = ntohl(ri.spec) & ROMFH_MASK;
	}

	/* Not really failsafe, but we are read-only... */
	for(;;) {
		if (!offset || offset >= maxoff) {
			offset = maxoff;
			filp->f_pos = offset;
			return stored;
		}
		filp->f_pos = offset;

		/* Fetch inode info */
		if (romfs_copyfrom(i, &ri, offset, ROMFH_SIZE) <= 0)
			return stored;

		j = romfs_strnlen(i, offset+ROMFH_SIZE, sizeof(fsname)-1);
		if (j < 0)
			return stored;

		fsname[j]=0;
		romfs_copyfrom(i, fsname, offset+ROMFH_SIZE, j);

		ino = offset;
		nextfh = ntohl(ri.next);
		if ((nextfh & ROMFH_TYPE) == ROMFH_HRD)
			ino = ntohl(ri.spec);
		if (filldir(dirent, fsname, j, offset, ino) < 0) {
			return stored;
		}
		stored++;
		offset = nextfh & ROMFH_MASK;
	}
}

static int
romfs_lookup(struct inode *dir, struct dentry *dentry)
{
	unsigned long offset, maxoff;
	int fslen, res;
	struct inode *inode;
	char fsname[ROMFS_MAXFN];	/* XXX dynamic? */
	struct romfs_inode ri;
	const char *name;		/* got from dentry */
	int len;

	res = -EBADF;
	if (!dir || !S_ISDIR(dir->i_mode))
		goto out;

	res = 0;			/* instead of ENOENT */
	offset = dir->i_ino & ROMFH_MASK;
	if (romfs_copyfrom(dir, &ri, offset, ROMFH_SIZE) <= 0)
		goto out;

	maxoff = dir->i_sb->u.romfs_sb.s_maxsize;
	offset = ntohl(ri.spec) & ROMFH_MASK;

	/* OK, now find the file whose name is in "dentry" in the
	 * directory specified by "dir".  */

	name = dentry->d_name.name;
	len = dentry->d_name.len;

	for(;;) {
		if (!offset || offset >= maxoff
		    || romfs_copyfrom(dir, &ri, offset, ROMFH_SIZE) <= 0)
			goto out;

		/* try to match the first 16 bytes of name */
		fslen = romfs_strnlen(dir, offset+ROMFH_SIZE, ROMFH_SIZE);
		if (len < ROMFH_SIZE) {
			if (len == fslen) {
				/* both are shorter, and same size */
				romfs_copyfrom(dir, fsname, offset+ROMFH_SIZE, len+1);
				if (strncmp (name, fsname, len) == 0)
					break;
			}
		} else if (fslen >= ROMFH_SIZE) {
			/* both are longer; XXX optimize max size */
			fslen = romfs_strnlen(dir, offset+ROMFH_SIZE, sizeof(fsname)-1);
			if (len == fslen) {
				romfs_copyfrom(dir, fsname, offset+ROMFH_SIZE, len+1);
				if (strncmp(name, fsname, len) == 0)
					break;
			}
		}
		/* next entry */
		offset = ntohl(ri.next) & ROMFH_MASK;
	}

	/* Hard link handling */
	if ((ntohl(ri.next) & ROMFH_TYPE) == ROMFH_HRD)
		offset = ntohl(ri.spec) & ROMFH_MASK;

	if ((inode = iget(dir->i_sb, offset))==NULL) {
		res = -EACCES;
	} else {
		d_add(dentry, inode);
	}

out:
	return res;
}

/*
 * Ok, we do readpage, to be able to execute programs.  Unfortunately,
 * we can't use bmap, since we have looser alignments.
 */

static int
romfs_readpage(struct file * file, struct page * page)
{
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;
	unsigned long buf;
	unsigned long offset, avail, readlen;
	int result = -EIO;

	atomic_inc(&page->count);
	set_bit(PG_locked, &page->flags);

	buf = page_address(page);
	clear_bit(PG_uptodate, &page->flags);
	clear_bit(PG_error, &page->flags);
	offset = page->offset;
	if (offset < inode->i_size) {
		avail = inode->i_size-offset;
		readlen = min(avail, PAGE_SIZE);
		if (romfs_copyfrom(inode, (void *)buf, inode->u.romfs_i.i_dataoffset+offset, readlen) == readlen) {
			if (readlen < PAGE_SIZE) {
				memset((void *)(buf+readlen),0,PAGE_SIZE-readlen);
			}
			set_bit(PG_uptodate, &page->flags);
			result = 0;
		}
	}
	if (result) {
		set_bit(PG_error, &page->flags);
		memset((void *)buf, 0, PAGE_SIZE);
	}

	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);
	free_page(buf);

	return result;
}

static int
romfs_readlink(struct dentry *dentry, char *buffer, int len)
{
	struct inode *inode = dentry->d_inode;
	int mylen;
	char buf[ROMFS_MAXFN];		/* XXX dynamic */

	if (!inode || !S_ISLNK(inode->i_mode)) {
		mylen = -EBADF;
		goto out;
	}

	mylen = min(sizeof(buf), inode->i_size);

	if (romfs_copyfrom(inode, buf, inode->u.romfs_i.i_dataoffset, mylen) <= 0) {
		mylen = -EIO;
		goto out;
	}
	copy_to_user(buffer, buf, mylen);

out:
	return mylen;
}

static struct dentry *romfs_follow_link(struct dentry *dentry,
					struct dentry *base,
					unsigned int follow)
{
	struct inode *inode = dentry->d_inode;
	char *link;
	int len, cnt;

	len = inode->i_size;

	dentry = ERR_PTR(-EAGAIN);			/* correct? */
	if (!(link = kmalloc(len+1, GFP_KERNEL)))
		goto outnobuf;

	cnt = romfs_copyfrom(inode, link, inode->u.romfs_i.i_dataoffset, len);
	if (len != cnt) {
		dentry = ERR_PTR(-EIO);
		goto out;
	} else
		link[len] = 0;

	dentry = lookup_dentry(link, base, follow);
	kfree(link);

	if (0) {
out:
		kfree(link);
outnobuf:
		dput(base);
	}
	return dentry;
}

/* Mapping from our types to the kernel */

static struct file_operations romfs_file_operations = {
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
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

static struct inode_operations romfs_file_inode_operations = {
	&romfs_file_operations,
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
	romfs_readpage,		/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap -- not really */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
};

static struct file_operations romfs_dir_operations = {
	NULL,			/* lseek - default */
        NULL,			/* read */
	NULL,			/* write - bad */
	romfs_readdir,		/* readdir */
	NULL,			/* poll - default */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	NULL,			/* open */
	NULL,			/* flush */
	NULL,			/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

/* Merged dir/symlink op table.  readdir/lookup/readlink/follow_link
 * will protect from type mismatch.
 */

static struct inode_operations romfs_dir_inode_operations = {
	&romfs_dir_operations,
	NULL,			/* create */
	romfs_lookup,		/* lookup */
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
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
};

static struct inode_operations romfs_link_inode_operations = {
	NULL,			/* no file operations on symlinks */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	romfs_readlink,		/* readlink */
	romfs_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
};

static mode_t romfs_modemap[] =
{
	0, S_IFDIR, S_IFREG, S_IFLNK+0777,
	S_IFBLK, S_IFCHR, S_IFSOCK, S_IFIFO
};

static struct inode_operations *romfs_inoops[] =
{
	NULL,				/* hardlink, handled elsewhere */
	&romfs_dir_inode_operations,
	&romfs_file_inode_operations,
	&romfs_link_inode_operations,
	&blkdev_inode_operations,	/* standard handlers */
	&chrdev_inode_operations,
	NULL,				/* socket */
	NULL,				/* fifo */
};

static void
romfs_read_inode(struct inode *i)
{
	int nextfh, ino;
	struct romfs_inode ri;

	ino = i->i_ino & ROMFH_MASK;
	i->i_op = NULL;
	i->i_mode = 0;

	/* Loop for finding the real hard link */
	for(;;) {
		if (romfs_copyfrom(i, &ri, ino, ROMFH_SIZE) <= 0) {
			printk("romfs: read error for inode 0x%x\n", ino);
			return;
		}
		/* XXX: do romfs_checksum here too (with name) */

		nextfh = ntohl(ri.next);
		if ((nextfh & ROMFH_TYPE) != ROMFH_HRD)
			break;

		ino = ntohl(ri.spec) & ROMFH_MASK;
	}

	i->i_nlink = 1;		/* Hard to decide.. */
	i->i_size = ntohl(ri.size);
	i->i_mtime = i->i_atime = i->i_ctime = 0;
	i->i_uid = i->i_gid = 0;

	i->i_op = romfs_inoops[nextfh & ROMFH_TYPE];

	/* Precalculate the data offset */
	ino = romfs_strnlen(i, ino+ROMFH_SIZE, ROMFS_MAXFN);
	if (ino >= 0)
		ino = ((ROMFH_SIZE+ino+1+ROMFH_PAD)&ROMFH_MASK);
	else
		ino = 0;

	i->u.romfs_i.i_metasize = ino;
	i->u.romfs_i.i_dataoffset = ino+(i->i_ino&ROMFH_MASK);

	/* Compute permissions */
	ino = S_IRUGO|S_IWUSR;
	ino |= romfs_modemap[nextfh & ROMFH_TYPE];
	if (nextfh & ROMFH_EXEC) {
		ino |= S_IXUGO;
	}
	i->i_mode = ino;

	if (S_ISFIFO(ino))
		init_fifo(i);
	else if (S_ISDIR(ino))
		i->i_size = i->u.romfs_i.i_metasize;
	else if (S_ISBLK(ino) || S_ISCHR(ino)) {
		i->i_mode &= ~(S_IRWXG|S_IRWXO);
		ino = ntohl(ri.spec);
		i->i_rdev = MKDEV(ino>>16,ino&0xffff);
	}
}

static struct super_operations romfs_ops = {
	romfs_read_inode,	/* read inode */
	NULL,			/* write inode */
	NULL,			/* put inode */
	NULL,			/* delete inode */
	NULL,			/* notify change */
	romfs_put_super,	/* put super */
	NULL,			/* write super */
	romfs_statfs,		/* statfs */
	NULL			/* remount */
};

static struct file_system_type romfs_fs_type = {
	"romfs",
	FS_REQUIRES_DEV,
	romfs_read_super,
	NULL
};

__initfunc(int init_romfs_fs(void))
{
	return register_filesystem(&romfs_fs_type);
}

#ifdef MODULE

/* Yes, works even as a module... :) */

EXPORT_NO_SYMBOLS;

int
init_module(void)
{
	return init_romfs_fs();
}

void
cleanup_module(void)
{
	unregister_filesystem(&romfs_fs_type);
}
#endif

/*
 *  linux/fs/affs/dir.c
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1992  Eric Youngdale Modified for ISO9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 *
 *  affs directory handling functions
 */

#include <linux/errno.h>

#include <asm/segment.h>

#include <linux/fs.h>
#include <linux/affs_fs.h>
#include <linux/kernel.h>
#include <linux/affs_fs.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/sched.h>

static int affs_readdir(struct inode *, struct file *, void *, filldir_t);

struct file_operations affs_dir_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read */
	NULL,			/* write - bad */
	affs_readdir,		/* readdir */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	NULL			/* fsync */
};

/*
 * directories can handle most operations...
 */
struct inode_operations affs_dir_inode_operations = {
	&affs_dir_operations,	/* default directory file-ops */
	NULL,			/* create */
	affs_lookup,		/* lookup */
	NULL,			/* link */
	NULL,       		/* unlink */
	NULL,       		/* symlink */
	NULL,       		/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

/* This is used to speed up lookup.  Without this we would need to
make a linear search of the directory to find the file that the
directory read just returned.  This is a single element cache. */

/* struct lookup_cache cache = {0,}; */

static int affs_readdir(struct inode * inode, struct file * filp,
	void * dirent, filldir_t filldir)
{
	int i, j, chain_pos, hash_pos, reclen, ino;
	char *name;
	struct buffer_head *dir_bh;
	struct buffer_head *fh_bh;
	void *dir_data;
	void *fh_data;

#ifdef DEBUG
	printk ("AFFS: readdir: inode=%d f_pos=%d\n",
		inode->i_ino, filp->f_pos);
#endif
	
	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;

	while ((unsigned long)filp->f_pos < 2) {
		if (filp->f_pos == 0) {
			if (filldir (dirent, ".", 1, filp->f_pos, inode->i_ino) < 0)
				return 0;
		} else {
			i = affs_parent_ino (inode);
			if (filldir (dirent, "..", 2, filp->f_pos, i) < 0)
				return 0;
		}
		filp->f_pos++;
	}
	/* No caching here.  I've got 16 megs why should I care?  :-) */
	chain_pos = (filp->f_pos - 2) & 0xffff;
	if (chain_pos == 0xffff)
		return 0;
	hash_pos = (filp->f_pos - 2) >> 16;
#ifdef DEBUG
	printk ("AFFS: hash_pos=%d chain_pos=%d\n", hash_pos, chain_pos);
#endif
	if (!(dir_bh = affs_pread(inode, inode->i_ino, &dir_data)))
		return 0;
	/* HASH_POS should already be on a used entry unless it is
	   the first read of the directory.  Will this break the
	   dirtell thing somehow? */
	i = affs_find_next_hash_entry (AFFS_I2BSIZE (inode), dir_data,
				       &hash_pos);
	j = chain_pos;
	for (;;) {
		if (i <= 0) {
#ifdef DEBUG
			printk ("AFFS: bad f_pos in readdir\n");
#endif
			brelse (dir_bh);
			return 0;
		}
		ino = i;
		if (!(fh_bh = affs_pread (inode, i, &fh_data))) {
			brelse (dir_bh);
			return 0;
		}
		i = affs_get_fh_hash_link (AFFS_I2BSIZE (inode), fh_data);
		if (j == 0) {
			j = 1;
			if (i <= 0) {
				hash_pos++;
				i = affs_find_next_hash_entry (AFFS_I2BSIZE (inode),
							       dir_data, &hash_pos);
				if (i <= 0)
					chain_pos = 0xffff;
				else
					chain_pos = 0;
			} else
				chain_pos++;
			reclen = affs_get_file_name (AFFS_I2BSIZE (inode),
						     fh_data, &name);
			if (filldir (dirent, name, reclen, filp->f_pos, ino) < 0) {
				brelse (fh_bh);
				brelse (dir_bh);
				return 0;
			}
			filp->f_pos = ((hash_pos << 16) | chain_pos) + 2;
		}
		brelse (fh_bh);
		j--;
	}
}

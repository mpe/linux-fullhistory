/*
 *  linux/fs/affs/dir.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1992  Eric Youngdale Modified for ISO9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 *
 *  affs directory handling functions
 *
 */

#include <asm/segment.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/affs_fs.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/amigaffs.h>

static int affs_readdir(struct inode *, struct file *, void *, filldir_t);
static int affs_dir_read(struct inode * inode, struct file * filp, char * buf, int count);

static struct file_operations affs_dir_operations = {
	NULL,			/* lseek - default */
	affs_dir_read,		/* read */
	NULL,			/* write - bad */
	affs_readdir,		/* readdir */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	file_fsync		/* default fsync */
};

/*
 * directories can handle most operations...
 */
struct inode_operations affs_dir_inode_operations = {
	&affs_dir_operations,	/* default directory file-ops */
	affs_create,		/* create */
	affs_lookup,		/* lookup */
	affs_link,		/* link */
	affs_unlink,		/* unlink */
	affs_symlink,		/* symlink */
	affs_mkdir,		/* mkdir */
	affs_rmdir,		/* rmdir */
	NULL,			/* mknod */
	affs_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permissions */
};

static int
affs_dir_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	return -EISDIR;
}

static int
affs_readdir(struct inode *inode, struct file *filp, void *dirent, filldir_t filldir)
{
	int			 j, namelen;
	int			 i;
	int			 hash_pos;
	int			 chain_pos;
	unsigned long		 ino;
	unsigned long		 old;
	int stored;
	char *name;
	struct buffer_head *dir_bh;
	struct buffer_head *fh_bh;
	struct inode	   *dir;

	pr_debug("AFFS: readdir(ino=%ld,f_pos=%lu)\n",inode->i_ino,filp->f_pos);
	

	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;

	stored = 0;
	dir_bh = NULL;
	fh_bh  = NULL;
	dir    = NULL;
	old    = filp->f_pos & 0x80000000;
	filp->f_pos &= 0x7FFFFFFF;

	if (filp->f_pos == 0) {
		filp->private_data = (void *)0;
		if (filldir(dirent,".",1,filp->f_pos,inode->i_ino) < 0) {
			return 0;
		}
		++filp->f_pos;
		stored++;
	}
	if (filp->f_pos == 1) {
		if (filldir(dirent,"..",2,filp->f_pos,affs_parent_ino(inode)) < 0) {
			filp->f_pos |= 0x80000000;
			return stored;
		}
		filp->f_pos = 2;
		stored++;
	}

	/* Read original if this is a link */
	ino = inode->u.affs_i.i_original ? inode->u.affs_i.i_original : inode->i_ino;
	if (!(dir = iget(inode->i_sb,ino)))
		return stored;
	
	chain_pos = (filp->f_pos - 2) & 0xffff;
	hash_pos  = (filp->f_pos - 2) >> 16;
	if (chain_pos == 0xffff) {
		printk("AFFS: more than 65535 entries in chain\n");
		chain_pos = 0;
		hash_pos++;
		filp->f_pos = ((hash_pos << 16) | chain_pos) + 2;
	}
	if (!(dir_bh = affs_bread(inode->i_dev,ino,AFFS_I2BSIZE(inode))))
		goto readdir_done;

	while (!stored || !old) {
		while (hash_pos < AFFS_I2HSIZE(inode) &&
		     !((struct dir_front *)dir_bh->b_data)->hashtable[hash_pos])
			hash_pos++;
		if (hash_pos >= AFFS_I2HSIZE(inode))
			goto readdir_done;
		
		i = htonl(((struct dir_front *)dir_bh->b_data)->hashtable[hash_pos]);
		j = chain_pos;
		/* If the directory hasn't changed since the last call to readdir(),
		 * we can jump directly to where we left off.
		 */
		if (filp->private_data && filp->f_version == dir->i_version) {
			i = (int)filp->private_data;
			j = 0;
			pr_debug("AFFS: readdir() left off=%d\n",i);
		}
		filp->f_version = dir->i_version;
		pr_debug("AFFS: hash_pos=%lu chain_pos=%lu\n", hash_pos, chain_pos);
		while (i) {
			if (!(fh_bh = affs_bread(inode->i_dev,i,AFFS_I2BSIZE(inode)))) {
				printk("AFFS: readdir: Can't get block %d\n",i);
				goto readdir_done;
			}
			ino = i;
			i   = htonl(FILE_END(fh_bh->b_data,inode)->hash_chain);
			if (j == 0)
				break;
			affs_brelse(fh_bh);
			fh_bh = NULL;
			j--;
		}
		if (fh_bh) {
			namelen = affs_get_file_name(AFFS_I2BSIZE(inode),fh_bh->b_data,&name);
			pr_debug("AFFS: readdir(): filldir(..,\"%.*s\",ino=%lu), i=%lu\n",
				 namelen,name,ino,i);
			filp->private_data = (void *)ino;
			if (filldir(dirent,name,namelen,filp->f_pos,ino) < 0)
				goto readdir_done;
			filp->private_data = (void *)i;
			affs_brelse(fh_bh);
			fh_bh = NULL;
			stored++;
		}
		if (i == 0) {
			hash_pos++;
			chain_pos = 0;
		} else
			chain_pos++;
		filp->f_pos = ((hash_pos << 16) | chain_pos) + 2;
	}

readdir_done:
	filp->f_pos |= old;
	affs_brelse(dir_bh);
	affs_brelse(fh_bh);
	iput(dir);
	pr_debug("AFFS: readdir()=%d\n",stored);
	return stored;
}

/*
 *  linux/fs/sysv/dir.c
 *
 *  minix/dir.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/dir.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/dir.c
 *  Copyright (C) 1993  Bruno Haible
 *
 *  SystemV/Coherent directory handling functions
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/sysv_fs.h>
#include <linux/stat.h>
#include <linux/string.h>

#include <asm/segment.h>

static int sysv_dir_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	return -EISDIR;
}

static int sysv_readdir(struct inode *, struct file *, void *, filldir_t);

static struct file_operations sysv_dir_operations = {
	NULL,			/* lseek - default */
	sysv_dir_read,		/* read */
	NULL,			/* write - bad */
	sysv_readdir,		/* readdir */
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
struct inode_operations sysv_dir_inode_operations = {
	&sysv_dir_operations,	/* default directory file-ops */
	sysv_create,		/* create */
	sysv_lookup,		/* lookup */
	sysv_link,		/* link */
	sysv_unlink,		/* unlink */
	sysv_symlink,		/* symlink */
	sysv_mkdir,		/* mkdir */
	sysv_rmdir,		/* rmdir */
	sysv_mknod,		/* mknod */
	sysv_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	sysv_truncate,		/* truncate */
	NULL			/* permission */
};

static int sysv_readdir(struct inode * inode, struct file * filp,
	void * dirent, filldir_t filldir)
{
	struct super_block * sb;
	unsigned int offset,i;
	struct buffer_head * bh;
	char* bh_data;
	struct sysv_dir_entry * de, sde;

	if (!inode || !(sb = inode->i_sb) || !S_ISDIR(inode->i_mode))
		return -EBADF;
	if ((unsigned long)(filp->f_pos) % SYSV_DIRSIZE)
		return -EBADF;
	while (filp->f_pos < inode->i_size) {
		offset = filp->f_pos & sb->sv_block_size_1;
		bh = sysv_file_bread(inode, filp->f_pos >> sb->sv_block_size_bits, 0);
		if (!bh) {
			filp->f_pos += sb->sv_block_size - offset;
			continue;
		}
		bh_data = bh->b_data;
		while (offset < sb->sv_block_size && filp->f_pos < inode->i_size) {
			de = (struct sysv_dir_entry *) (offset + bh_data);
			if (de->inode) {
				/* Copy the directory entry first, because the directory
				 * might be modified while we sleep in filldir()...
				 */
				memcpy(&sde, de, sizeof(struct sysv_dir_entry));

				if (sde.inode > inode->i_sb->sv_ninodes)
					printk("sysv_readdir: Bad inode number on dev "
					       "%s, ino %ld, offset 0x%04lx: %d is out of range\n",
					       kdevname(inode->i_dev),
					       inode->i_ino, (off_t) filp->f_pos, sde.inode);

				i = strnlen(sde.name, SYSV_NAMELEN);
				if (filldir(dirent, sde.name, i, filp->f_pos, sde.inode) < 0) {
					brelse(bh);
					return 0;
				}
			}
			offset += SYSV_DIRSIZE;
			filp->f_pos += SYSV_DIRSIZE;
		}
		brelse(bh);
	}
	return 0;
}

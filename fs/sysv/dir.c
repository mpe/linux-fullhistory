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

#ifdef MODULE
#include <linux/module.h>
#endif

#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/sysv_fs.h>
#include <linux/stat.h>

#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x) (((x)+3) & ~3)

static int sysv_dir_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	return -EISDIR;
}

static int sysv_readdir(struct inode *, struct file *, struct dirent *, int);

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
	NULL,			/* bmap */
	sysv_truncate,		/* truncate */
	NULL			/* permission */
};

static int sysv_readdir1 (struct inode * inode, struct file * filp,
	struct dirent * dirent)
{
	struct super_block * sb;
	unsigned int offset,i;
	char c;
	struct buffer_head * bh;
	char* bh_data;
	struct sysv_dir_entry * de;

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
			offset += SYSV_DIRSIZE;
			filp->f_pos += SYSV_DIRSIZE;
			if (de->inode) {
				struct sysv_dir_entry sde;

				/* Copy the directory entry first, because the directory
				 * might be modified while we sleep in put_fs_byte...
				 */
				memcpy(&sde, de, sizeof(struct sysv_dir_entry));

				for (i = 0; i < SYSV_NAMELEN; i++)
					if ((c = sde.name[i]) != 0)
						put_fs_byte(c,i+dirent->d_name);
					else
						break;
				if (i) {
					if (sde.inode > inode->i_sb->sv_ninodes)
						printk("sysv_readdir: Bad inode number on dev 0x%04x, ino %ld, offset 0x%04lx: %d is out of range\n",
				                        inode->i_dev, inode->i_ino, (off_t) filp->f_pos - SYSV_DIRSIZE, sde.inode);
					put_fs_long(sde.inode,&dirent->d_ino);
					put_fs_byte(0,i+dirent->d_name);
					put_fs_word(i,&dirent->d_reclen);
					brelse(bh);
					return ROUND_UP(NAME_OFFSET(dirent)+i+1);
				}
			}
		}
		brelse(bh);
	}
	return 0;
}

static int sysv_readdir(struct inode * inode, struct file * filp,
	struct dirent * dirent, int count)
{
	int retval, stored;

	/* compatibility */
	if (count==1)
		return sysv_readdir1(inode,filp,dirent);

	stored = 0;
	while (count >= sizeof(struct dirent)) {
		retval = sysv_readdir1(inode,filp,dirent);
		if (retval < 0)
			return retval;
		if (!retval)
			return stored;
		dirent = (struct dirent *)((char *) dirent + retval);
		stored += retval;
		count -= retval;
	}
	return stored;
}

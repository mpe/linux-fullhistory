/*
 *  linux/fs/msdos/dir.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  MS-DOS directory handling functions
 */

#ifdef MODULE
#include <linux/module.h>
#endif

#include <asm/segment.h>

#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/string.h>

#include "msbuffer.h"

#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x) (((x)+3) & ~3)


#define PRINTK(X)

static int msdos_dir_read(struct inode * inode,struct file * filp, char * buf,int count)
{
	return -EISDIR;
}

static struct file_operations msdos_dir_operations = {
	NULL,			/* lseek - default */
	msdos_dir_read,		/* read */
	NULL,			/* write - bad */
	msdos_readdir,		/* readdir */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	file_fsync		/* fsync */
};

struct inode_operations msdos_dir_inode_operations = {
	&msdos_dir_operations,	/* default directory file-ops */
	msdos_create,		/* create */
	msdos_lookup,		/* lookup */
	NULL,			/* link */
	msdos_unlink,		/* unlink */
	NULL,			/* symlink */
	msdos_mkdir,		/* mkdir */
	msdos_rmdir,		/* rmdir */
	NULL,			/* mknod */
	msdos_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	msdos_bmap,		/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

int msdos_readdir(
	struct inode *inode,
	struct file *filp,
	struct dirent *dirent,	/* dirent in user space */
	int count)
{
	struct super_block *sb = inode->i_sb;
	int ino,i,i2,last;
	char c,*walk;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;

	if (!inode || !S_ISDIR(inode->i_mode)) return -EBADF;
	if (inode->i_ino == MSDOS_ROOT_INO) {
/* Fake . and .. for the root directory. */
		if (filp->f_pos == 2) filp->f_pos = 0;
		else if (filp->f_pos < 2) {
			walk = filp->f_pos++ ? ".." : ".";
			for (i = 0; *walk; walk++)
				put_fs_byte(*walk,dirent->d_name+i++);
			put_fs_long(MSDOS_ROOT_INO,&dirent->d_ino);
			put_fs_byte(0,dirent->d_name+i);
			put_fs_word(i,&dirent->d_reclen);
			return ROUND_UP(NAME_OFFSET(dirent) + i + 1);
		}
	}
	if (filp->f_pos & (sizeof(struct msdos_dir_entry)-1)) return -ENOENT;
	bh = NULL;
	while ((ino = msdos_get_entry(inode,&filp->f_pos,&bh,&de)) > -1) {
		if (!IS_FREE(de->name) && !(de->attr & ATTR_VOLUME)) {
			char bufname[13];
			char *ptname = bufname;
			for (i = last = 0; i < 8; i++) {
				if (!(c = de->name[i])) break;
				if (c >= 'A' && c <= 'Z') c += 32;
				if (c != ' ')
					last = i+1;
				ptname[i] = c;
			}
			i = last;
			ptname[i] = '.';
			i++;
			for (i2 = 0; i2 < 3; i2++) {
				if (!(c = de->ext[i2])) break;
				if (c >= 'A' && c <= 'Z') c += 32;
				if (c != ' ')
					last = i+1;
				ptname[i] = c;
                               i++;
			}
			if ((i = last) != 0) {
				if (!strcmp(de->name,MSDOS_DOT))
					ino = inode->i_ino;
				else if (!strcmp(de->name,MSDOS_DOTDOT))
						ino = msdos_parent_ino(inode,0);
				bufname[i] = '\0';
				put_fs_long(ino,&dirent->d_ino);
				memcpy_tofs(dirent->d_name,bufname,i+1);
				put_fs_word(i,&dirent->d_reclen);
				PRINTK (("readdir avant brelse\n"));
				brelse(bh);
				PRINTK (("readdir retourne %d\n",i));
				return ROUND_UP(NAME_OFFSET(dirent) + i + 1);
			}
		}
	}
	if (bh) brelse(bh);
	return 0;
}

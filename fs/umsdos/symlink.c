/*
 *  linux/fs/umsdos/file.c
 *
 *  Written 1992 by Jacques Gelinas
 *	inspired from linux/fs/msdos/file.c Werner Almesberger
 *
 *  Extended MS-DOS regular file handling primitives
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/umsdos_fs.h>
#include <linux/malloc.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#define PRINTK(x)
#define Printk(x)	printk x

/*
	Read the data associate with the symlink.
	Return length read in buffer or  a negative error code.
*/
static int umsdos_readlink_x (
	struct inode *inode,
	char *buffer,
	long (*msdos_read)(struct inode *, struct file *, char *, unsigned long),
	int bufsiz)
{
	int ret = inode->i_size;
	struct file filp;
	filp.f_pos = 0;
	filp.f_reada = 0;
	if (ret > bufsiz) ret = bufsiz;
	if ((*msdos_read) (inode, &filp, buffer,ret) != ret){
		ret = -EIO;
	}
	return ret;
}

static int UMSDOS_readlink(struct inode * inode, char * buffer, int buflen)
{
	int ret;

	ret = umsdos_readlink_x (inode,buffer,fat_file_read,buflen);
	PRINTK (("readlink %d %x bufsiz %d\n",ret,inode->i_mode,buflen));
	iput(inode);
	return ret;
	
}

static struct file_operations umsdos_symlink_operations = {
	NULL,				/* lseek - default */
	NULL,				/* read */
	NULL,				/* write */
	NULL,				/* readdir - bad */
	NULL,				/* poll - default */
	NULL,				/* ioctl - default */
	NULL,				/* mmap */
	NULL,				/* no special open is needed */
	NULL,				/* release */
	NULL				/* fsync */
};

struct inode_operations umsdos_symlink_inode_operations = {
	&umsdos_symlink_operations,	/* default file operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	UMSDOS_readlink,	/* readlink */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};




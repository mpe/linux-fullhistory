/*
 *  linux/fs/umsdos/file.c
 *
 *  Written 1993 by Jacques Gelinas
 *	inspired from linux/fs/msdos/file.c Werner Almesberger
 *
 *  Extended MS-DOS regular file handling primitives
 */

#include <asm/segment.h>
#include <asm/system.h>

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/msdos_fs.h>
#include <linux/umsdos_fs.h>


#define PRINTK(x)
#define Printk(x)	printk x
/*
	Read a file into user space memory
*/
static int UMSDOS_file_read(
	struct inode *inode,
	struct file *filp,
	char *buf,
    int count)
{
	/* We have to set the access time because msdos don't care */
	int ret = msdos_file_read(inode,filp,buf,count);
	inode->i_atime = CURRENT_TIME;
	inode->i_dirt = 1;
	return ret;
}
/*
	Write a file from user space memory
*/
static int UMSDOS_file_write(
	struct inode *inode,
	struct file *filp,
	char *buf,
    int count)
{
	return msdos_file_write(inode,filp,buf,count);
}
/*
	Truncate a file to 0 length.
*/
static void UMSDOS_truncate(struct inode *inode)
{
	PRINTK (("UMSDOS_truncate\n"));
	msdos_truncate (inode);
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
	inode->i_dirt = 1;
}
/*
	See inode.c
	
	Some entry point are filled dynamically with function pointers
	from the msdos file_operations and file_inode_operations.
	
	The idea is to have the code as independent as possible from
	the msdos file system.
*/

struct file_operations umsdos_file_operations = {
	NULL,				/* lseek - default */
	UMSDOS_file_read,	/* read */
	UMSDOS_file_write,	/* write */
	NULL,				/* readdir - bad */
	NULL,				/* select - default */
	NULL,				/* ioctl - default */
	msdos_mmap,			/* mmap */
	NULL,				/* no special open is needed */
	NULL,				/* release */
	file_fsync			/* fsync */
};

struct inode_operations umsdos_file_inode_operations = {
	&umsdos_file_operations,	/* default file operations */
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
	NULL,			/* bmap */
	UMSDOS_truncate,/* truncate */
	NULL,			/* permission */
	msdos_smap		/* smap */
};



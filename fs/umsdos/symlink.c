/*
 *  linux/fs/umsdos/file.c
 *
 *  Written 1992 by Jacques Gelinas
 *	inspired from linux/fs/msdos/file.c Werner Almesberger
 *
 *  Extended MS-DOS regular file handling primitives
 */
#ifdef MODULE
#include <linux/module.h>
#endif

#include <asm/segment.h>
#include <asm/system.h>

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/umsdos_fs.h>
#include <linux/malloc.h>

#define PRINTK(x)
#define Printk(x)	printk x
/*
	Read the data associate with the symlink.
	Return length read in buffer or  a negative error code.
*/
static int umsdos_readlink_x (
	struct inode *inode,
	char *buffer,
	int (*msdos_read)(struct inode *, struct file *, char *, int),
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
/*
	Follow a symbolic link chain by calling open_namei recursively
	until an inode is found.

	Return 0 if ok, or a negative error code if not.
*/
static int UMSDOS_follow_link(
	struct inode * dir,
	struct inode * inode,
	int flag,
	int mode,
	struct inode ** res_inode)
{
	int ret = -ELOOP;
	*res_inode = NULL;
	if (current->link_count < 5) {
		char *path = (char*)kmalloc(PATH_MAX,GFP_KERNEL);
		if (path == NULL){
			ret = -ENOMEM;
		}else{
			if (!dir) {
				dir = current->fs[1].root;
				dir->i_count++;
			}
			if (!inode){
				PRINTK (("symlink: inode = NULL\n"));
				ret = -ENOENT;
			}else if (!S_ISLNK(inode->i_mode)){
				PRINTK (("symlink: Not ISLNK\n"));
				*res_inode = inode;
				inode = NULL;
				ret = 0;
			}else{
				ret = umsdos_readlink_x (inode,path
					,umsdos_file_read_kmem,PATH_MAX-1);
				if (ret > 0){
					path[ret] = '\0';
					PRINTK (("follow :%s: %d ",path,ret));
					iput(inode);
					inode = NULL;
					current->link_count++;
					ret = open_namei(path,flag,mode,res_inode,dir);
					current->link_count--;
					dir = NULL;
				}else{
					ret = -EIO;
				}
			}
			kfree (path);
		}
	}	
	iput(inode);
	iput(dir);
	PRINTK (("follow_link ret %d\n",ret));
	return ret;
}

static int UMSDOS_readlink(struct inode * inode, char * buffer, int buflen)
{
	int ret = -EINVAL;
	if (S_ISLNK(inode->i_mode)) {
		ret = umsdos_readlink_x (inode,buffer,msdos_file_read,buflen);
	}
	PRINTK (("readlink %d %x bufsiz %d\n",ret,inode->i_mode,buflen));
	iput(inode);
	return ret;
	
}

static struct file_operations umsdos_symlink_operations = {
	NULL,				/* lseek - default */
	NULL,				/* read */
	NULL,				/* write */
	NULL,				/* readdir - bad */
	NULL,				/* select - default */
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
	UMSDOS_follow_link,	/* follow_link */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};




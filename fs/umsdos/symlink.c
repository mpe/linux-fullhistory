/*
 *  linux/fs/umsdos/file.c
 *
 *  Written 1992 by Jacques Gelinas
 *      inspired from linux/fs/msdos/file.c Werner Almesberger
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

static struct file_operations umsdos_symlink_operations;


/*
 * Read the data associate with the symlink.
 * Return length read in buffer or  a negative error code.
 * 
 */

int umsdos_readlink_x (	     struct dentry *dentry,
			     char *buffer,
			     int bufsiz)
{
	size_t size = dentry->d_inode->i_size;
	loff_t loffs = 0;
	ssize_t ret;
	struct file filp;

Printk((KERN_DEBUG "UMSDOS_read: %s/%s, size=%u\n",
dentry->d_parent->d_name.name, dentry->d_name.name, size));

	fill_new_filp (&filp, dentry);
	filp.f_reada = 0;
	filp.f_flags = O_RDONLY;
	filp.f_op    = &umsdos_symlink_operations;

	if (size > bufsiz)
		size = bufsiz;

	ret = fat_file_read (&filp, buffer, size, &loffs);
	if (ret != size) {
		ret = -EIO;
	}
	return ret;
}



static int UMSDOS_readlink (struct dentry *dentry, char *buffer, int buflen)
{
	return umsdos_readlink_x (dentry, buffer, buflen);
}

/* this one mostly stolen from romfs :) */
static struct dentry *UMSDOS_followlink (struct dentry *dentry, 
					struct dentry *base,
					unsigned int follow)
{
	struct inode *inode = dentry->d_inode;
	char *symname;
	int len, cnt;
	mm_segment_t old_fs = get_fs ();

Printk((KERN_DEBUG "UMSDOS_followlink /mn/: (%s/%s)\n",
dentry->d_parent->d_name.name, dentry->d_name.name));

	len = inode->i_size;

	if (!(symname = kmalloc (len + 1, GFP_KERNEL))) {
		dentry = ERR_PTR (-ENOMEM);
		goto outnobuf;
	}

	set_fs (KERNEL_DS);	/* we read into kernel space this time */
	cnt = umsdos_readlink_x (dentry, symname, len);
	set_fs (old_fs);

	if (len != cnt) {
		dentry = ERR_PTR (-EIO);
		goto out;
	}

	symname[len] = 0;
	dentry = lookup_dentry (symname, base, follow);
	kfree (symname);

	if (0) {
	      out:
		kfree (symname);
	      outnobuf:
		dput (base);
	}
	return dentry;
}

/* needed to patch the file structure */
static struct file_operations umsdos_symlink_operations =
{
	NULL,			/* lseek - default */
	NULL,			/* read */
	NULL,			/* write */
	NULL,			/* readdir - bad */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open is needed */
	NULL,			/* no flush code */
	NULL,			/* release */
	NULL			/* fsync */
};


struct inode_operations umsdos_symlink_inode_operations =
{
	NULL,			/* default file operations (none) */
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
	UMSDOS_followlink,	/* followlink */
	generic_readpage,	/* readpage */
	NULL,			/* writepage */
	fat_bmap,		/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL,			/* updatepage */
	NULL			/* revalidate */
};


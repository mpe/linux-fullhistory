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

static struct file_operations umsdos_symlink_operations;


/*
	Read the data associate with the symlink.
	Return length read in buffer or  a negative error code.
	FIXME, this is messed up.
	
	/mn/ WIP fixing...
*/
static int umsdos_readlink_x (
	struct dentry *dentry,
	char *buffer,
	int bufsiz)
{
	int ret = dentry->d_inode->i_size;
	loff_t loffs = 0;
	struct file filp;
	
	memset (&filp, 0, sizeof (filp));
	
	filp.f_pos = 0;
	filp.f_reada = 0;
	filp.f_flags = O_RDONLY;
	filp.f_dentry = dentry;
        filp.f_op = &umsdos_symlink_operations;	 /* /mn/ - we have to fill it with dummy values so we won't segfault */

	if (ret > bufsiz) ret = bufsiz;

        Printk ((KERN_WARNING "umsdos_readlink_x /mn/: Checkin: filp=%p, buffer=%p, size=%ld, offs=%d\n", &filp, buffer, ret, loffs));
        Printk ((KERN_WARNING "  f_op=%p\n", filp.f_op));
        Printk ((KERN_WARNING "  inode=%d, i_size=%d\n", filp.f_dentry->d_inode->i_ino,filp.f_dentry->d_inode->i_size));
        Printk ((KERN_WARNING "  f_pos=%ld\n", filp.f_pos));
        Printk ((KERN_WARNING "  name=%12s\n", filp.f_dentry->d_name.name));
        Printk ((KERN_WARNING "  i_binary(sb)=%d\n", MSDOS_I(filp.f_dentry->d_inode)->i_binary ));
        Printk ((KERN_WARNING "  f_count=%d, f_flags=%d\n", filp.f_count, filp.f_flags));
        Printk ((KERN_WARNING "  f_owner=%d\n", filp.f_owner));
        Printk ((KERN_WARNING "  f_version=%ld\n", filp.f_version));
        Printk ((KERN_WARNING "  f_reada=%ld, f_ramax=%ld, f_raend=%ld, f_ralen=%ld, f_rawin=%ld\n", filp.f_reada, filp.f_ramax, filp.f_raend, filp.f_ralen, filp.f_rawin));


	/* FIXME */
	Printk (("umsdos_readlink_x: FIXME /mn/: running fat_file_read (%p, %p, %d, %ld)\n", &filp, buffer, ret, loffs));
	if (fat_file_read (&filp, buffer, (size_t) ret, &loffs) != ret){
		ret = -EIO;
	}
#if 0
    {
      struct umsdos_dirent *mydirent=buffer;

      Printk ((KERN_DEBUG "  (DDD) uid=%d\n",mydirent->uid));
      Printk ((KERN_DEBUG "  (DDD) gid=%d\n",mydirent->gid));
      Printk ((KERN_DEBUG "  (DDD) name=>%20s<\n",mydirent->name));
    }
#endif  

	Printk (("umsdos_readlink_x: FIXME /mn/: fat_file_read returned offs=%ld ret=%d\n", loffs, ret));
	return ret;
}



static int UMSDOS_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	int ret;

	Printk (("UMSDOS_readlink: calling umsdos_readlink_x for %20s\n", dentry->d_name.name));
	ret = umsdos_readlink_x (dentry, buffer, buflen);
	Printk (("readlink %d bufsiz %d\n", ret, buflen));
	/* dput(dentry); / * FIXME /mn/ */
	Printk (("UMSDOS_readlink /mn/: FIXME! skipped dput(dentry). returning %d\n", ret));
	return ret;
	
}

static struct dentry *UMSDOS_followlink(struct dentry * dentry,	struct dentry * base)
{
	int ret;
	char symname[256];
	struct inode *inode = dentry->d_inode;
	mm_segment_t old_fs = get_fs();
	Printk ((KERN_ERR "UMSDOS_followlink /mn/: (%s/%s)\n", dentry->d_parent->d_name.name, dentry->d_name.name));

	set_fs (KERNEL_DS);	/* we read into kernel space */
	ret = umsdos_readlink_x (dentry, &symname, 256);
	set_fs (old_fs);
	
	base = creat_dentry (symname, ret, NULL);
/*	UMSDOS_lookup (dentry->d_parent->d_inode, base);*/
	
	return base;
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
	NULL,	/* default file operations */
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
	NULL/*UMSDOS_followlink*/,	/* followlink */	/* /mn/ is this REALLY needed ? I recall seeing it working w/o it... */
	generic_readpage,	/* readpage */	/* in original NULL. changed to generic_readpage. FIXME? /mn/ */
	NULL,			/* writepage */
	fat_bmap,		/* bmap */	/* in original NULL. changed to fat_bmap. FIXME? /mn/ */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL,			/* updatepage */
	NULL			/* revalidate */
	
};




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
	int ret;
	loff_t loffs = 0;
	struct file filp;

	ret = dentry->d_inode->i_size;

	if (!(dentry->d_inode)) {
		return -EBADF;
	}

	fill_new_filp (&filp, dentry);

	filp.f_reada = 0;
	filp.f_flags = O_RDONLY;
	filp.f_op = &umsdos_symlink_operations;		/* /mn/ - we have to fill it with dummy values so we won't segfault */

	if (ret > bufsiz)
		ret = bufsiz;

	PRINTK ((KERN_DEBUG "umsdos_readlink_x /mn/: Checkin: filp=%p, buffer=%p, size=%d, offs=%Lu\n", &filp, buffer, ret, loffs));
	PRINTK ((KERN_DEBUG "  f_op=%p\n", filp.f_op));
	PRINTK ((KERN_DEBUG "  inode=%lu, i_size=%lu\n", filp.f_dentry->d_inode->i_ino, filp.f_dentry->d_inode->i_size));
	PRINTK ((KERN_DEBUG "  f_pos=%Lu\n", filp.f_pos));
	PRINTK ((KERN_DEBUG "  name=%.*s\n", (int) filp.f_dentry->d_name.len, filp.f_dentry->d_name.name));
	PRINTK ((KERN_DEBUG "  i_binary(sb)=%d\n", MSDOS_I (filp.f_dentry->d_inode)->i_binary));
	PRINTK ((KERN_DEBUG "  f_count=%d, f_flags=%d\n", filp.f_count, filp.f_flags));
	PRINTK ((KERN_DEBUG "  f_owner=%d\n", filp.f_owner.uid));
	PRINTK ((KERN_DEBUG "  f_version=%ld\n", filp.f_version));
	PRINTK ((KERN_DEBUG "  f_reada=%ld, f_ramax=%ld, f_raend=%ld, f_ralen=%ld, f_rawin=%ld\n", filp.f_reada, filp.f_ramax, filp.f_raend, filp.f_ralen, filp.f_rawin));


	PRINTK ((KERN_DEBUG "umsdos_readlink_x: FIXME /mn/: running fat_file_read (%p, %p, %d, %Lu)\n", &filp, buffer, ret, loffs));
	if (fat_file_read (&filp, buffer, (size_t) ret, &loffs) != ret) {
		ret = -EIO;
	}
#if 0
	{
		struct umsdos_dirent *mydirent = buffer;

		PRINTK ((KERN_DEBUG "  (DDD) uid=%d\n", mydirent->uid));
		PRINTK ((KERN_DEBUG "  (DDD) gid=%d\n", mydirent->gid));
		PRINTK ((KERN_DEBUG "  (DDD) name=>%.20s<\n", mydirent->name));
	}
#endif

	PRINTK ((KERN_DEBUG "umsdos_readlink_x: FIXME /mn/: fat_file_read returned offs=%Lu ret=%d\n", loffs, ret));
	return ret;
}



static int UMSDOS_readlink (struct dentry *dentry, char *buffer, int buflen)
{
	int ret;

	PRINTK ((KERN_DEBUG "UMSDOS_readlink: calling umsdos_readlink_x for %.*s\n", (int) dentry->d_name.len, dentry->d_name.name));

	ret = umsdos_readlink_x (dentry, buffer, buflen);
	PRINTK ((KERN_DEBUG "readlink %d bufsiz %d\n", ret, buflen));
	/* dput(dentry); / * FIXME /mn/? It seems it is unneeded. d_count is not changed by umsdos_readlink_x */

	Printk ((KERN_WARNING "UMSDOS_readlink /mn/: FIXME! skipped dput(dentry). returning %d\n", ret));
	return ret;

}

/* this one mostly stolen from romfs :) */
static struct dentry *UMSDOS_followlink (struct dentry *dentry, struct dentry *base)
{
	struct inode *inode = dentry->d_inode;
	char *symname = NULL;
	int len, cnt;
	mm_segment_t old_fs = get_fs ();

	Printk ((KERN_DEBUG "UMSDOS_followlink /mn/: (%.*s/%.*s)\n", (int) dentry->d_parent->d_name.len, dentry->d_parent->d_name.name, (int) dentry->d_name.len, dentry->d_name.name));

	len = inode->i_size;

	if (!(symname = kmalloc (len + 1, GFP_KERNEL))) {
		dentry = ERR_PTR (-EAGAIN);	/* correct? */
		goto outnobuf;
	}
	set_fs (KERNEL_DS);	/* we read into kernel space this time */
	PRINTK ((KERN_DEBUG "UMSDOS_followlink /mn/: Here goes umsdos_readlink_x %p, %p, %d\n", dentry, symname, len));
	cnt = umsdos_readlink_x (dentry, symname, len);
	PRINTK ((KERN_DEBUG "UMSDOS_followlink /mn/: back from umsdos_readlink_x %p, %p, %d!\n", dentry, symname, len));
	set_fs (old_fs);
	Printk ((KERN_DEBUG "UMSDOS_followlink /mn/: link name is %.*s with len %d\n", cnt, symname, cnt));

	if (len != cnt) {
		dentry = ERR_PTR (-EIO);
		goto out;
	} else
		symname[len] = 0;

	dentry = lookup_dentry (symname, base, 1);
	kfree (symname);

	if (0) {
	      out:
		kfree (symname);
	      outnobuf:
		dput (base);
	}
	return dentry;
}


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
	NULL,			/* default file operations */
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
	UMSDOS_followlink,	/* followlink *//* /mn/ is this REALLY needed ? I recall seeing it working w/o it... */
	generic_readpage,	/* readpage *//* in original NULL. changed to generic_readpage. FIXME? /mn/ */
	NULL,			/* writepage */
	fat_bmap,		/* bmap *//* in original NULL. changed to fat_bmap. FIXME? /mn/ */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL,			/* updatepage */
	NULL			/* revalidate */

};

/*
 *  linux/fs/devices.c
 *
 * (C) 1993 Matthias Urlichs -- collected common code and tables.
 * 
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Added kerneld support: Jacques Gelinas and Bjorn Ekwall
 *  (changed to kmod)
 */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/errno.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>

#include <linux/tty.h>

/* serial module kmod load support */
struct tty_driver *get_tty_driver(kdev_t device);
#define isa_tty_dev(ma)	(ma == TTY_MAJOR || ma == TTYAUX_MAJOR)
#define need_serial(ma,mi) (get_tty_driver(MKDEV(ma,mi)) == NULL)
#endif

struct device_struct {
	const char * name;
	struct file_operations * fops;
};

static struct device_struct chrdevs[MAX_CHRDEV] = {
	{ NULL, NULL },
};

static struct device_struct blkdevs[MAX_BLKDEV] = {
	{ NULL, NULL },
};

int get_device_list(char * page)
{
	int i;
	int len;

	len = sprintf(page, "Character devices:\n");
	for (i = 0; i < MAX_CHRDEV ; i++) {
		if (chrdevs[i].fops) {
			len += sprintf(page+len, "%3d %s\n", i, chrdevs[i].name);
		}
	}
	len += sprintf(page+len, "\nBlock devices:\n");
	for (i = 0; i < MAX_BLKDEV ; i++) {
		if (blkdevs[i].fops) {
			len += sprintf(page+len, "%3d %s\n", i, blkdevs[i].name);
		}
	}
	return len;
}

/*
	Return the function table of a device.
	Load the driver if needed.
*/
static struct file_operations * get_fops(
	unsigned int major,
	unsigned int minor,
	unsigned int maxdev,
	const char *mangle,		/* String to use to build the module name */
	struct device_struct tb[])
{
	struct file_operations *ret = NULL;

	if (major < maxdev){
#ifdef CONFIG_KMOD
		/*
		 * I do get request for device 0. I have no idea why. It happen
		 * at shutdown time for one. Without the following test, the
		 * kernel will happily trigger a request_module() which will
		 * trigger kmod and modprobe for nothing (since there
		 * is no device with major number == 0. And furthermore
		 * it locks the reboot process :-(
		 *
		 * Jacques Gelinas (jacques@solucorp.qc.ca)
		 *
		 * A. Haritsis <ah@doc.ic.ac.uk>: fix for serial module
		 *  though we need the minor here to check if serial dev,
		 *  we pass only the normal major char dev to kmod 
		 *  as there is no other loadable dev on these majors
		 */
		if ((isa_tty_dev(major) && need_serial(major,minor)) ||
		    (major != 0 && !tb[major].fops)) {
			char name[20];
			sprintf(name, mangle, major);
			request_module(name);
		}
#endif
		ret = tb[major].fops;
	}
	return ret;
}


/*
	Return the function table of a device.
	Load the driver if needed.
*/
struct file_operations * get_blkfops(unsigned int major)
{
	return get_fops (major,0,MAX_BLKDEV,"block-major-%d",blkdevs);
}

struct file_operations * get_chrfops(unsigned int major, unsigned int minor)
{
	return get_fops (major,minor,MAX_CHRDEV,"char-major-%d",chrdevs);
}

int register_chrdev(unsigned int major, const char * name, struct file_operations *fops)
{
	if (major == 0) {
		for (major = MAX_CHRDEV-1; major > 0; major--) {
			if (chrdevs[major].fops == NULL) {
				chrdevs[major].name = name;
				chrdevs[major].fops = fops;
				return major;
			}
		}
		return -EBUSY;
	}
	if (major >= MAX_CHRDEV)
		return -EINVAL;
	if (chrdevs[major].fops && chrdevs[major].fops != fops)
		return -EBUSY;
	chrdevs[major].name = name;
	chrdevs[major].fops = fops;
	return 0;
}

int register_blkdev(unsigned int major, const char * name, struct file_operations *fops)
{
	if (major == 0) {
		for (major = MAX_BLKDEV-1; major > 0; major--) {
			if (blkdevs[major].fops == NULL) {
				blkdevs[major].name = name;
				blkdevs[major].fops = fops;
				return major;
			}
		}
		return -EBUSY;
	}
	if (major >= MAX_BLKDEV)
		return -EINVAL;
	if (blkdevs[major].fops && blkdevs[major].fops != fops)
		return -EBUSY;
	blkdevs[major].name = name;
	blkdevs[major].fops = fops;
	return 0;
}

int unregister_chrdev(unsigned int major, const char * name)
{
	if (major >= MAX_CHRDEV)
		return -EINVAL;
	if (!chrdevs[major].fops)
		return -EINVAL;
	if (strcmp(chrdevs[major].name, name))
		return -EINVAL;
	chrdevs[major].name = NULL;
	chrdevs[major].fops = NULL;
	return 0;
}

int unregister_blkdev(unsigned int major, const char * name)
{
	if (major >= MAX_BLKDEV)
		return -EINVAL;
	if (!blkdevs[major].fops)
		return -EINVAL;
	if (strcmp(blkdevs[major].name, name))
		return -EINVAL;
	blkdevs[major].name = NULL;
	blkdevs[major].fops = NULL;
	return 0;
}

/*
 * This routine checks whether a removable media has been changed,
 * and invalidates all buffer-cache-entries in that case. This
 * is a relatively slow routine, so we have to try to minimize using
 * it. Thus it is called only upon a 'mount' or 'open'. This
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to loose :-)
 */
int check_disk_change(kdev_t dev)
{
	int i;
	struct file_operations * fops;
	struct super_block * sb;

	i = MAJOR(dev);
	if (i >= MAX_BLKDEV || (fops = blkdevs[i].fops) == NULL)
		return 0;
	if (fops->check_media_change == NULL)
		return 0;
	if (!fops->check_media_change(dev))
		return 0;

	printk(KERN_DEBUG "VFS: Disk change detected on device %s\n",
		bdevname(dev));

	sb = get_super(dev);
	if (sb && invalidate_inodes(sb))
		printk("VFS: busy inodes on changed media.\n");

	invalidate_buffers(dev);

	if (fops->revalidate)
		fops->revalidate(dev);
	return 1;
}

/*
 * Called every time a block special file is opened
 */
int blkdev_open(struct inode * inode, struct file * filp)
{
	int ret = -ENODEV;
	filp->f_op = get_blkfops(MAJOR(inode->i_rdev));
	if (filp->f_op != NULL){
		ret = 0;
		if (filp->f_op->open != NULL)
			ret = filp->f_op->open(inode,filp);
	}	
	return ret;
}	

int blkdev_release(struct inode * inode)
{
	struct file_operations *fops = get_blkfops(MAJOR(inode->i_rdev));
	if (fops && fops->release)
		return fops->release(inode,NULL);
	return 0;
}


/*
 * Dummy default file-operations: the only thing this does
 * is contain the open that then fills in the correct operations
 * depending on the special file...
 */
struct file_operations def_blk_fops = {
	NULL,		/* lseek */
	NULL,		/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* poll */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	blkdev_open,	/* open */
	NULL,		/* flush */
	NULL,		/* release */
};

struct inode_operations blkdev_inode_operations = {
	&def_blk_fops,		/* default file operations */
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
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

/*
 * Called every time a character special file is opened
 */
int chrdev_open(struct inode * inode, struct file * filp)
{
	int ret = -ENODEV;

	filp->f_op = get_chrfops(MAJOR(inode->i_rdev), MINOR(inode->i_rdev));
	if (filp->f_op != NULL){
		ret = 0;
		if (filp->f_op->open != NULL)
			ret = filp->f_op->open(inode,filp);
	}
	return ret;
}

/*
 * Dummy default file-operations: the only thing this does
 * is contain the open that then fills in the correct operations
 * depending on the special file...
 */
struct file_operations def_chr_fops = {
	NULL,		/* lseek */
	NULL,		/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* poll */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	chrdev_open,	/* open */
	NULL,		/* flush */
	NULL,		/* release */
};

struct inode_operations chrdev_inode_operations = {
	&def_chr_fops,		/* default file operations */
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
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

/*
 * Print device name (in decimal, hexadecimal or symbolic)
 * Note: returns pointer to static data!
 */
char * kdevname(kdev_t dev)
{
	static char buffer[32];
	sprintf(buffer, "%02x:%02x", MAJOR(dev), MINOR(dev));
	return buffer;
}

char * bdevname(kdev_t dev)
{
	static char buffer[32];
	const char * name = blkdevs[MAJOR(dev)].name;

	if (!name)
		name = "unknown-block";

	sprintf(buffer, "%s(%d,%d)", name, MAJOR(dev), MINOR(dev));
	return buffer;
}

char * cdevname(kdev_t dev)
{
	static char buffer[32];
	const char * name = chrdevs[MAJOR(dev)].name;

	if (!name)
		name = "unknown-char";
	sprintf(buffer, "%s(%d,%d)", name, MAJOR(dev), MINOR(dev));
	return buffer;
}

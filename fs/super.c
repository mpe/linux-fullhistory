/*
 *  linux/fs/super.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
#include <stdarg.h>

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>
 
extern struct file_operations * get_blkfops(unsigned int);
extern struct file_operations * get_chrfops(unsigned int);

extern void wait_for_keypress(void);

extern int root_mountflags;

struct super_block super_blocks[NR_SUPER];

static int do_remount_sb(struct super_block *sb, int flags, char * data);

/* this is initialized in init/main.c */
dev_t ROOT_DEV = 0;

static struct file_system_type * file_systems = NULL;

int register_filesystem(struct file_system_type * fs)
{
	struct file_system_type ** tmp;

	if (!fs)
		return -EINVAL;
	if (fs->next)
		return -EBUSY;
	tmp = &file_systems;
	while (*tmp) {
		if (strcmp((*tmp)->name, fs->name) == 0)
			return -EBUSY;
		tmp = &(*tmp)->next;
	}
	*tmp = fs;
	return 0;
}

int unregister_filesystem(struct file_system_type * fs)
{
	struct file_system_type ** tmp;

	tmp = &file_systems;
	while (*tmp) {
		if (fs == *tmp) {
			*tmp = fs->next;
			fs->next = NULL;
			return 0;
		}
		tmp = &(*tmp)->next;
	}
	return -EINVAL;
}

static int fs_index(const char * __name)
{
	struct file_system_type * tmp;
	char * name;
	int err, index;

	err = getname(__name, &name);
	if (err)
		return err;
	index = 0;
	for (tmp = file_systems ; tmp ; tmp = tmp->next) {
		if (strcmp(tmp->name, name) == 0) {
			putname(name);
			return index;
		}
		index++;
	}
	putname(name);
	return -EINVAL;
}

static int fs_name(unsigned int index, char * buf)
{
	struct file_system_type * tmp;
	int err, len;

	tmp = file_systems;
	while (tmp && index > 0) {
		tmp = tmp->next;
		index--;
	}
	if (!tmp)
		return -EINVAL;
	len = strlen(tmp->name) + 1;
	err = verify_area(VERIFY_WRITE, buf, len);
	if (err)
		return err;
	memcpy_tofs(buf, tmp->name, len);
	return 0;
}

static int fs_maxindex(void)
{
	struct file_system_type * tmp;
	int index;

	index = 0;
	for (tmp = file_systems ; tmp ; tmp = tmp->next)
		index++;
	return index;
}

/*
 * Whee.. Weird sysv syscall. 
 */
asmlinkage int sys_sysfs(int option, ...)
{
	va_list args;
	int retval = -EINVAL;
	unsigned int index;

	va_start(args, option);
	switch (option) {
		case 1:
			retval = fs_index(va_arg(args, const char *));
			break;

		case 2:
			index = va_arg(args, unsigned int);
			retval = fs_name(index, va_arg(args, char *));
			break;

		case 3:
			retval = fs_maxindex();
			break;
	}
	va_end(args);
	return retval;
}

int get_filesystem_list(char * buf)
{
	int len = 0;
	struct file_system_type * tmp;

	tmp = file_systems;
	while (tmp && len < PAGE_SIZE - 80) {
		len += sprintf(buf+len, "%s\t%s\n",
			tmp->requires_dev ? "" : "nodev",
			tmp->name);
		tmp = tmp->next;
	}
	return len;
}

struct file_system_type *get_fs_type(char *name)
{
	struct file_system_type * fs = file_systems;
	
	if (!name)
		return fs;
	while (fs) {
		if (!strcmp(name,fs->name))
			break;
		fs = fs->next;
	}
	return fs;
}

void __wait_on_super(struct super_block * sb)
{
	struct wait_queue wait = { current, NULL };

	add_wait_queue(&sb->s_wait, &wait);
repeat:
	current->state = TASK_UNINTERRUPTIBLE;
	if (sb->s_lock) {
		schedule();
		goto repeat;
	}
	remove_wait_queue(&sb->s_wait, &wait);
	current->state = TASK_RUNNING;
}

void sync_supers(dev_t dev)
{
	struct super_block * sb;

	for (sb = super_blocks + 0 ; sb < super_blocks + NR_SUPER ; sb++) {
		if (!sb->s_dev)
			continue;
		if (dev && sb->s_dev != dev)
			continue;
		wait_on_super(sb);
		if (!sb->s_dev || !sb->s_dirt)
			continue;
		if (dev && (dev != sb->s_dev))
			continue;
		if (sb->s_op && sb->s_op->write_super)
			sb->s_op->write_super(sb);
	}
}

static struct super_block * get_super(dev_t dev)
{
	struct super_block * s;

	if (!dev)
		return NULL;
	s = 0+super_blocks;
	while (s < NR_SUPER+super_blocks)
		if (s->s_dev == dev) {
			wait_on_super(s);
			if (s->s_dev == dev)
				return s;
			s = 0+super_blocks;
		} else
			s++;
	return NULL;
}

void put_super(dev_t dev)
{
	struct super_block * sb;

	if (dev == ROOT_DEV) {
		printk("VFS: Root device %d/%d: prepare for armageddon\n",
							MAJOR(dev), MINOR(dev));
		return;
	}
	if (!(sb = get_super(dev)))
		return;
	if (sb->s_covered) {
		printk("VFS: Mounted device %d/%d - tssk, tssk\n",
						MAJOR(dev), MINOR(dev));
		return;
	}
	if (sb->s_op && sb->s_op->put_super)
		sb->s_op->put_super(sb);
}

static struct super_block * read_super(dev_t dev,char *name,int flags,
				       void *data, int silent)
{
	struct super_block * s;
	struct file_system_type *type;

	if (!dev)
		return NULL;
	check_disk_change(dev);
	s = get_super(dev);
	if (s)
		return s;
	if (!(type = get_fs_type(name))) {
		printk("VFS: on device %d/%d: get_fs_type(%s) failed\n",
						MAJOR(dev), MINOR(dev), name);
		return NULL;
	}
	for (s = 0+super_blocks ;; s++) {
		if (s >= NR_SUPER+super_blocks)
			return NULL;
		if (!s->s_dev)
			break;
	}
	s->s_dev = dev;
	s->s_flags = flags;
	if (!type->read_super(s,data, silent)) {
		s->s_dev = 0;
		return NULL;
	}
	s->s_dev = dev;
	s->s_covered = NULL;
	s->s_rd_only = 0;
	s->s_dirt = 0;
	s->s_type = type;
	return s;
}

/*
 * Unnamed block devices are dummy devices used by virtual
 * filesystems which don't use real block-devices.  -- jrs
 */

static char unnamed_dev_in_use[256/8] = { 0, };

static dev_t get_unnamed_dev(void)
{
	int i;

	for (i = 1; i < 256; i++) {
		if (!set_bit(i,unnamed_dev_in_use))
			return (UNNAMED_MAJOR << 8) | i;
	}
	return 0;
}

static void put_unnamed_dev(dev_t dev)
{
	if (!dev)
		return;
	if (MAJOR(dev) == UNNAMED_MAJOR &&
	    clear_bit(MINOR(dev), unnamed_dev_in_use))
		return;
	printk("VFS: put_unnamed_dev: freeing unused device %d/%d\n",
			MAJOR(dev), MINOR(dev));
}

static int do_umount(dev_t dev)
{
	struct super_block * sb;
	int retval;
	
	if (dev==ROOT_DEV) {
		/* Special case for "unmounting" root.  We just try to remount
		   it readonly, and sync() the device. */
		if (!(sb=get_super(dev)))
			return -ENOENT;
		if (!(sb->s_flags & MS_RDONLY)) {
			fsync_dev(dev);
			retval = do_remount_sb(sb, MS_RDONLY, 0);
			if (retval)
				return retval;
		}
		return 0;
	}
	if (!(sb=get_super(dev)) || !(sb->s_covered))
		return -ENOENT;
	if (!sb->s_covered->i_mount)
		printk("VFS: umount(%d/%d): mounted inode has i_mount=NULL\n",
							MAJOR(dev), MINOR(dev));
	if (!fs_may_umount(dev, sb->s_mounted))
		return -EBUSY;
	sb->s_covered->i_mount = NULL;
	iput(sb->s_covered);
	sb->s_covered = NULL;
	iput(sb->s_mounted);
	sb->s_mounted = NULL;
	if (sb->s_op && sb->s_op->write_super && sb->s_dirt)
		sb->s_op->write_super(sb);
	put_super(dev);
	return 0;
}

/*
 * Now umount can handle mount points as well as block devices.
 * This is important for filesystems which use unnamed block devices.
 *
 * There is a little kludge here with the dummy_inode.  The current
 * vfs release functions only use the r_dev field in the inode so
 * we give them the info they need without using a real inode.
 * If any other fields are ever needed by any block device release
 * functions, they should be faked here.  -- jrs
 */

asmlinkage int sys_umount(char * name)
{
	struct inode * inode;
	dev_t dev;
	int retval;
	struct inode dummy_inode;
	struct file_operations * fops;

	if (!suser())
		return -EPERM;
	retval = namei(name,&inode);
	if (retval) {
		retval = lnamei(name,&inode);
		if (retval)
			return retval;
	}
	if (S_ISBLK(inode->i_mode)) {
		dev = inode->i_rdev;
		if (IS_NODEV(inode)) {
			iput(inode);
			return -EACCES;
		}
	} else {
		if (!inode->i_sb || inode != inode->i_sb->s_mounted) {
			iput(inode);
			return -EINVAL;
		}
		dev = inode->i_sb->s_dev;
		iput(inode);
		memset(&dummy_inode, 0, sizeof(dummy_inode));
		dummy_inode.i_rdev = dev;
		inode = &dummy_inode;
	}
	if (MAJOR(dev) >= MAX_BLKDEV) {
		iput(inode);
		return -ENXIO;
	}
	if (!(retval = do_umount(dev)) && dev != ROOT_DEV) {
		fops = get_blkfops(MAJOR(dev));
		if (fops && fops->release)
			fops->release(inode,NULL);
		if (MAJOR(dev) == UNNAMED_MAJOR)
			put_unnamed_dev(dev);
	}
	if (inode != &dummy_inode)
		iput(inode);
	if (retval)
		return retval;
	fsync_dev(dev);
	return 0;
}

/*
 * do_mount() does the actual mounting after sys_mount has done the ugly
 * parameter parsing. When enough time has gone by, and everything uses the
 * new mount() parameters, sys_mount() can then be cleaned up.
 *
 * We cannot mount a filesystem if it has active, used, or dirty inodes.
 * We also have to flush all inode-data for this device, as the new mount
 * might need new info.
 */
static int do_mount(dev_t dev, const char * dir, char * type, int flags, void * data)
{
	struct inode * dir_i;
	struct super_block * sb;
	int error;

	error = namei(dir,&dir_i);
	if (error)
		return error;
	if (dir_i->i_count != 1 || dir_i->i_mount) {
		iput(dir_i);
		return -EBUSY;
	}
	if (!S_ISDIR(dir_i->i_mode)) {
		iput(dir_i);
		return -ENOTDIR;
	}
	if (!fs_may_mount(dev)) {
		iput(dir_i);
		return -EBUSY;
	}
	sb = read_super(dev,type,flags,data,0);
	if (!sb) {
		iput(dir_i);
		return -EINVAL;
	}
	if (sb->s_covered) {
		iput(dir_i);
		return -EBUSY;
	}
	sb->s_covered = dir_i;
	dir_i->i_mount = sb->s_mounted;
	return 0;		/* we don't iput(dir_i) - see umount */
}


/*
 * Alters the mount flags of a mounted file system. Only the mount point
 * is used as a reference - file system type and the device are ignored.
 * FS-specific mount options can't be altered by remounting.
 */

static int do_remount_sb(struct super_block *sb, int flags, char *data)
{
	int retval;
	
	if (!(flags & MS_RDONLY ) && sb->s_dev && is_read_only(sb->s_dev))
		return -EACCES;
		/*flags |= MS_RDONLY;*/
	/* If we are remounting RDONLY, make sure there are no rw files open */
	if ((flags & MS_RDONLY) && !(sb->s_flags & MS_RDONLY))
		if (!fs_may_remount_ro(sb->s_dev))
			return -EBUSY;
	if (sb->s_op && sb->s_op->remount_fs) {
		retval = sb->s_op->remount_fs(sb, &flags, data);
		if (retval)
			return retval;
	}
	sb->s_flags = (sb->s_flags & ~MS_RMT_MASK) |
		(flags & MS_RMT_MASK);
	return 0;
}

static int do_remount(const char *dir,int flags,char *data)
{
	struct inode *dir_i;
	int retval;

	retval = namei(dir,&dir_i);
	if (retval)
		return retval;
	if (dir_i != dir_i->i_sb->s_mounted) {
		iput(dir_i);
		return -EINVAL;
	}
	retval = do_remount_sb(dir_i->i_sb, flags, data);
	iput(dir_i);
	return retval;
}

static int copy_mount_options (const void * data, unsigned long *where)
{
	int i;
	unsigned long page;
	struct vm_area_struct * vma;

	*where = 0;
	if (!data)
		return 0;

	vma = find_vma(current, (unsigned long) data);
	if (!vma || (unsigned long) data < vma->vm_start)
		return -EFAULT;
	i = vma->vm_end - (unsigned long) data;
	if (PAGE_SIZE <= (unsigned long) i)
		i = PAGE_SIZE-1;
	if (!(page = __get_free_page(GFP_KERNEL))) {
		return -ENOMEM;
	}
	memcpy_fromfs((void *) page,data,i);
	*where = page;
	return 0;
}

/*
 * Flags is a 16-bit value that allows up to 16 non-fs dependent flags to
 * be given to the mount() call (ie: read-only, no-dev, no-suid etc).
 *
 * data is a (void *) that can point to any structure up to
 * PAGE_SIZE-1 bytes, which can contain arbitrary fs-dependent
 * information (or be NULL).
 *
 * NOTE! As old versions of mount() didn't use this setup, the flags
 * has to have a special 16-bit magic number in the hight word:
 * 0xC0ED. If this magic word isn't present, the flags and data info
 * isn't used, as the syscall assumes we are talking to an older
 * version that didn't understand them.
 */
asmlinkage int sys_mount(char * dev_name, char * dir_name, char * type,
	unsigned long new_flags, void * data)
{
	struct file_system_type * fstype;
	struct inode * inode;
	struct file_operations * fops;
	dev_t dev;
	int retval;
	char * t;
	unsigned long flags = 0;
	unsigned long page = 0;

	if (!suser())
		return -EPERM;
	if ((new_flags &
	     (MS_MGC_MSK | MS_REMOUNT)) == (MS_MGC_VAL | MS_REMOUNT)) {
		retval = copy_mount_options (data, &page);
		if (retval < 0)
			return retval;
		retval = do_remount(dir_name,
				    new_flags & ~MS_MGC_MSK & ~MS_REMOUNT,
				    (char *) page);
		free_page(page);
		return retval;
	}
	retval = copy_mount_options (type, &page);
	if (retval < 0)
		return retval;
	fstype = get_fs_type((char *) page);
	free_page(page);
	if (!fstype)		
		return -ENODEV;
	t = fstype->name;
	fops = NULL;
	if (fstype->requires_dev) {
		retval = namei(dev_name,&inode);
		if (retval)
			return retval;
		if (!S_ISBLK(inode->i_mode)) {
			iput(inode);
			return -ENOTBLK;
		}
		if (IS_NODEV(inode)) {
			iput(inode);
			return -EACCES;
		}
		dev = inode->i_rdev;
		if (MAJOR(dev) >= MAX_BLKDEV) {
			iput(inode);
			return -ENXIO;
		}
		fops = get_blkfops(MAJOR(dev));
		if (!fops) {
			iput(inode);
			return -ENOTBLK;
		}
		if (fops->open) {
			struct file dummy;	/* allows read-write or read-only flag */
			memset(&dummy, 0, sizeof(dummy));
			dummy.f_inode = inode;
			dummy.f_mode = (new_flags & MS_RDONLY) ? 1 : 3;
			retval = fops->open(inode, &dummy);
			if (retval) {
				iput(inode);
				return retval;
			}
		}

	} else {
		if (!(dev = get_unnamed_dev()))
			return -EMFILE;
		inode = NULL;
	}
	page = 0;
	if ((new_flags & MS_MGC_MSK) == MS_MGC_VAL) {
		flags = new_flags & ~MS_MGC_MSK;
		retval = copy_mount_options(data, &page);
		if (retval < 0) {
			iput(inode);
			return retval;
		}
	}
	retval = do_mount(dev,dir_name,t,flags,(void *) page);
	free_page(page);
	if (retval && fops && fops->release)
		fops->release(inode, NULL);
	iput(inode);
	return retval;
}

void mount_root(void)
{
	struct file_system_type * fs_type;
	struct super_block * sb;
	struct inode * inode, d_inode;
	struct file filp;
	int retval;

	memset(super_blocks, 0, sizeof(super_blocks));
#ifdef CONFIG_BLK_DEV_FD
	if (MAJOR(ROOT_DEV) == FLOPPY_MAJOR) {
		printk(KERN_NOTICE "VFS: Insert root floppy and press ENTER\n");
		wait_for_keypress();
	}
#endif

	memset(&filp, 0, sizeof(filp));
	memset(&d_inode, 0, sizeof(d_inode));
	d_inode.i_rdev = ROOT_DEV;
	filp.f_inode = &d_inode;
	if ( root_mountflags & MS_RDONLY)
		filp.f_mode = 1; /* read only */
	else
		filp.f_mode = 3; /* read write */
	retval = blkdev_open(&d_inode, &filp);
	if(retval == -EROFS){
		root_mountflags |= MS_RDONLY;
		filp.f_mode = 1;
		retval = blkdev_open(&d_inode, &filp);
	}

	for (fs_type = file_systems ; fs_type ; fs_type = fs_type->next) {
		if(retval)
			break;
		if (!fs_type->requires_dev)
			continue;
		sb = read_super(ROOT_DEV,fs_type->name,root_mountflags,NULL,1);
		if (sb) {
			inode = sb->s_mounted;
			inode->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1 */
			sb->s_covered = inode;
			sb->s_flags = root_mountflags;
			current->fs->pwd = inode;
			current->fs->root = inode;
			printk ("VFS: Mounted root (%s filesystem)%s.\n",
				fs_type->name,
				(sb->s_flags & MS_RDONLY) ? " readonly" : "");
			return;
		}
	}
	panic("VFS: Unable to mount root fs on %02x:%02x",
		MAJOR(ROOT_DEV), MINOR(ROOT_DEV));
}

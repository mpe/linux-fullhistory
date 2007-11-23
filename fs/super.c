/*
 *  linux/fs/super.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/locks.h>

#include <asm/system.h>
#include <asm/segment.h>

 
/*
 * The definition of file_systems that used to be here is now in
 * filesystems.c.  Now super.c contains no fs specific code.  -- jrs
 */

extern struct file_system_type file_systems[];
extern struct file_operations * blkdev_fops[];

extern void wait_for_keypress(void);
extern void fcntl_init_locks(void);

extern int root_mountflags;

struct super_block super_block[NR_SUPER];

/* this is initialized in init/main.c */
dev_t ROOT_DEV = 0;

struct file_system_type *get_fs_type(char *name)
{
	int a;
	
	if (!name)
		return &file_systems[0];
	for(a = 0 ; file_systems[a].read_super ; a++)
		if (!strcmp(name,file_systems[a].name))
			return(&file_systems[a]);
	return NULL;
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

	for (sb = super_block + 0 ; sb < super_block + NR_SUPER ; sb++) {
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
	s = 0+super_block;
	while (s < NR_SUPER+super_block)
		if (s->s_dev == dev) {
			wait_on_super(s);
			if (s->s_dev == dev)
				return s;
			s = 0+super_block;
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
	for (s = 0+super_block ;; s++) {
		if (s >= NR_SUPER+super_block)
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
	return s;
}

/*
 * Unnamed block devices are dummy devices used by virtual
 * filesystems which don't use real block-devices.  -- jrs
 */

static char unnamed_dev_in_use[256];

static dev_t get_unnamed_dev(void)
{
	static int first_use = 0;
	int i;

	if (first_use == 0) {
		first_use = 1;
		memset(unnamed_dev_in_use, 0, sizeof(unnamed_dev_in_use));
		unnamed_dev_in_use[0] = 1; /* minor 0 (nodev) is special */
	}
	for (i = 0; i < 256; i++) {
		if (!unnamed_dev_in_use[i]) {
			unnamed_dev_in_use[i] = 1;
			return (UNNAMED_MAJOR << 8) | i;
		}
	}
	return 0;
}

static void put_unnamed_dev(dev_t dev)
{
	if (!dev)
		return;
	if (!unnamed_dev_in_use[dev]) {
		printk("VFS: put_unnamed_dev: freeing unused device %d/%d\n",
							MAJOR(dev), MINOR(dev));
		return;
	}
	unnamed_dev_in_use[dev] = 0;
}

static int do_umount(dev_t dev)
{
	struct super_block * sb;

	if (dev==ROOT_DEV)
		return -EBUSY;
	if (!(sb=get_super(dev)) || !(sb->s_covered))
		return -ENOENT;
	if (!sb->s_covered->i_mount)
		printk("VFS: umount(%d/%d): mounted inode has i_mount=0\n",
							MAJOR(dev), MINOR(dev));
	if (!fs_may_umount(dev, sb->s_mounted))
		return -EBUSY;
	sb->s_covered->i_mount=0;
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

int sys_umount(char * name)
{
	struct inode * inode;
	dev_t dev;
	int retval;
	struct inode dummy_inode;
	struct file_operations * fops;

	if (!suser())
		return -EPERM;
	retval = namei(name,&inode);
	if (retval)
		return retval;
	if (S_ISBLK(inode->i_mode)) {
		dev = inode->i_rdev;
		if (IS_NODEV(inode)) {
			iput(inode);
			return -EACCES;
		}
	} else if (S_ISDIR(inode->i_mode)) {
		if (!inode || !inode->i_sb || inode != inode->i_sb->s_mounted) {
			iput(inode);
			return -EINVAL;
		}
		dev = inode->i_sb->s_dev;
		iput(inode);
		memset(&dummy_inode, 0, sizeof(dummy_inode));
		dummy_inode.i_rdev = dev;
		inode = &dummy_inode;
	} else {
		iput(inode);
		return -EINVAL;
	}
	if (MAJOR(dev) >= MAX_BLKDEV) {
		iput(inode);
		return -ENXIO;
	}
	if (!(retval = do_umount(dev))) {
		fops = blkdev_fops[MAJOR(dev)];
		if (fops && fops->release)
			fops->release(inode,NULL);
		if (MAJOR(dev) == UNNAMED_MAJOR)
			put_unnamed_dev(dev);
	}
	if (inode != &dummy_inode)
		iput(inode);
	if (retval)
		return retval;
	sync_dev(dev);
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
		return -EPERM;
	}
	if (!fs_may_mount(dev)) {
		iput(dir_i);
		return -EBUSY;
	}
	sb = read_super(dev,type,flags,data,0);
	if (!sb || sb->s_covered) {
		iput(dir_i);
		return -EBUSY;
	}
	sb->s_covered = dir_i;
	dir_i->i_mount = 1;
	return 0;		/* we don't iput(dir_i) - see umount */
}


/*
 * Alters the mount flags of a mounted file system. Only the mount point
 * is used as a reference - file system type and the device are ignored.
 * FS-specific mount options can't be altered by remounting.
 */

static int do_remount(const char *dir,int flags)
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
	dir_i->i_sb->s_flags = (dir_i->i_sb->s_flags & ~MS_RMT_MASK) |
		(flags & MS_RMT_MASK);
	iput(dir_i);
	return 0;
}


/*
 * Flags is a 16-bit value that allows up to 16 non-fs dependent flags to
 * be given to the mount() call (ie: read-only, no-dev, no-suid etc).
 *
 * data is a (void *) that can point to any structure up to 4095 bytes, which
 * can contain arbitrary fs-dependent information (or be NULL).
 *
 * NOTE! As old versions of mount() didn't use this setup, the flags has to have
 * a special 16-bit magic number in the hight word: 0xC0ED. If this magic word
 * isn't present, the flags and data info isn't used, as the syscall assumes we
 * are talking to an older version that didn't understand them.
 */
int sys_mount(char * dev_name, char * dir_name, char * type,
	unsigned long new_flags, void * data)
{
	struct file_system_type * fstype;
	struct inode * inode;
	struct file_operations * fops;
	dev_t dev;
	int retval;
	char tmp[100], * t;
	int i;
	unsigned long flags = 0;
	unsigned long page = 0;

	if (!suser())
		return -EPERM;
	if ((new_flags & (MS_MGC_MSK | MS_REMOUNT)) == (MS_MGC_VAL | MS_REMOUNT)) {
		return do_remount(dir_name,new_flags & ~MS_MGC_MSK & ~MS_REMOUNT);
	}
	if (type) {
		for (i = 0 ; i < 100 ; i++) {
			if (TASK_SIZE <= (unsigned long) type)
				return -EFAULT;
			if (!(tmp[i] = get_fs_byte(type++)))
				break;
		}
		t = tmp;
	} else
		t = NULL;
	if (!(fstype = get_fs_type(t)))
		return -ENODEV;
	t = fstype->name;
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
	} else {
		if (!(dev = get_unnamed_dev()))
			return -EMFILE;
		inode = NULL;
	}
	fops = blkdev_fops[MAJOR(dev)];
	if (fops && fops->open) {
		retval = fops->open(inode,NULL);
		if (retval) {
			iput(inode);
			return retval;
		}
	}
	if ((new_flags & MS_MGC_MSK) == MS_MGC_VAL) {
		flags = new_flags & ~MS_MGC_MSK;
		if (data) {
			if ((unsigned long) data >= TASK_SIZE) {
				iput(inode);
				return -EFAULT;
			}
			page = get_free_page(GFP_KERNEL);
			i = TASK_SIZE - (unsigned long) data;
			if (i < 0 || i > 4095)
				i = 4095;
			memcpy_fromfs((void *) page,data,i);
		}
	}
	retval = do_mount(dev,dir_name,t,flags,(void *) page);
	free_page(page);
	if (retval && fops && fops->release)
		fops->release(inode,NULL);
	iput(inode);
	return retval;
}

void mount_root(void)
{
	struct file_system_type * fs_type;
	struct super_block * sb;
	struct inode * inode;

	memset(super_block, 0, sizeof(super_block));
	fcntl_init_locks();
	if (MAJOR(ROOT_DEV) == 2) {
		printk("VFS: Insert root floppy and press ENTER");
		wait_for_keypress();
	}
	for (fs_type = file_systems; fs_type->read_super; fs_type++) {
		if (!fs_type->requires_dev)
			continue;
		sb = read_super(ROOT_DEV,fs_type->name,root_mountflags,NULL,1);
		if (sb) {
			inode = sb->s_mounted;
			inode->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1 */
			sb->s_covered = inode;
			sb->s_flags = root_mountflags;
			current->pwd = inode;
			current->root = inode;
			printk ("VFS: Mounted root (%s filesystem)%s.\n",
				fs_type->name,
				(sb->s_flags & MS_RDONLY) ? " readonly" : "");
			return;
		}
	}
	panic("VFS: Unable to mount root");
}

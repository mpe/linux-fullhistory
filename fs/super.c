/*
 *  linux/fs/super.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  super.c contains code to handle: - mount structures
 *                                   - super-block tables.
 *                                   - mount systemcall
 *                                   - umount systemcall
 *
 *  Added options to /proc/mounts
 *  Torbjörn Lindh (torbjorn.lindh@gopta.se), April 14, 1996.
 *
 * GK 2/5/95  -  Changed to support mounting the root fs via NFS
 *
 *  Added kerneld support: Jacques Gelinas and Bjorn Ekwall
 *  Added change_root: Werner Almesberger & Hans Lermen, Feb '96
 */

#include <stdarg.h>

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mount.h>
#include <linux/malloc.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/mm.h>
#include <linux/fd.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>

#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif
 
#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/nfs_mount.h>

extern void wait_for_keypress(void);
extern struct file_operations * get_blkfops(unsigned int major);
extern void blkdev_release (struct inode *);

extern int root_mountflags;

static int do_remount_sb(struct super_block *sb, int flags, char * data);

/* this is initialized in init/main.c */
kdev_t ROOT_DEV;

struct super_block super_blocks[NR_SUPER];
static struct file_system_type *file_systems = (struct file_system_type *) NULL;
static struct vfsmount *vfsmntlist = (struct vfsmount *) NULL,
                       *vfsmnttail = (struct vfsmount *) NULL,
                       *mru_vfsmnt = (struct vfsmount *) NULL;

/* 
 * This part handles the management of the list of mounted filesystems.
 */
struct vfsmount *lookup_vfsmnt(kdev_t dev)
{
	struct vfsmount *lptr;

	if (vfsmntlist == (struct vfsmount *)NULL)
		return ((struct vfsmount *)NULL);

	if (mru_vfsmnt != (struct vfsmount *)NULL &&
	    mru_vfsmnt->mnt_dev == dev)
		return (mru_vfsmnt);

	for (lptr = vfsmntlist;
	     lptr != (struct vfsmount *)NULL;
	     lptr = lptr->mnt_next)
		if (lptr->mnt_dev == dev) {
			mru_vfsmnt = lptr;
			return (lptr);
		}

	return ((struct vfsmount *)NULL);
	/* NOTREACHED */
}

struct vfsmount *add_vfsmnt(kdev_t dev, const char *dev_name, const char *dir_name)
{
	struct vfsmount *lptr;
	char *tmp;

	lptr = (struct vfsmount *)kmalloc(sizeof(struct vfsmount), GFP_KERNEL);
        if (!lptr)
		return NULL;
	memset(lptr, 0, sizeof(struct vfsmount));

	lptr->mnt_dev = dev;
	lptr->mnt_sem.count = 1;
	if (dev_name && !getname(dev_name, &tmp)) {
		if ((lptr->mnt_devname =
		    (char *) kmalloc(strlen(tmp)+1, GFP_KERNEL)) != (char *)NULL)
			strcpy(lptr->mnt_devname, tmp);
		putname(tmp);
	}
	if (dir_name && !getname(dir_name, &tmp)) {
		if ((lptr->mnt_dirname =
		    (char *) kmalloc(strlen(tmp)+1, GFP_KERNEL)) != (char *)NULL)
			strcpy(lptr->mnt_dirname, tmp);
		putname(tmp);
	}

	if (vfsmntlist == (struct vfsmount *)NULL) {
		vfsmntlist = vfsmnttail = lptr;
	} else {
		vfsmnttail->mnt_next = lptr;
		vfsmnttail = lptr;
	}
	return (lptr);
}

void remove_vfsmnt(kdev_t dev)
{
	struct vfsmount *lptr, *tofree;

	if (vfsmntlist == (struct vfsmount *)NULL)
		return;
	lptr = vfsmntlist;
	if (lptr->mnt_dev == dev) {
		tofree = lptr;
		vfsmntlist = lptr->mnt_next;
		if (vfsmnttail->mnt_dev == dev)
			vfsmnttail = vfsmntlist;
	} else {
		while (lptr->mnt_next != (struct vfsmount *)NULL) {
			if (lptr->mnt_next->mnt_dev == dev)
				break;
			lptr = lptr->mnt_next;
		}
		tofree = lptr->mnt_next;
		if (tofree == (struct vfsmount *)NULL)
			return;
		lptr->mnt_next = lptr->mnt_next->mnt_next;
		if (vfsmnttail->mnt_dev == dev)
			vfsmnttail = lptr;
	}
	kfree(tofree->mnt_devname);
	kfree(tofree->mnt_dirname);
	kfree_s(tofree, sizeof(struct vfsmount));
}

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

#ifdef CONFIG_MODULES
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
#endif

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

static struct proc_fs_info {
	int flag;
	char *str;
} fs_info[] = {
	{ MS_NOEXEC, ",noexec" },
	{ MS_NOSUID, ",nosuid" },
	{ MS_NODEV, ",nodev" },
	{ MS_SYNCHRONOUS, ",sync" },
#ifdef MS_NOSUB			/* Can't find this except in mount.c */
	{ MS_NOSUB, ",nosub" },
#endif
	{ 0, NULL }
};

static struct proc_nfs_info {
	int flag;
	char *str;
} nfs_info[] = {
	{ NFS_MOUNT_SOFT, ",soft" },
	{ NFS_MOUNT_INTR, ",intr" },
	{ NFS_MOUNT_POSIX, ",posix" },
	{ NFS_MOUNT_NOCTO, ",nocto" },
	{ NFS_MOUNT_NOAC, ",noac" },
	{ 0, NULL }
};

int get_filesystem_info( char *buf )
{
	struct vfsmount *tmp = vfsmntlist;
	struct proc_fs_info *fs_infop;
	struct proc_nfs_info *nfs_infop;
	struct nfs_server *nfss;
	int len = 0;

	while ( tmp && len < PAGE_SIZE - 160)
	{
		len += sprintf( buf + len, "%s %s %s %s",
			tmp->mnt_devname, tmp->mnt_dirname, tmp->mnt_sb->s_type->name,
			tmp->mnt_flags & MS_RDONLY ? "ro" : "rw" );
		for (fs_infop = fs_info; fs_infop->flag; fs_infop++) {
		  if (tmp->mnt_flags & fs_infop->flag) {
		    strcpy(buf + len, fs_infop->str);
		    len += strlen(fs_infop->str);
		  }
		}
		if (!strcmp("nfs", tmp->mnt_sb->s_type->name)) {
			nfss = &tmp->mnt_sb->u.nfs_sb.s_server;
			if (nfss->rsize != NFS_DEF_FILE_IO_BUFFER_SIZE) {
				len += sprintf(buf+len, ",rsize=%d",
					       nfss->rsize);
			}
			if (nfss->wsize != NFS_DEF_FILE_IO_BUFFER_SIZE) {
				len += sprintf(buf+len, ",wsize=%d",
					       nfss->wsize);
			}
			if (nfss->timeo != 7*HZ/10) {
				len += sprintf(buf+len, ",timeo=%d",
					       nfss->timeo*10/HZ);
			}
			if (nfss->retrans != 3) {
				len += sprintf(buf+len, ",retrans=%d",
					       nfss->retrans);
			}
			if (nfss->acregmin != 3*HZ) {
				len += sprintf(buf+len, ",acregmin=%d",
					       nfss->acregmin/HZ);
			}
			if (nfss->acregmax != 60*HZ) {
				len += sprintf(buf+len, ",acregmax=%d",
					       nfss->acregmax/HZ);
			}
			if (nfss->acdirmin != 30*HZ) {
				len += sprintf(buf+len, ",acdirmin=%d",
					       nfss->acdirmin/HZ);
			}
			if (nfss->acdirmax != 60*HZ) {
				len += sprintf(buf+len, ",acdirmax=%d",
					       nfss->acdirmax/HZ);
			}
			for (nfs_infop = nfs_info; nfs_infop->flag; nfs_infop++) {
				if (nfss->flags & nfs_infop->flag) {
					strcpy(buf + len, nfs_infop->str);
					len += strlen(nfs_infop->str);
				}
			}
			len += sprintf(buf+len, ",addr=%s",
				       nfss->hostname);
		}
		len += sprintf( buf + len, " 0 0\n" );
		tmp = tmp->mnt_next;
	}

	return len;
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

struct file_system_type *get_fs_type(const char *name)
{
	struct file_system_type * fs = file_systems;
	
	if (!name)
		return fs;
	for (fs = file_systems; fs && strcmp(fs->name, name); fs = fs->next)
		;
#ifdef CONFIG_KERNELD
	if (!fs && (request_module(name) == 0)) {
		for (fs = file_systems; fs && strcmp(fs->name, name); fs = fs->next)
			;
	}
#endif

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

void sync_supers(kdev_t dev)
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

static struct super_block * get_super(kdev_t dev)
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

void put_super(kdev_t dev)
{
	struct super_block * sb;

	if (dev == ROOT_DEV) {
		printk("VFS: Root device %s: prepare for armageddon\n",
		       kdevname(dev));
		return;
	}
	if (!(sb = get_super(dev)))
		return;
	if (sb->s_covered) {
		printk("VFS: Mounted device %s - tssk, tssk\n",
		       kdevname(dev));
		return;
	}
	if (sb->s_op && sb->s_op->put_super)
		sb->s_op->put_super(sb);
}

asmlinkage int sys_ustat(dev_t dev, struct ustat * ubuf)
{
        struct super_block *s;
        struct ustat tmp;
        struct statfs sbuf;
        unsigned long old_fs;
        int error;

        s = get_super(to_kdev_t(dev));
        if (s == NULL)
                return -EINVAL;

        if (!(s->s_op->statfs))
                return -ENOSYS;

        error = verify_area(VERIFY_WRITE,ubuf,sizeof(struct ustat));
        if (error)
                return error;

        old_fs = get_fs();
        set_fs(get_ds());
        s->s_op->statfs(s,&sbuf,sizeof(struct statfs));
        set_fs(old_fs);

        memset(&tmp,0,sizeof(struct ustat));
        tmp.f_tfree = sbuf.f_bfree;
        tmp.f_tinode = sbuf.f_ffree;

        memcpy_tofs(ubuf,&tmp,sizeof(struct ustat));
        return 0;
}

static struct super_block * read_super(kdev_t dev,const char *name,int flags,
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
		printk("VFS: on device %s: get_fs_type(%s) failed\n",
		       kdevname(dev), name);
		return NULL;
	}
	for (s = 0+super_blocks ;; s++) {
		if (s >= NR_SUPER+super_blocks)
			return NULL;
		if (!(s->s_dev))
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

static unsigned int unnamed_dev_in_use[256/(8*sizeof(unsigned int))] = { 0, };

kdev_t get_unnamed_dev(void)
{
	int i;

	for (i = 1; i < 256; i++) {
		if (!set_bit(i,unnamed_dev_in_use))
			return MKDEV(UNNAMED_MAJOR, i);
	}
	return 0;
}

void put_unnamed_dev(kdev_t dev)
{
	if (!dev)
		return;
	if (MAJOR(dev) == UNNAMED_MAJOR &&
	    clear_bit(MINOR(dev), unnamed_dev_in_use))
		return;
	printk("VFS: put_unnamed_dev: freeing unused device %s\n",
			kdevname(dev));
}

static int do_umount(kdev_t dev,int unmount_root)
{
	struct super_block * sb;
	int retval;
	
	if (dev==ROOT_DEV && !unmount_root) {
		/*
		 * Special case for "unmounting" root. We just try to remount
		 * it readonly, and sync() the device.
		 */
		if (!(sb=get_super(dev)))
			return -ENOENT;
		if (!(sb->s_flags & MS_RDONLY)) {
			/*
			 * Make sure all quotas are turned off on this device we need to mount
			 * it readonly so no more writes by the quotasystem.
			 * If later on the remount fails too bad there are no quotas running
			 * anymore. Turn them on again by hand.
			 */
			quota_off(dev, -1);
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
		printk("VFS: umount(%s): mounted inode has i_mount=NULL\n",
		       kdevname(dev));
	/*
	 * Before checking if the filesystem is still busy make sure the kernel
	 * doesn't hold any quotafiles open on that device. If the umount fails
	 * too bad there are no quotas running anymore. Turn them on again by hand.
	 */
	quota_off(dev, -1);
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
	remove_vfsmnt(dev);
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
	kdev_t dev;
	int retval;
	struct inode dummy_inode;

	if (!suser())
		return -EPERM;
	retval = namei(name, &inode);
	if (retval) {
		retval = lnamei(name, &inode);
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
	retval = do_umount(dev,0);
	if (!retval) {
		fsync_dev(dev);
		if (dev != ROOT_DEV) {
			blkdev_release (inode);
			if (MAJOR(dev) == UNNAMED_MAJOR)
				put_unnamed_dev(dev);
		}
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

int do_mount(kdev_t dev, const char * dev_name, const char * dir_name, const char * type, int flags, void * data)
{
	struct inode * dir_i;
	struct super_block * sb;
	struct vfsmount *vfsmnt;
	int error;

	if (!(flags & MS_RDONLY) && dev && is_read_only(dev))
		return -EACCES;
		/*flags |= MS_RDONLY;*/
	error = namei(dir_name, &dir_i);
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
	vfsmnt = add_vfsmnt(dev, dev_name, dir_name);
	if (vfsmnt) {
		vfsmnt->mnt_sb = sb;
		vfsmnt->mnt_flags = flags;
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
	struct vfsmount *vfsmnt;
	
	if (!(flags & MS_RDONLY) && sb->s_dev && is_read_only(sb->s_dev))
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
	vfsmnt = lookup_vfsmnt(sb->s_dev);
	if (vfsmnt)
		vfsmnt->mnt_flags = sb->s_flags;
	return 0;
}

static int do_remount(const char *dir,int flags,char *data)
{
	struct inode *dir_i;
	int retval;

	retval = namei(dir, &dir_i);
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
	if (!(vma->vm_flags & VM_READ))
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
	kdev_t dev;
	int retval;
	const char * t;
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
		retval = namei(dev_name, &inode);
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
	retval = do_mount(dev,dev_name,dir_name,t,flags,(void *) page);
	free_page(page);
	if (retval && fops && fops->release)
		fops->release(inode, NULL);
	iput(inode);
	return retval;
}

static void do_mount_root(void)
{
	struct file_system_type * fs_type;
	struct super_block * sb;
	struct vfsmount *vfsmnt;
	struct inode * inode, d_inode;
	struct file filp;
	int retval;
  
#ifdef CONFIG_ROOT_NFS
	if (MAJOR(ROOT_DEV) == UNNAMED_MAJOR)
		if (nfs_root_init(nfs_root_name, nfs_root_addrs) < 0) {
			printk(KERN_ERR "Root-NFS: Unable to contact NFS "
			    "server for root fs, using /dev/fd0 instead\n");
			ROOT_DEV = MKDEV(FLOPPY_MAJOR, 0);
		}
	if (MAJOR(ROOT_DEV) == UNNAMED_MAJOR) {
		ROOT_DEV = 0;
		if ((fs_type = get_fs_type("nfs"))) {
			sb = &super_blocks[0];
			while (sb->s_dev) sb++;
			sb->s_dev = get_unnamed_dev();
			sb->s_flags = root_mountflags & ~MS_RDONLY;
			if (nfs_root_mount(sb) >= 0) {
				inode = sb->s_mounted;
				inode->i_count += 3 ;
				sb->s_covered = inode;
				sb->s_rd_only = 0;
				sb->s_dirt = 0;
				sb->s_type = fs_type;
				current->fs->pwd = inode;
				current->fs->root = inode;
				ROOT_DEV = sb->s_dev;
				printk (KERN_NOTICE "VFS: Mounted root (nfs filesystem).\n");
				vfsmnt = add_vfsmnt(ROOT_DEV, "rootfs", "/");
				if (!vfsmnt)
					panic("VFS: add_vfsmnt failed for NFS root.\n");
				vfsmnt->mnt_sb = sb;
				vfsmnt->mnt_flags = sb->s_flags;
				return;
			}
			sb->s_dev = 0;
		}
		if (!ROOT_DEV) {
			printk(KERN_ERR "VFS: Unable to mount root fs via NFS, trying floppy.\n");
			ROOT_DEV = MKDEV(FLOPPY_MAJOR, 0);
		}
	}
#endif

#ifdef CONFIG_BLK_DEV_FD
	if (MAJOR(ROOT_DEV) == FLOPPY_MAJOR) {
		floppy_eject();
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
	if (retval == -EROFS) {
		root_mountflags |= MS_RDONLY;
		filp.f_mode = 1;
		retval = blkdev_open(&d_inode, &filp);
	}
	if (retval)
	        /*
		 * Allow the user to distinguish between failed open
		 * and bad superblock on root device.
		 */
		printk("VFS: Cannot open root device %s\n",
		       kdevname(ROOT_DEV));
	else for (fs_type = file_systems ; fs_type ; fs_type = fs_type->next) {
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
			vfsmnt = add_vfsmnt(ROOT_DEV, "rootfs", "/");
			if (!vfsmnt)
				panic("VFS: add_vfsmnt failed for root fs");
			vfsmnt->mnt_sb = sb;
			vfsmnt->mnt_flags = root_mountflags;
			return;
		}
	}
	panic("VFS: Unable to mount root fs on %s",
		kdevname(ROOT_DEV));
}


void mount_root(void)
{
	memset(super_blocks, 0, sizeof(super_blocks));
	do_mount_root();
}


#ifdef CONFIG_BLK_DEV_INITRD

int change_root(kdev_t new_root_dev,const char *put_old)
{
	kdev_t old_root_dev;
	struct vfsmount *vfsmnt;
	struct inode *old_root,*old_pwd,*inode;
	unsigned long old_fs;
	int error;

	old_root = current->fs->root;
	old_pwd = current->fs->pwd;
	old_root_dev = ROOT_DEV;
	if (!fs_may_mount(new_root_dev)) {
		printk(KERN_CRIT "New root is busy. Staying in initrd.\n");
		return -EBUSY;
	}
	ROOT_DEV = new_root_dev;
	do_mount_root();
	old_fs = get_fs();
	set_fs(get_ds());
        error = namei(put_old,&inode);
	if (error) inode = NULL;
	set_fs(old_fs);
	if (!error && (inode->i_count != 1 || inode->i_mount)) error = -EBUSY;
	if (!error && !S_ISDIR(inode->i_mode)) error = -ENOTDIR;
	iput(old_root); /* current->fs->root */
	iput(old_pwd); /* current->fs->pwd */
	if (error) {
		int umount_error;

		if (inode) iput(inode);
		printk(KERN_NOTICE "Trying to unmount old root ... ");
		old_root->i_mount = old_root;
			/* does this belong into do_mount_root ? */
		umount_error = do_umount(old_root_dev,1);
		if (umount_error) printk(KERN_ERR "error %d\n",umount_error);
		else {
			printk(KERN_NOTICE "okay\n");
			invalidate_buffers(old_root_dev);
		}
		return umount_error ? error : 0;
	}
	iput(old_root); /* sb->s_covered */
	remove_vfsmnt(old_root_dev);
	vfsmnt = add_vfsmnt(old_root_dev,"old_rootfs",put_old);
	if (!vfsmnt) printk(KERN_CRIT "Trouble: add_vfsmnt failed\n");
	else {
		vfsmnt->mnt_sb = old_root->i_sb;
		vfsmnt->mnt_sb->s_covered = inode;
		vfsmnt->mnt_flags = vfsmnt->mnt_sb->s_flags;
	}
	inode->i_mount = old_root;
	return 0;
}

#endif

/*
 *  linux/fs/super.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  super.c contains code to handle: - mount structures
 *                                   - super-block tables.
 *                                   - mount system call
 *                                   - umount system call
 *
 *  Added options to /proc/mounts
 *  Torbjörn Lindh (torbjorn.lindh@gopta.se), April 14, 1996.
 *
 * GK 2/5/95  -  Changed to support mounting the root fs via NFS
 *
 *  Added kerneld support: Jacques Gelinas and Bjorn Ekwall
 *  Added change_root: Werner Almesberger & Hans Lermen, Feb '96
 *  Added devfs support: Richard Gooch <rgooch@atnf.csiro.au>, 13-JAN-1998
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/fd.h>
#include <linux/init.h>
#include <linux/quotaops.h>
#include <linux/acct.h>

#include <asm/uaccess.h>

#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/nfs_mount.h>

#include <linux/kmod.h>
#define __NO_VERSION__
#include <linux/module.h>

/*
 * We use a semaphore to synchronize all mount/umount
 * activity - imagine the mess if we have a race between
 * unmounting a filesystem and re-mounting it (or something
 * else).
 */
static DECLARE_MUTEX(mount_sem);

extern void wait_for_keypress(void);

extern int root_mountflags;

static int do_remount_sb(struct super_block *sb, int flags, char * data);

/* this is initialized in init/main.c */
kdev_t ROOT_DEV;

int nr_super_blocks = 0;
int max_super_blocks = NR_SUPER;
LIST_HEAD(super_blocks);

/*
 * Handling of filesystem drivers list.
 * Rules:
 *	Inclusion to/removals from/scanning of list are protected by spinlock.
 *	During the unload module must call unregister_filesystem().
 *	We can access the fields of list element if:
 *		1) spinlock is held or
 *		2) we hold the reference to the module.
 *	The latter can be guaranteed by call of try_inc_mod_count(); if it
 *	returned 0 we must skip the element, otherwise we got the reference.
 *	Once the reference is obtained we can drop the spinlock.
 */

static struct file_system_type *file_systems = NULL;
static spinlock_t file_systems_lock = SPIN_LOCK_UNLOCKED;

static void put_filesystem(struct file_system_type *fs)
{
	if (fs->owner)
		__MOD_DEC_USE_COUNT(fs->owner);
}

static struct file_system_type **find_filesystem(const char *name)
{
	struct file_system_type **p;
	for (p=&file_systems; *p; p=&(*p)->next)
		if (strcmp((*p)->name,name) == 0)
			break;
	return p;
}

int register_filesystem(struct file_system_type * fs)
{
	int res = 0;
	struct file_system_type ** p;

	if (!fs)
		return -EINVAL;
	if (fs->next)
		return -EBUSY;
	spin_lock(&file_systems_lock);
	p = find_filesystem(fs->name);
	if (*p)
		res = -EBUSY;
	else
		*p = fs;
	spin_unlock(&file_systems_lock);
	return res;
}

int unregister_filesystem(struct file_system_type * fs)
{
	struct file_system_type ** tmp;

	spin_lock(&file_systems_lock);
	tmp = &file_systems;
	while (*tmp) {
		if (fs == *tmp) {
			*tmp = fs->next;
			fs->next = NULL;
			spin_unlock(&file_systems_lock);
			return 0;
		}
		tmp = &(*tmp)->next;
	}
	spin_unlock(&file_systems_lock);
	return -EINVAL;
}

static int fs_index(const char * __name)
{
	struct file_system_type * tmp;
	char * name;
	int err, index;

	name = getname(__name);
	err = PTR_ERR(name);
	if (IS_ERR(name))
		return err;

	err = -EINVAL;
	spin_lock(&file_systems_lock);
	for (tmp=file_systems, index=0 ; tmp ; tmp=tmp->next, index++) {
		if (strcmp(tmp->name,name) == 0) {
			err = index;
			break;
		}
	}
	spin_unlock(&file_systems_lock);
	putname(name);
	return err;
}

static int fs_name(unsigned int index, char * buf)
{
	struct file_system_type * tmp;
	int len, res;

	spin_lock(&file_systems_lock);
	for (tmp = file_systems; tmp; tmp = tmp->next, index--)
		if (index <= 0 && try_inc_mod_count(tmp->owner))
				break;
	spin_unlock(&file_systems_lock);
	if (!tmp)
		return -EINVAL;

	/* OK, we got the reference, so we can safely block */
	len = strlen(tmp->name) + 1;
	res = copy_to_user(buf, tmp->name, len) ? -EFAULT : 0;
	put_filesystem(tmp);
	return res;
}

static int fs_maxindex(void)
{
	struct file_system_type * tmp;
	int index;

	spin_lock(&file_systems_lock);
	for (tmp = file_systems, index = 0 ; tmp ; tmp = tmp->next, index++)
		;
	spin_unlock(&file_systems_lock);
	return index;
}

/*
 * Whee.. Weird sysv syscall. 
 */
asmlinkage long sys_sysfs(int option, unsigned long arg1, unsigned long arg2)
{
	int retval = -EINVAL;

	lock_kernel();
	switch (option) {
		case 1:
			retval = fs_index((const char *) arg1);
			break;

		case 2:
			retval = fs_name(arg1, (char *) arg2);
			break;

		case 3:
			retval = fs_maxindex();
			break;
	}
	unlock_kernel();
	return retval;
}

int get_filesystem_list(char * buf)
{
	int len = 0;
	struct file_system_type * tmp;

	spin_lock(&file_systems_lock);
	tmp = file_systems;
	while (tmp && len < PAGE_SIZE - 80) {
		len += sprintf(buf+len, "%s\t%s\n",
			(tmp->fs_flags & FS_REQUIRES_DEV) ? "" : "nodev",
			tmp->name);
		tmp = tmp->next;
	}
	spin_unlock(&file_systems_lock);
	return len;
}

static struct file_system_type *get_fs_type(const char *name)
{
	struct file_system_type *fs;
	
	spin_lock(&file_systems_lock);
	fs = *(find_filesystem(name));
	if (fs && !try_inc_mod_count(fs->owner))
		fs = NULL;
	spin_unlock(&file_systems_lock);
	if (!fs && (request_module(name) == 0)) {
		spin_lock(&file_systems_lock);
		fs = *(find_filesystem(name));
		if (fs && !try_inc_mod_count(fs->owner))
			fs = NULL;
		spin_unlock(&file_systems_lock);
	}
	return fs;
}

struct vfsmount *vfsmntlist = NULL;
static struct vfsmount *vfsmnttail = NULL, *mru_vfsmnt = NULL;

static struct vfsmount *add_vfsmnt(struct super_block *sb,
			const char *dev_name, const char *dir_name)
{
	struct vfsmount *lptr;
	char *tmp, *name;

	lptr = (struct vfsmount *)kmalloc(sizeof(struct vfsmount), GFP_KERNEL);
	if (!lptr)
		goto out;
	memset(lptr, 0, sizeof(struct vfsmount));

	lptr->mnt_sb = sb;
	lptr->mnt_dev = sb->s_dev;

	/* N.B. Is it really OK to have a vfsmount without names? */
	if (dev_name && !IS_ERR(tmp = getname(dev_name))) {
		name = (char *) kmalloc(strlen(tmp)+1, GFP_KERNEL);
		if (name) {
			strcpy(name, tmp);
			lptr->mnt_devname = name;
		}
		putname(tmp);
	}
	if (dir_name && !IS_ERR(tmp = getname(dir_name))) {
		name = (char *) kmalloc(strlen(tmp)+1, GFP_KERNEL);
		if (name) {
			strcpy(name, tmp);
			lptr->mnt_dirname = name;
		}
		putname(tmp);
	}

	if (vfsmntlist == (struct vfsmount *)NULL) {
		vfsmntlist = vfsmnttail = lptr;
	} else {
		vfsmnttail->mnt_next = lptr;
		vfsmnttail = lptr;
	}
out:
	return lptr;
}

void remove_vfsmnt(kdev_t dev)
{
	struct vfsmount *lptr, *tofree;

	if (vfsmntlist == NULL)
		return;
	lptr = vfsmntlist;
	if (lptr->mnt_dev == dev) {
		tofree = lptr;
		vfsmntlist = lptr->mnt_next;
		if (vfsmnttail->mnt_dev == dev)
			vfsmnttail = vfsmntlist;
	} else {
		while (lptr->mnt_next != NULL) {
			if (lptr->mnt_next->mnt_dev == dev)
				break;
			lptr = lptr->mnt_next;
		}
		tofree = lptr->mnt_next;
		if (tofree == NULL)
			return;
		lptr->mnt_next = lptr->mnt_next->mnt_next;
		if (vfsmnttail->mnt_dev == dev)
			vfsmnttail = lptr;
	}
	if (tofree == mru_vfsmnt)
		mru_vfsmnt = NULL;
	kfree(tofree->mnt_devname);
	kfree(tofree->mnt_dirname);
	kfree_s(tofree, sizeof(struct vfsmount));
}

static struct proc_fs_info {
	int flag;
	char *str;
} fs_info[] = {
	{ MS_NOEXEC, ",noexec" },
	{ MS_NOSUID, ",nosuid" },
	{ MS_NODEV, ",nodev" },
	{ MS_SYNCHRONOUS, ",sync" },
	{ MS_MANDLOCK, ",mand" },
	{ MS_NOATIME, ",noatime" },
	{ MS_NODIRATIME, ",nodiratime" },
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
	struct vfsmount *tmp;
	struct proc_fs_info *fs_infop;
	struct proc_nfs_info *nfs_infop;
	struct nfs_server *nfss;
	int len = 0;
	char *path,*buffer = (char *) __get_free_page(GFP_KERNEL);

	if (!buffer) return 0;
	for (tmp = vfsmntlist; tmp && len < PAGE_SIZE - 160;
	    tmp = tmp->mnt_next) {
		path = d_path(tmp->mnt_sb->s_root, buffer, PAGE_SIZE);
		if (!path)
			continue;
		len += sprintf( buf + len, "%s %s %s %s",
			tmp->mnt_devname, path,
			tmp->mnt_sb->s_type->name,
			tmp->mnt_sb->s_flags & MS_RDONLY ? "ro" : "rw" );
		for (fs_infop = fs_info; fs_infop->flag; fs_infop++) {
		  if (tmp->mnt_sb->s_flags & fs_infop->flag) {
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
#if 0
			if (nfss->timeo != 7*HZ/10) {
				len += sprintf(buf+len, ",timeo=%d",
					       nfss->timeo*10/HZ);
			}
			if (nfss->retrans != 3) {
				len += sprintf(buf+len, ",retrans=%d",
					       nfss->retrans);
			}
#endif
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
	}

	free_page((unsigned long) buffer);
	return len;
}

void __wait_on_super(struct super_block * sb)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&sb->s_wait, &wait);
repeat:
	set_current_state(TASK_UNINTERRUPTIBLE);
	if (sb->s_lock) {
		schedule();
		goto repeat;
	}
	remove_wait_queue(&sb->s_wait, &wait);
	current->state = TASK_RUNNING;
}

/*
 * Note: check the dirty flag before waiting, so we don't
 * hold up the sync while mounting a device. (The newly
 * mounted device won't need syncing.)
 */
void sync_supers(kdev_t dev)
{
	struct super_block * sb;

	for (sb = sb_entry(super_blocks.next);
	     sb != sb_entry(&super_blocks); 
	     sb = sb_entry(sb->s_list.next)) {
		if (!sb->s_dev)
			continue;
		if (dev && sb->s_dev != dev)
			continue;
		if (!sb->s_dirt)
			continue;
		/* N.B. Should lock the superblock while writing */
		wait_on_super(sb);
		if (!sb->s_dev || !sb->s_dirt)
			continue;
		if (dev && (dev != sb->s_dev))
			continue;
		if (sb->s_op && sb->s_op->write_super)
			sb->s_op->write_super(sb);
	}
}

struct super_block * get_super(kdev_t dev)
{
	struct super_block * s;

	if (!dev)
		return NULL;
restart:
	s = sb_entry(super_blocks.next);
	while (s != sb_entry(&super_blocks))
		if (s->s_dev == dev) {
			wait_on_super(s);
			if (s->s_dev == dev)
				return s;
			goto restart;
		} else
			s = sb_entry(s->s_list.next);
	return NULL;
}

asmlinkage long sys_ustat(dev_t dev, struct ustat * ubuf)
{
        struct super_block *s;
        struct ustat tmp;
        struct statfs sbuf;
	int err = -EINVAL;

	lock_kernel();
        s = get_super(to_kdev_t(dev));
        if (s == NULL)
                goto out;
	err = vfs_statfs(s, &sbuf);
	if (err)
		goto out;

        memset(&tmp,0,sizeof(struct ustat));
        tmp.f_tfree = sbuf.f_bfree;
        tmp.f_tinode = sbuf.f_ffree;

        err = copy_to_user(ubuf,&tmp,sizeof(struct ustat)) ? -EFAULT : 0;
out:
	unlock_kernel();
	return err;
}

/*
 * Find a super_block with no device assigned.
 */
struct super_block *get_empty_super(void)
{
	struct super_block *s;

	for (s  = sb_entry(super_blocks.next);
	     s != sb_entry(&super_blocks); 
	     s  = sb_entry(s->s_list.next)) {
		if (s->s_dev)
			continue;
		if (!s->s_lock)
			return s;
		printk("VFS: empty superblock %p locked!\n", s);
	}
	/* Need a new one... */
	if (nr_super_blocks >= max_super_blocks)
		return NULL;
	s = kmalloc(sizeof(struct super_block),  GFP_USER);
	if (s) {
		nr_super_blocks++;
		memset(s, 0, sizeof(struct super_block));
		INIT_LIST_HEAD(&s->s_dirty);
		list_add (&s->s_list, super_blocks.prev);
		init_waitqueue_head(&s->s_wait);
		INIT_LIST_HEAD(&s->s_files);
	}
	return s;
}

static struct super_block * read_super(kdev_t dev, struct block_device *bdev,
				       struct file_system_type *type, int flags,
				       void *data, int silent)
{
	struct super_block * s;
	s = get_empty_super();
	if (!s)
		goto out;
	s->s_dev = dev;
	s->s_bdev = bdev;
	s->s_flags = flags;
	s->s_dirt = 0;
	sema_init(&s->s_vfs_rename_sem,1);
	sema_init(&s->s_nfsd_free_path_sem,1);
	s->s_type = type;
	sema_init(&s->s_dquot.dqio_sem, 1);
	sema_init(&s->s_dquot.dqoff_sem, 1);
	s->s_dquot.flags = 0;
	lock_super(s);
	if (!type->read_super(s, data, silent))
		goto out_fail;
	unlock_super(s);
	/* tell bdcache that we are going to keep this one */
	if (bdev)
		atomic_inc(&bdev->bd_count);
out:
	return s;

out_fail:
	s->s_dev = 0;
	s->s_bdev = 0;
	s->s_type = NULL;
	unlock_super(s);
	return NULL;
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
		if (!test_and_set_bit(i,unnamed_dev_in_use))
			return MKDEV(UNNAMED_MAJOR, i);
	}
	return 0;
}

void put_unnamed_dev(kdev_t dev)
{
	if (!dev || MAJOR(dev) != UNNAMED_MAJOR)
		return;
	if (test_and_clear_bit(MINOR(dev), unnamed_dev_in_use))
		return;
	printk("VFS: put_unnamed_dev: freeing unused device %s\n",
			kdevname(dev));
}

static int d_umount(struct super_block * sb)
{
	struct dentry * root = sb->s_root;
	struct dentry * covered = root->d_covers;

	if (root->d_count != 1)
		return -EBUSY;

	if (root->d_inode->i_state)
		return -EBUSY;

	sb->s_root = NULL;

	if (covered != root) {
		root->d_covers = root;
		covered->d_mounts = covered;
		dput(covered);
	}
	dput(root);
	return 0;
}

static void d_mount(struct dentry *covered, struct dentry *dentry)
{
	if (covered->d_mounts != covered) {
		printk("VFS: mount - already mounted\n");
		return;
	}
	covered->d_mounts = dentry;
	dentry->d_covers = covered;
}

static struct block_device *do_umount(kdev_t dev, int unmount_root, int flags)
{
	struct super_block * sb;
	struct block_device *bdev;
	int retval;
	
	retval = -ENOENT;
	sb = get_super(dev);
	if (!sb || !sb->s_root)
		goto out;

	/*
	 * Before checking whether the filesystem is still busy,
	 * make sure the kernel doesn't hold any quota files open
	 * on the device. If the umount fails, too bad -- there
	 * are no quotas running any more. Just turn them on again.
	 */
	DQUOT_OFF(sb);
	acct_auto_close(dev);

	/*
	 * If we may have to abort operations to get out of this
	 * mount, and they will themselves hold resources we must
	 * allow the fs to do things. In the Unix tradition of
	 * 'Gee thats tricky lets do it in userspace' the umount_begin
	 * might fail to complete on the first run through as other tasks
	 * must return, and the like. Thats for the mount program to worry
	 * about for the moment.
	 */
	 
	if( (flags&MNT_FORCE) && sb->s_op->umount_begin)
		sb->s_op->umount_begin(sb);

	/*
	 * Shrink dcache, then fsync. This guarantees that if the
	 * filesystem is quiescent at this point, then (a) only the
	 * root entry should be in use and (b) that root entry is
	 * clean.
	 */
	shrink_dcache_sb(sb);
	fsync_dev(dev);

	if (sb == current->fs->root->d_sb && !unmount_root) {
		/*
		 * Special case for "unmounting" root ...
		 * we just try to remount it readonly.
		 */
		retval = 0;
		if (!(sb->s_flags & MS_RDONLY))
			retval = do_remount_sb(sb, MS_RDONLY, 0);
		return ERR_PTR(retval);
	}

	retval = d_umount(sb);
	if (retval)
		goto out;

	if (sb->s_op) {
		if (sb->s_op->write_super && sb->s_dirt)
			sb->s_op->write_super(sb);
	}

	lock_super(sb);
	if (sb->s_op) {
		if (sb->s_op->put_super)
			sb->s_op->put_super(sb);
	}

	/* Forget any remaining inodes */
	if (invalidate_inodes(sb)) {
		printk("VFS: Busy inodes after unmount. "
			"Self-destruct in 5 seconds.  Have a nice day...\n");
	}

	sb->s_dev = 0;		/* Free the superblock */
	bdev = sb->s_bdev;
	sb->s_bdev = NULL;
	put_filesystem(sb->s_type);
	sb->s_type = NULL;
	unlock_super(sb);

	remove_vfsmnt(dev);

	return bdev;

out:
	return ERR_PTR(retval);
}

static int umount_dev(kdev_t dev, int flags)
{
	int retval;
	struct block_device *bdev;

	retval = -ENXIO;
	if (MAJOR(dev) >= MAX_BLKDEV)
		goto out;

	fsync_dev(dev);

	down(&mount_sem);

	bdev = do_umount(dev, 0, flags);
	if (IS_ERR(bdev))
		retval = PTR_ERR(bdev);
	else {
		retval = 0;
		if (bdev) {
			blkdev_put(bdev, BDEV_FS);
			bdput(bdev);
		} else {
			put_unnamed_dev(dev);
		}
	}
	up(&mount_sem);
out:
	return retval;
}

/*
 * Now umount can handle mount points as well as block devices.
 * This is important for filesystems which use unnamed block devices.
 *
 * We now support a flag for forced unmount like the other 'big iron'
 * unixes. Our API is identical to OSF/1 to avoid making a mess of AMD
 */

asmlinkage long sys_umount(char * name, int flags)
{
	struct dentry * dentry;
	int retval;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	lock_kernel();
	dentry = namei(name);
	retval = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		struct inode * inode = dentry->d_inode;
		kdev_t dev = inode->i_rdev;

		retval = 0;		
		if (S_ISBLK(inode->i_mode)) {
			if (IS_NODEV(inode))
				retval = -EACCES;
		} else {
			struct super_block *sb = inode->i_sb;
			retval = -EINVAL;
			if (sb && inode == sb->s_root->d_inode) {
				dev = sb->s_dev;
				retval = 0;
			}
		}
		dput(dentry);

		if (!retval)
			retval = umount_dev(dev, flags);
	}
	unlock_kernel();
	return retval;
}

/*
 *	The 2.0 compatible umount. No flags. 
 */
 
asmlinkage long sys_oldumount(char * name)
{
	return sys_umount(name,0);
}

/*
 * Check whether we can mount the specified device.
 */
int fs_may_mount(kdev_t dev)
{
	struct super_block * sb = get_super(dev);
	int busy;

	busy = sb && sb->s_root &&
	       (sb->s_root->d_count != 1 || sb->s_root->d_covers != sb->s_root);
	return !busy;
}

/*
 * do_mount() does the actual mounting after sys_mount has done the ugly
 * parameter parsing. When enough time has gone by, and everything uses the
 * new mount() parameters, sys_mount() can then be cleaned up.
 *
 * We cannot mount a filesystem if it has active, used, or dirty inodes.
 * We also have to flush all inode-data for this device, as the new mount
 * might need new info.
 *
 * [21-Mar-97] T.Schoebel-Theuer: Now this can be overridden when
 * supplying a leading "!" before the dir_name, allowing "stacks" of
 * mounted filesystems. The stacking will only influence any pathname lookups
 * _after_ the mount, but open file descriptors or working directories that
 * are now covered remain valid. For example, when you overmount /home, any
 * process with old cwd /home/joe will continue to use the old versions,
 * as long as relative paths are used, but absolute paths like /home/joe/xxx
 * will go to the new "top of stack" version. In general, crossing a
 * mount point will always go to the top of stack element.
 * Anyone using this new feature must know what he/she is doing.
 */

int do_mount(struct block_device *bdev, const char *dev_name,
	     const char *dir_name, const char * type, int flags, void * data)
{
	kdev_t dev;
	struct dentry * dir_d;
	struct super_block * sb;
	struct vfsmount *vfsmnt;
	struct file_system_type *fs_type;
	int error;

	if (bdev) {
		mode_t mode = FMODE_READ; /* we always need it ;-) */
		if (!(flags & MS_RDONLY))
			mode |= FMODE_WRITE;
		dev = to_kdev_t(bdev->bd_dev);
		error = blkdev_get(bdev, mode, 0, BDEV_FS);
		if (error)
			return error;
	} else {
		dev = get_unnamed_dev();
		if (!dev)
			return -EMFILE;	/* huh? */
	}

	error = -EACCES;
	if (!(flags & MS_RDONLY) && dev && is_read_only(dev))
		goto out;

	/*
	 * Do the lookup first to force automounting.
	 */
	dir_d = namei(dir_name);
	error = PTR_ERR(dir_d);
	if (IS_ERR(dir_d))
		goto out;

	down(&mount_sem);
	error = -ENOTDIR;
	if (!S_ISDIR(dir_d->d_inode->i_mode))
		goto dput_and_out;

	error = -EBUSY;
	if (dir_d->d_covers != dir_d)
		goto dput_and_out;

	error = -EINVAL;
	if (!dev)
		goto dput_and_out;
	check_disk_change(dev);
	sb = get_super(dev);
	if (sb) {
		/* Already mounted */
		error = -EBUSY;
		goto dput_and_out;
	}

	fs_type = get_fs_type(type);
	if (!fs_type) {
		printk("VFS: on device %s: get_fs_type(%s) failed\n",
		       kdevname(dev), type);
		goto dput_and_out;
	}

	sb = read_super(dev, bdev, fs_type, flags, data, 0);
	if (!sb)
		goto fsput_and_out;

	/*
	 * We may have slept while reading the super block, 
	 * so we check afterwards whether it's safe to mount.
	 */
	error = -EBUSY;
	if (!fs_may_mount(dev))
		goto bdput_and_out;

	error = -ENOMEM;
	vfsmnt = add_vfsmnt(sb, dev_name, dir_name);
	if (vfsmnt) {
		d_mount(dget(dir_d), sb->s_root);
		dput(dir_d);
		up(&mount_sem);
		return 0;
	}

bdput_and_out:
	/* FIXME: ->put_super() is needed here */
	sb->s_bdev = NULL;
	sb->s_dev = 0;
	sb->s_type = NULL;
	if (bdev)
		bdput(bdev);
fsput_and_out:
	put_filesystem(fs_type);
dput_and_out:
	dput(dir_d);
	up(&mount_sem);
out:
	if (bdev)
		blkdev_put(bdev, BDEV_FS);
	else
		put_unnamed_dev(dev);
	return error;
}


/*
 * Alters the mount flags of a mounted file system. Only the mount point
 * is used as a reference - file system type and the device are ignored.
 */

static int do_remount_sb(struct super_block *sb, int flags, char *data)
{
	int retval;
	
	if (!(flags & MS_RDONLY) && sb->s_dev && is_read_only(sb->s_dev))
		return -EACCES;
		/*flags |= MS_RDONLY;*/
	/* If we are remounting RDONLY, make sure there are no rw files open */
	if ((flags & MS_RDONLY) && !(sb->s_flags & MS_RDONLY))
		if (!fs_may_remount_ro(sb))
			return -EBUSY;
	if (sb->s_op && sb->s_op->remount_fs) {
		lock_super(sb);
		retval = sb->s_op->remount_fs(sb, &flags, data);
		unlock_super(sb);
		if (retval)
			return retval;
	}
	sb->s_flags = (sb->s_flags & ~MS_RMT_MASK) | (flags & MS_RMT_MASK);

	/*
	 * Invalidate the inodes, as some mount options may be changed.
	 * N.B. If we are changing media, we should check the return
	 * from invalidate_inodes ... can't allow _any_ open files.
	 */
	invalidate_inodes(sb);

	return 0;
}

static int do_remount(const char *dir,int flags,char *data)
{
	struct dentry *dentry;
	int retval;

	dentry = namei(dir);
	retval = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		struct super_block * sb = dentry->d_inode->i_sb;

		retval = -ENODEV;
		if (sb) {
			retval = -EINVAL;
			if (dentry == sb->s_root) {
				/*
				 * Shrink the dcache and sync the device.
				 */
				shrink_dcache_sb(sb);
				fsync_dev(sb->s_dev);
				if (flags & MS_RDONLY)
					acct_auto_close(sb->s_dev);
				retval = do_remount_sb(sb, flags, data);
			}
		}
		dput(dentry);
	}
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

	vma = find_vma(current->mm, (unsigned long) data);
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
	if (copy_from_user((void *) page,data,i)) {
		free_page(page); 
		return -EFAULT;
	}
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
 * have to have a special 16-bit magic number in the high word:
 * 0xC0ED. If this magic word isn't present, the flags and data info
 * aren't used, as the syscall assumes we are talking to an older
 * version that didn't understand them.
 */
long do_sys_mount(char * dev_name, char * dir_name, unsigned long type_page,
		  unsigned long new_flags, unsigned long data_page)
{
	struct file_system_type * fstype;
	struct dentry * dentry = NULL;
	struct inode * inode = NULL;
	struct block_device *bdev = NULL;
	int retval;
	unsigned long flags = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if ((new_flags &
	     (MS_MGC_MSK | MS_REMOUNT)) == (MS_MGC_VAL | MS_REMOUNT)) {
		retval = do_remount(dir_name,
				    new_flags & ~MS_MGC_MSK & ~MS_REMOUNT,
				    (char *) data_page);
		goto out;
	}

	fstype = get_fs_type((char *) type_page);
	retval = -ENODEV;
	if (!fstype)		
		goto out;

	if (fstype->fs_flags & FS_REQUIRES_DEV) {
		struct block_device_operations *bdops;

		dentry = namei(dev_name);
		retval = PTR_ERR(dentry);
		if (IS_ERR(dentry))
			goto fs_out;

		inode = dentry->d_inode;
		retval = -ENOTBLK;
		if (!S_ISBLK(inode->i_mode))
			goto dput_and_out;

		retval = -EACCES;
		if (IS_NODEV(inode))
			goto dput_and_out;

		bdev = inode->i_bdev;
		bdops = devfs_get_ops ( devfs_get_handle_from_inode (inode) );
		if (bdops) bdev->bd_op = bdops;
	}

	if ((new_flags & MS_MGC_MSK) == MS_MGC_VAL)
		flags = new_flags & ~MS_MGC_MSK;

	retval = do_mount(bdev, dev_name, dir_name, fstype->name, flags,
				(void *) data_page);

dput_and_out:
	dput(dentry);
fs_out:
	put_filesystem(fstype);
out:
	return retval;
}

asmlinkage long sys_mount(char * dev_name, char * dir_name, char * type,
			  unsigned long new_flags, void * data)
{
	int retval;
	unsigned long data_page = 0;
	unsigned long type_page = 0;

	lock_kernel();
	retval = copy_mount_options (type, &type_page);
	if (retval < 0)
		goto out;

	/* copy_mount_options allows a NULL user pointer,
	 * and just returns zero in that case.  But if we
	 * allow the type to be NULL we will crash.
	 * Previously we did not check this case.
	 */
	if (type_page == 0) {
		retval = -EINVAL;
		goto out;
	}

	retval = copy_mount_options (data, &data_page);
	if (retval >= 0) {
		retval = do_sys_mount(dev_name, dir_name, type_page,
				      new_flags, data_page);
		free_page(data_page);
	}
	free_page(type_page);
out:
	unlock_kernel();
	return retval;
}

void __init mount_root(void)
{
	struct file_system_type * fs_type;
	struct super_block * sb;
	struct vfsmount *vfsmnt;
	struct block_device *bdev = NULL;
	mode_t mode;
	int retval;
	void *handle;
	char path[64];
	int path_start = -1;

#ifdef CONFIG_ROOT_NFS
	if (MAJOR(ROOT_DEV) == UNNAMED_MAJOR) {
		ROOT_DEV = 0;
		if ((fs_type = get_fs_type("nfs"))) {
			sb = get_empty_super(); /* "can't fail" */
			sb->s_dev = get_unnamed_dev();
			sb->s_bdev = NULL;
			sb->s_flags = root_mountflags;
			sema_init(&sb->s_vfs_rename_sem,1);
			sema_init(&sb->s_nfsd_free_path_sem,1);
			vfsmnt = add_vfsmnt(sb, "/dev/root", "/");
			if (vfsmnt) {
				if (nfs_root_mount(sb) >= 0) {
					sb->s_dirt = 0;
					sb->s_type = fs_type;
					current->fs->root = dget(sb->s_root);
					current->fs->pwd = dget(sb->s_root);
					ROOT_DEV = sb->s_dev;
			                printk (KERN_NOTICE "VFS: Mounted root (NFS filesystem)%s.\n", (sb->s_flags & MS_RDONLY) ? " readonly" : "");
					return;
				}
				remove_vfsmnt(sb->s_dev);
			}
			put_unnamed_dev(sb->s_dev);
			sb->s_dev = 0;
			put_filesystem(fs_type);
		}
		if (!ROOT_DEV) {
			printk(KERN_ERR "VFS: Unable to mount root fs via NFS, trying floppy.\n");
			ROOT_DEV = MKDEV(FLOPPY_MAJOR, 0);
		}
	}
#endif

#ifdef CONFIG_BLK_DEV_FD
	if (MAJOR(ROOT_DEV) == FLOPPY_MAJOR) {
#ifdef CONFIG_BLK_DEV_RAM
		extern int rd_doload;
		extern void rd_load_secondary(void);
#endif
		floppy_eject();
#ifndef CONFIG_BLK_DEV_RAM
		printk(KERN_NOTICE "(Warning, this kernel has no ramdisk support)\n");
#else
		/* rd_doload is 2 for a dual initrd/ramload setup */
		if(rd_doload==2)
			rd_load_secondary();
		else
#endif
		{
			printk(KERN_NOTICE "VFS: Insert root floppy and press ENTER\n");
			wait_for_keypress();
		}
	}
#endif

	devfs_make_root (root_device_name);
	handle = devfs_find_handle (NULL, ROOT_DEVICE_NAME, 0,
	                            MAJOR (ROOT_DEV), MINOR (ROOT_DEV),
				    DEVFS_SPECIAL_BLK, 1);
	if (handle)  /*  Sigh: bd*() functions only paper over the cracks  */
	{
	    unsigned major, minor;

	    devfs_get_maj_min (handle, &major, &minor);
	    ROOT_DEV = MKDEV (major, minor);
	}

	/*
	 * Probably pure paranoia, but I'm less than happy about delving into
	 * devfs crap and checking it right now. Later.
	 */
	if (!ROOT_DEV)
		panic("I have no root and I want to scream");

	bdev = bdget(kdev_t_to_nr(ROOT_DEV));
	if (!bdev)
		panic(__FUNCTION__ ": unable to allocate root device");
	bdev->bd_op = devfs_get_ops (handle);
	path_start = devfs_generate_path (handle, path + 5, sizeof (path) - 5);
	mode = FMODE_READ;
	if (!(root_mountflags & MS_RDONLY))
		mode |= FMODE_WRITE;
	retval = blkdev_get(bdev, mode, 0, BDEV_FS);
	if (retval == -EROFS) {
		root_mountflags |= MS_RDONLY;
		retval = blkdev_get(bdev, FMODE_READ, 0, BDEV_FS);
	}
	if (retval) {
	        /*
		 * Allow the user to distinguish between failed open
		 * and bad superblock on root device.
		 */
		printk ("VFS: Cannot open root device \"%s\" or %s\n",
			root_device_name, kdevname (ROOT_DEV));
		printk ("Please append a correct \"root=\" boot option\n");
		panic("VFS: Unable to mount root fs on %s",
			kdevname(ROOT_DEV));
	}

	check_disk_change(ROOT_DEV);

	spin_lock(&file_systems_lock);
	for (fs_type = file_systems ; fs_type ; fs_type = fs_type->next) {
  		if (!(fs_type->fs_flags & FS_REQUIRES_DEV))
  			continue;
		if (!try_inc_mod_count(fs_type->owner))
			continue;
		spin_unlock(&file_systems_lock);
		sb = get_super(ROOT_DEV);
		if (sb) {
			/* Shouldn't we fail here? Oh, well... */
			sb->s_bdev = bdev;
			goto mount_it;
		}
  		sb = read_super(ROOT_DEV,bdev,fs_type,root_mountflags,NULL,1);
		if (sb) 
			goto mount_it;
		spin_lock(&file_systems_lock);
		put_filesystem(fs_type);
	}
	spin_unlock(&file_systems_lock);
	panic("VFS: Unable to mount root fs on %s",
		kdevname(ROOT_DEV));

mount_it:
	sb->s_flags = root_mountflags;
	current->fs->root = dget(sb->s_root);
	current->fs->pwd = dget(sb->s_root);
	printk ("VFS: Mounted root (%s filesystem)%s.\n",
		fs_type->name,
		(sb->s_flags & MS_RDONLY) ? " readonly" : "");
	if (path_start >= 0) {
		devfs_mk_symlink (NULL,
				  "root", 0, DEVFS_FL_DEFAULT,
				  path + 5 + path_start, 0,
				  NULL, NULL);
		memcpy (path + path_start, "/dev/", 5);
		vfsmnt = add_vfsmnt (sb, path + path_start,
				     "/");
	}
	else vfsmnt = add_vfsmnt (sb, "/dev/root", "/");
	if (vfsmnt) {
		bdput(bdev); /* sb holds a reference */
		return;
	}
	panic("VFS: add_vfsmnt failed for root fs");
}


static void chroot_fs_refs(struct dentry *old_root,
    struct dentry *new_root)
{
	struct task_struct *p;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if (!p->fs) continue;
		if (p->fs->root == old_root) {
			dput(old_root);
			p->fs->root = dget(new_root);
			printk(KERN_DEBUG "chroot_fs_refs: changed root of "
			    "process %d\n",p->pid);
		}
		if (p->fs->pwd == old_root) {
			dput(old_root);
			p->fs->pwd = dget(new_root);
			printk(KERN_DEBUG "chroot_fs_refs: changed cwd of "
			    "process %d\n",p->pid);
		}
	}
	read_unlock(&tasklist_lock);
}


/*
 * Moves the current root to put_root, and sets root/cwd of all processes
 * which had them on the old root to new_root.
 *
 * Note:
 *  - we don't move root/cwd if they are not at the root (reason: if something
 *    cared enough to change them, it's probably wrong to force them elsewhere)
 *  - it's okay to pick a root that isn't the root of a file system, e.g.
 *    /nfs/my_root where /nfs is the mount point. Better avoid creating
 *    unreachable mount points this way, though.
 */

asmlinkage long sys_pivot_root(const char *new_root, const char *put_old)
{
	struct dentry *root = current->fs->root;
	struct dentry *d_new_root, *d_put_old, *covered;
	struct dentry *root_dev_root, *new_root_dev_root;
	struct dentry *walk, *next;
	int error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	lock_kernel();
	d_new_root = namei(new_root);
	if (IS_ERR(d_new_root)) {
		error = PTR_ERR(d_new_root);
		goto out0;
	}
	d_put_old = namei(put_old);
	if (IS_ERR(d_put_old)) {
		error = PTR_ERR(d_put_old);
		goto out1;
	}
	down(&mount_sem);
	if (!d_new_root->d_inode || !d_put_old->d_inode) {
		error = -ENOENT;
		goto out2;
	}
	if (!S_ISDIR(d_new_root->d_inode->i_mode) ||
	    !S_ISDIR(d_put_old->d_inode->i_mode)) {
		error = -ENOTDIR;
		goto out2;
	}
	error = -EBUSY;
	if (d_new_root->d_sb == root->d_sb || d_put_old->d_sb == root->d_sb)
		goto out2; /* loop */
	if (d_put_old != d_put_old->d_covers)
		goto out2; /* mount point is busy */
	error = -EINVAL;
	walk = d_put_old; /* make sure we can reach put_old from new_root */
	for (;;) {
		next = walk->d_covers->d_parent;
		if (next == walk)
			goto out2;
		if (next == d_new_root)
			break;
		walk = next;
	}

	new_root_dev_root = d_new_root->d_sb->s_root;
	covered = new_root_dev_root->d_covers;
	new_root_dev_root->d_covers = new_root_dev_root;
	dput(covered);
	covered->d_mounts = covered;

	root_dev_root = root->d_sb->s_root;
	root_dev_root->d_covers = dget(d_put_old);
	d_put_old->d_mounts = root_dev_root;
	chroot_fs_refs(root,d_new_root);
	error = 0;
out2:
	up(&mount_sem);
	dput(d_put_old);
out1:
	dput(d_new_root);
out0:
	unlock_kernel();
	return error;
}


#ifdef CONFIG_BLK_DEV_INITRD

int __init change_root(kdev_t new_root_dev,const char *put_old)
{
	kdev_t old_root_dev;
	struct vfsmount *vfsmnt;
	struct dentry *old_root,*old_pwd,*dir_d = NULL;
	int error;

	old_root = current->fs->root;
	old_pwd = current->fs->pwd;
	old_root_dev = ROOT_DEV;
	if (!fs_may_mount(new_root_dev)) {
		printk(KERN_CRIT "New root is busy. Staying in initrd.\n");
		return -EBUSY;
	}
	/*  First unmount devfs if mounted  */
	dir_d = lookup_dentry ("/dev", NULL, 1);
	if (!IS_ERR(dir_d)) {
		struct super_block *sb = dir_d->d_inode->i_sb;

		if (sb && (dir_d->d_inode == sb->s_root->d_inode) &&
		    (sb->s_magic == DEVFS_SUPER_MAGIC)) {
			dput (dir_d);
			do_umount (sb->s_dev, 0, 0);
		}
		else dput (dir_d);
	}
	ROOT_DEV = new_root_dev;
	mount_root();
	dput(old_root);
	dput(old_pwd);
#if 1
	shrink_dcache();
	printk("change_root: old root has d_count=%d\n", old_root->d_count);
#endif
	mount_devfs_fs ();
	/*
	 * Get the new mount directory
	 */
	dir_d = lookup_dentry(put_old, NULL, 1);
	if (IS_ERR(dir_d)) {
		error = PTR_ERR(dir_d);
	} else if (!dir_d->d_inode) {
		dput(dir_d);
		error = -ENOENT;
	} else {
		error = 0;
	}
	if (!error && dir_d->d_covers != dir_d) {
		dput(dir_d);
		error = -EBUSY;
	}
	if (!error && !S_ISDIR(dir_d->d_inode->i_mode)) {
		dput(dir_d);
		error = -ENOTDIR;
	}
	if (error) {
		struct block_device *bdev;

		printk(KERN_NOTICE "Trying to unmount old root ... ");
		bdev = do_umount(old_root_dev,1, 0);
		if (!IS_ERR(bdev)) {
			printk("okay\n");
			/* special: the old device driver is going to be
			   a ramdisk and the point of this call is to free its
			   protected memory (even if dirty). */
			destroy_buffers(old_root_dev);
			if (bdev) {
				blkdev_put(bdev, BDEV_FS);
				bdput(bdev);
			}
			return 0;
		}
		printk(KERN_ERR "error %ld\n",PTR_ERR(bdev));
		return error;
	}
	remove_vfsmnt(old_root_dev);
	vfsmnt = add_vfsmnt(old_root->d_sb, "/dev/root.old", put_old);
	if (vfsmnt) {
		d_mount(dir_d,old_root);
		return 0;
	}
	printk(KERN_CRIT "Trouble: add_vfsmnt failed\n");
	return -ENOMEM;
}

#endif

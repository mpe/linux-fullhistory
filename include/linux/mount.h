/*
 *
 * Definitions for mount interface. This describes the in the kernel build 
 * linkedlist with mounted filesystems.
 *
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 *
 * Version: $Id: mount.h,v 2.0 1996/11/17 16:48:14 mvw Exp mvw $
 *
 */
#ifndef _LINUX_MOUNT_H
#define _LINUX_MOUNT_H
#ifdef __KERNEL__

struct vfsmount
{
	struct dentry *mnt_mountpoint;	/* dentry of mountpoint */
	struct dentry *mnt_root;	/* root of the mounted tree */
	struct vfsmount *mnt_parent;	/* fs we are mounted on */
	struct list_head mnt_instances;	/* other vfsmounts of the same fs */
	struct list_head mnt_clash;	/* those who are mounted on (other */
					/* instances) of the same dentry */
	struct super_block *mnt_sb;	/* pointer to superblock */
	atomic_t mnt_count;

  kdev_t mnt_dev;			/* Device this applies to */
  char *mnt_devname;			/* Name of device e.g. /dev/dsk/hda1 */
  char *mnt_dirname;			/* Name of directory mounted on */
	struct list_head mnt_list;
};

static inline struct vfsmount *mntget(struct vfsmount *mnt)
{
	if (mnt)
		atomic_inc(&mnt->mnt_count);
	return mnt;
}

static inline void mntput(struct vfsmount *mnt)
{
	if (mnt) {
		if (atomic_dec_and_test(&mnt->mnt_count))
			BUG();
	}
}

#endif
#endif /* _LINUX_MOUNT_H */

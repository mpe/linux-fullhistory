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

struct vfsmount
{
  kdev_t mnt_dev;			/* Device this applies to */
  char *mnt_devname;			/* Name of device e.g. /dev/dsk/hda1 */
  char *mnt_dirname;			/* Name of directory mounted on */
  struct super_block *mnt_sb;		/* pointer to superblock */
	struct list_head mnt_list;
};

/* MOUNT_REWRITE: fill these */
static inline struct vfsmount *mntget(struct vfsmount *mnt)
{
	return mnt;
}

static inline void mntput(struct vfsmount *mnt)
{
}

#endif /* _LINUX_MOUNT_H */

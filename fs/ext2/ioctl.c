/*
 * linux/fs/ext2/ioctl.c
 *
 * Copyright (C) 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 */

#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/mm.h>

int ext2_ioctl (struct inode * inode, struct file * filp, unsigned int cmd,
		unsigned long arg)
{
	int err;
	unsigned long flags;

	ext2_debug ("cmd = %u, arg = %lu\n", cmd, arg);

	switch (cmd) {
	case EXT2_IOC_GETFLAGS:
		err = verify_area(VERIFY_WRITE, (int *) arg, sizeof(int));
		if (err)
			return err;
		put_user(inode->u.ext2_i.i_flags, (int *) arg);
		return 0;
	case EXT2_IOC_SETFLAGS:
		err = verify_area(VERIFY_READ, (int *) arg, sizeof(int));
		if (err)
			return err;
		flags = get_user((int *) arg);
		/*
		 * The IMMUTABLE and APPEND_ONLY flags can only be changed by
		 * the super user when the security level is zero.
		 */
		if ((flags & (EXT2_APPEND_FL | EXT2_IMMUTABLE_FL)) ^
		    (inode->u.ext2_i.i_flags &
		     (EXT2_APPEND_FL | EXT2_IMMUTABLE_FL))) {
			/* This test looks nicer. Thanks to Pauline Middelink */
			if (!fsuser() || securelevel > 0)
				return -EPERM;
		} else
			if ((current->fsuid != inode->i_uid) && !fsuser())
				return -EPERM;
		if (IS_RDONLY(inode))
			return -EROFS;
		inode->u.ext2_i.i_flags = flags;
		if (flags & EXT2_APPEND_FL)
			inode->i_flags |= S_APPEND;
		else
			inode->i_flags &= ~S_APPEND;
		if (flags & EXT2_IMMUTABLE_FL)
			inode->i_flags |= S_IMMUTABLE;
		else
			inode->i_flags &= ~S_IMMUTABLE;
		inode->i_ctime = CURRENT_TIME;
		inode->i_dirt = 1;
		return 0;
	case EXT2_IOC_GETVERSION:
		err = verify_area(VERIFY_WRITE, (int *) arg, sizeof(int));
		if (err)
			return err;
		put_user(inode->u.ext2_i.i_version, (int *) arg);
		return 0;
	case EXT2_IOC_SETVERSION:
		if ((current->fsuid != inode->i_uid) && !fsuser())
			return -EPERM;
		if (IS_RDONLY(inode))
			return -EROFS;
		err = verify_area(VERIFY_READ, (int *) arg, sizeof(int));
		if (err)
			return err;
		inode->u.ext2_i.i_version = get_user((int *) arg);
		inode->i_ctime = CURRENT_TIME;
		inode->i_dirt = 1;
		return 0;
	default:
		return -ENOTTY;
	}
}

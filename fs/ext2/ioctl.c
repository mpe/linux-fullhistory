/*
 * linux/fs/ext2/ioctl.c
 *
 * Copyright (C) 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/mm.h>

int ext2_ioctl (struct inode * inode, struct file * filp, unsigned int cmd,
		unsigned long arg)
{
	unsigned int flags;

	ext2_debug ("cmd = %u, arg = %lu\n", cmd, arg);

	switch (cmd) {
	case EXT2_IOC_GETFLAGS:
		flags = inode->u.ext2_i.i_flags & EXT2_FL_USER_VISIBLE;
		return put_user(inode->u.ext2_i.i_flags, (int *) arg);
	case EXT2_IOC_SETFLAGS:
		if (get_user(flags, (int *) arg))
			return -EFAULT;
		flags = flags & EXT2_FL_USER_MODIFIABLE;
		/*
		 * The IMMUTABLE and APPEND_ONLY flags can only be changed by
		 * the super user when the security level is zero.
		 */
		if ((flags & (EXT2_APPEND_FL | EXT2_IMMUTABLE_FL)) ^
		    (inode->u.ext2_i.i_flags &
		     (EXT2_APPEND_FL | EXT2_IMMUTABLE_FL))) {
			/* This test looks nicer. Thanks to Pauline Middelink */
			if (!capable(CAP_LINUX_IMMUTABLE))
				return -EPERM;
		} else
			if ((current->fsuid != inode->i_uid) && 
			    !capable(CAP_FOWNER))
				return -EPERM;
		if (IS_RDONLY(inode))
			return -EROFS;
		inode->u.ext2_i.i_flags = (inode->u.ext2_i.i_flags &
					   ~EXT2_FL_USER_MODIFIABLE) | flags;
		if (flags & EXT2_SYNC_FL)
			inode->i_flags |= MS_SYNCHRONOUS;
		else
			inode->i_flags &= ~MS_SYNCHRONOUS;
		if (flags & EXT2_APPEND_FL)
			inode->i_flags |= S_APPEND;
		else
			inode->i_flags &= ~S_APPEND;
		if (flags & EXT2_IMMUTABLE_FL)
			inode->i_flags |= S_IMMUTABLE;
		else
			inode->i_flags &= ~S_IMMUTABLE;
		if (flags & EXT2_NOATIME_FL)
			inode->i_flags |= MS_NOATIME;
		else
			inode->i_flags &= ~MS_NOATIME;
		inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty(inode);
		return 0;
	case EXT2_IOC_GETVERSION:
		return put_user(inode->u.ext2_i.i_version, (int *) arg);
	case EXT2_IOC_SETVERSION:
		if ((current->fsuid != inode->i_uid) && !capable(CAP_FOWNER))
			return -EPERM;
		if (IS_RDONLY(inode))
			return -EROFS;
		if (get_user(inode->u.ext2_i.i_version, (int *) arg))
			return -EFAULT;	
		inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty(inode);
		return 0;
	default:
		return -ENOTTY;
	}
}

/*
 *  linux/fs/attr.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  changes by Thomas Schoebel-Theuer
 */

#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <asm/system.h>

/* Taken over from the old code... */

/* POSIX UID/GID verification for setting inode attributes. */
int inode_change_ok(struct inode *inode, struct iattr *attr)
{
	/* If force is set do it anyway. */
	if (attr->ia_valid & ATTR_FORCE)
		return 0;

	/* Make sure a caller can chown. */
	if ((attr->ia_valid & ATTR_UID) &&
	    (current->fsuid != inode->i_uid ||
	     attr->ia_uid != inode->i_uid) && !fsuser())
		return -EPERM;

	/* Make sure caller can chgrp. */
	if ((attr->ia_valid & ATTR_GID) &&
	    (!in_group_p(attr->ia_gid) && attr->ia_gid != inode->i_gid) &&
	    !fsuser())
		return -EPERM;

	/* Make sure a caller can chmod. */
	if (attr->ia_valid & ATTR_MODE) {
		if ((current->fsuid != inode->i_uid) && !fsuser())
			return -EPERM;
		/* Also check the setgid bit! */
		if (!fsuser() && !in_group_p((attr->ia_valid & ATTR_GID) ? attr->ia_gid :
					     inode->i_gid))
			attr->ia_mode &= ~S_ISGID;
	}

	/* Check for setting the inode time. */
	if ((attr->ia_valid & ATTR_ATIME_SET) &&
	    ((current->fsuid != inode->i_uid) && !fsuser()))
		return -EPERM;
	if ((attr->ia_valid & ATTR_MTIME_SET) &&
	    ((current->fsuid != inode->i_uid) && !fsuser()))
		return -EPERM;
	return 0;
}

void inode_setattr(struct inode * inode, struct iattr * attr)
{
	if(attr->ia_valid &
	   (ATTR_UID|ATTR_GID|ATTR_SIZE|ATTR_ATIME|ATTR_MTIME|ATTR_CTIME|ATTR_MODE)) {
		if (attr->ia_valid & ATTR_UID)
			inode->i_uid = attr->ia_uid;
		if (attr->ia_valid & ATTR_GID)
			inode->i_gid = attr->ia_gid;
		if (attr->ia_valid & ATTR_SIZE)
			inode->i_size = attr->ia_size;
		if (attr->ia_valid & ATTR_ATIME)
			inode->i_atime = attr->ia_atime;
		if (attr->ia_valid & ATTR_MTIME)
			inode->i_mtime = attr->ia_mtime;
		if (attr->ia_valid & ATTR_CTIME)
			inode->i_ctime = attr->ia_ctime;
		if (attr->ia_valid & ATTR_MODE) {
			inode->i_mode = attr->ia_mode;
			if (!fsuser() && !in_group_p(inode->i_gid))
				inode->i_mode &= ~S_ISGID;
		}
		inode->i_dirt = 1;
	}
}

int notify_change(struct inode * inode, struct iattr * attr)
{
	int error;
	time_t now = CURRENT_TIME;

	attr->ia_ctime = now;
	if ((attr->ia_valid & (ATTR_ATIME | ATTR_ATIME_SET)) == ATTR_ATIME)
		attr->ia_atime = now;
	if ((attr->ia_valid & (ATTR_MTIME | ATTR_MTIME_SET)) == ATTR_MTIME)
		attr->ia_mtime = now;
	attr->ia_valid &= ~(ATTR_CTIME);
	if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->notify_change) 
		return inode->i_sb->s_op->notify_change(inode, attr);
	error = inode_change_ok(inode, attr);
	if(!error)
		inode_setattr(inode, attr);
	return error;
}


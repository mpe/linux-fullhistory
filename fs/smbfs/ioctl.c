/*
 *  ioctl.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/smb_fs.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/uaccess.h>

int
smb_ioctl(struct inode *inode, struct file *filp,
	  unsigned int cmd, unsigned long arg)
{
	switch (cmd)
	{
	case SMB_IOC_GETMOUNTUID:
		return put_user(SMB_SERVER(inode)->m.mounted_uid, (uid_t *) arg);

	default:
		return -EINVAL;
	}
}

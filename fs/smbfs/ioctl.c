/*
 *  ioctl.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
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
		return put_user(SMB_SERVER(inode)->m.mounted_uid,
				(uid_t *) arg);

	case SMB_IOC_NEWCONN:
	{
		struct smb_conn_opt opt;
		int result;

		if (arg == 0)
		{
			/* The process offers a new connection upon SIGUSR1 */
			return smb_offerconn(SMB_SERVER(inode));
		}

		if ((result = verify_area(VERIFY_READ, (uid_t *) arg,
					  sizeof(opt))) != 0)
		{
			return result;
		}
		copy_from_user(&opt, (void *)arg, sizeof(opt));

		return smb_newconn(SMB_SERVER(inode), &opt);
	}
	default:
		return -EINVAL;
	}
}

/*
 *  ioctl.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <linux/smb_fs.h>
#include <linux/smb_mount.h>

#include <asm/uaccess.h>

int
smb_ioctl(struct inode *inode, struct file *filp,
	  unsigned int cmd, unsigned long arg)
{
	struct smb_sb_info *server = SMB_SERVER(inode);
	int result = -EINVAL;

	switch (cmd)
	{
	case SMB_IOC_GETMOUNTUID:
		result = put_user(server->mnt->mounted_uid, (uid_t *) arg);
		break;

	case SMB_IOC_NEWCONN:
	{
		struct smb_conn_opt opt;

		if (arg)
		{
			result = -EFAULT;
			if (!copy_from_user(&opt, (void *)arg, sizeof(opt)))
				result = smb_newconn(server, &opt);
		}
		else
		{
#if 0
			/* obsolete option ... print a warning */
			printk("SMBFS: ioctl deprecated, please upgrade "
				"smbfs package\n");
#endif
			result = 0;
		}
		break;
	}
	default:
	}
	return result;
}

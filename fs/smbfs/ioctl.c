/*
 *  ioctl.c
 *
 *  Copyright (C) 1995 by Volker Lendecke
 *
 */

#include <asm/segment.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/smb_fs.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/mm.h>

int
smb_ioctl (struct inode * inode, struct file * filp,
           unsigned int cmd, unsigned long arg)
{
	int result;

	switch (cmd) {
        case SMB_IOC_GETMOUNTUID:
                if ((result = verify_area(VERIFY_WRITE, (uid_t*) arg,
                                          sizeof(uid_t))) != 0) {
                        return result;
                }
                put_fs_word(SMB_SERVER(inode)->m.mounted_uid, (uid_t*) arg);
                return 0;
        default:
		return -EINVAL;
	}
}

/*
 *	A Non implementation of disk quotas. Chainsawed from dquot.c by
 *	Alan Cox <alan@lxorguk.ukuu.org.uk>. This saves us memory without
 *	having zillions of #ifdefs (Or if it had been done right one
 *	
 *	QUOTA_OP(inode,func)
 *
 *	macro.)
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/mount.h>

#include <asm/segment.h>

#ifndef min
#define min(a,b) ((a) < (b)) ? (a) : (b)
#endif

int sync_dquots(kdev_t dev, short type)
{
	return(0);
}

/*
 * Trash the cache for a certain type on a device.
 */

void invalidate_dquots(kdev_t dev, short type)
{
}

/*
 * Initialize pointer in a inode to the right dquots.
 */
void dquot_initialize(struct inode *inode, short type)
{
}

void dquot_drop(struct inode *inode)
{
}

void dquot_init(void)
{
}

/*
 * Turn quota off on a device. type == -1 ==> quotaoff for all types (umount)
 */

int quota_off(kdev_t dev, short type)
{
	return(0);
}

int quota_on(kdev_t dev, short type, char *path)
{
	return(-ENOPKG);
}

/*
 * Ok this is the systemcall interface, this communicates with
 * the userlevel programs. Currently this only supports diskquota
 * calls. Maybe we need to add the process quotas etc in the future.
 * But we probably better use rlimits for that.
 */
asmlinkage int sys_quotactl(int cmd, const char *special, int id, caddr_t addr)
{
	return(-ENOPKG);
}

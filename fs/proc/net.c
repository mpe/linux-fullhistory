/*
 *  linux/fs/proc/net.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  gjh 3/'93 heim@peanuts.informatik.uni-tuebingen.de (Gerald J. Heim)
 *            most of this file is stolen from base.c
 *            it works, but you shouldn't use it as a guideline
 *            for new proc-fs entries. once i'll make it better.
 * fvk 3/'93  waltje@uwalt.nl.mugnet.org (Fred N. van Kempen)
 *	      cleaned up the whole thing, moved "net" specific code to
 *	      the NET kernel layer (where it belonged in the first place).
 * Michael K. Johnson (johnsonm@stolaf.edu) 3/93
 *            Added support from my previous inet.c.  Cleaned things up
 *            quite a bit, modularized the code.
 * fvk 4/'93  waltje@uwalt.nl.mugnet.org (Fred N. van Kempen)
 *	      Renamed "route_get_info()" to "rt_get_info()" for consistency.
 * Alan Cox (gw4pts@gw4pts.ampr.org) 4/94
 *	      Dusted off the code and added IPX. Fixed the 4K limit.
 * Erik Schoenfelder (schoenfr@ibr.cs.tu-bs.de)
 *	      /proc/net/snmp.
 * Alan Cox (gw4pts@gw4pts.ampr.org) 1/95
 *	      Added AppleTalk slots
 *
 *  proc net directory handling functions
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/mm.h>

#include <asm/uaccess.h>

#define PROC_BLOCK_SIZE	(3*1024)		/* 4K page size but our output routines use some slack for overruns */

static long proc_readnet(struct inode * inode, struct file * file,
			char * buf, unsigned long count)
{
	char * page;
	int bytes=count;
	int copied=0;
	char *start;
	struct proc_dir_entry * dp;

	if (count < 0)
		return -EINVAL;
	dp = (struct proc_dir_entry *) inode->u.generic_ip;
	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	while (bytes>0)
	{
		int length, thistime=bytes;
		if (bytes > PROC_BLOCK_SIZE)
			thistime=PROC_BLOCK_SIZE;

		length = dp->get_info(page, &start,
				      file->f_pos,
				      thistime,
				      (file->f_flags & O_ACCMODE) == O_RDWR);

		/*
 		 *	We have been given a non page aligned block of
		 *	the data we asked for + a bit. We have been given
 		 *	the start pointer and we know the length.. 
		 */

		if (length <= 0)
			break;
		/*
 		 *	Copy the bytes
		 */
		copy_to_user(buf+copied, start, length);
		file->f_pos += length;	/* Move down the file */
		bytes  -= length;
		copied += length;
		if (length<thistime)
			break;	/* End of file */
	}
	free_page((unsigned long) page);
	return copied;
}

static struct file_operations proc_net_operations = {
	NULL,			/* lseek - default */
	proc_readnet,		/* read - bad */
	NULL,			/* write - bad */
	NULL,			/* readdir */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* flush */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

/*
 * proc directories can do almost nothing..
 */
struct inode_operations proc_net_inode_operations = {
	&proc_net_operations,	/* default net file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

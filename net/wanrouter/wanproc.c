/*****************************************************************************
* wanproc.c	WAN Multiprotocol Router Module. proc filesystem interface.
*
*		This module is completely hardware-independent and provides
*		access to the router using Linux /proc filesystem.
*
* Author:	Gene Kozin	<genek@compuserve.com>
*
* Copyright:	(c) 1995-1996 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Dec 13, 1996	Gene Kozin	Initial version (based on Sangoma's WANPIPE)
* Jan 30, 1997	Alan Cox	Hacked around for 2.1
*****************************************************************************/

#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/kernel.h>
#include <linux/malloc.h>	/* kmalloc(), kfree() */
#include <linux/mm.h>		/* verify_area(), etc. */
#include <linux/string.h>	/* inline mem*, str* functions */
#include <asm/segment.h>	/* kernel <-> user copy */
#include <asm/byteorder.h>	/* htons(), etc. */
#include <asm/uaccess.h>	/* copy_to_user */
#include <linux/wanrouter.h>	/* WAN router API definitions */


/****** Defines and Macros **************************************************/

#ifndef	min
#define min(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef	max
#define max(a,b) (((a)>(b))?(a):(b))
#endif

#define	ROUTER_PAGE_SZ	4000	/* buffer size for printing proc info */

/****** Data Types **********************************************************/

typedef struct wan_stat_entry
{
	struct wan_stat_entry *	next;
	char *description;		/* description string */
	void *data;			/* -> data */
	unsigned data_type;		/* data type */
} wan_stat_entry_t;

/****** Function Prototypes *************************************************/

/* Proc filesystem interface */
static int router_proc_perms (struct inode*, int);
static long router_proc_read(struct inode* inode, struct file* file, char* buf,
				unsigned long count);

/* Methods for preparing data for reading proc entries */

static int about_get_info(char* buf, char** start, off_t offs, int len, int dummy);
static int config_get_info(char* buf, char** start, off_t offs, int len, int dummy);
static int status_get_info(char* buf, char** start, off_t offs, int len, int dummy);
static int wandev_get_info(char* buf, char** start, off_t offs, int len, int dummy);

/* Miscellaneous */

/*
 *	Global Data
 */

/*
 *	Names of the proc directory entries 
 */

static char name_root[]	= ROUTER_NAME;
static char name_info[]	= "about";
static char name_conf[]	= "config";
static char name_stat[]	= "status";

/*
 *	Structures for interfacing with the /proc filesystem.
 *	Router creates its own directory /proc/net/router with the folowing
 *	entries:
 *	About		general information (version, copyright, etc.)
 *	Conf		device configuration
 *	Stat		global device statistics
 *	<device>	entry for each WAN device
 */

/*
 *	Generic /proc/net/router/<file> file and inode operations 
 */

static struct file_operations router_fops =
{
	NULL,			/* lseek   */
	router_proc_read,	/* read	   */
	NULL,			/* write   */
	NULL,			/* readdir */
	NULL,			/* select  */
	NULL,			/* ioctl   */
	NULL,			/* mmap	   */
	NULL,			/* no special open code	   */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

static struct inode_operations router_inode =
{
	&router_fops,
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
	router_proc_perms
};

/*
 *	/proc/net/router/<device> file and inode operations
 */

static struct file_operations wandev_fops =
{
	NULL,			/* lseek   */
	router_proc_read,	/* read	   */
	NULL,			/* write   */
	NULL,			/* readdir */
	NULL,			/* select  */
	wanrouter_ioctl,	/* ioctl   */
	NULL,			/* mmap	   */
	NULL,			/* no special open code	   */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

static struct inode_operations wandev_inode =
{
	&wandev_fops,
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
	router_proc_perms
};

/*
 * Proc filesystem derectory entries.
 */

/*
 *	/proc/net/router 
 */
 
static struct proc_dir_entry proc_router =
{
	0,			/* .low_ino */
	sizeof(name_root) - 1,	/* .namelen */
	name_root,		/* .name */
	0555 | S_IFDIR,		/* .mode */
	2,			/* .nlink */
	0,			/* .uid */
	0,			/* .gid */
	0,			/* .size */
	&proc_dir_inode_operations, /* .ops */
	NULL,			/* .get_info */
	NULL,			/* .fill_node */
	NULL,			/* .next */
	NULL,			/* .parent */
	NULL,			/* .subdir */
	NULL,			/* .data */
};

/*
 *	/proc/net/router/about 
 */
 
static struct proc_dir_entry proc_router_info =
{
	0,			/* .low_ino */
	sizeof(name_info) - 1,	/* .namelen */
	name_info,		/* .name */
	0444 | S_IFREG,		/* .mode */
	1,			/* .nlink */
	0,			/* .uid */
	0,			/* .gid */
	0,			/* .size */
	&router_inode,		/* .ops */
	&about_get_info,	/* .get_info */
	NULL,			/* .fill_node */
	NULL,			/* .next */
	NULL,			/* .parent */
	NULL,			/* .subdir */
	NULL,			/* .data */
};

/*
 *	/proc/net/router/config 
 */
 
static struct proc_dir_entry proc_router_conf =
{
	0,			/* .low_ino */
	sizeof(name_conf) - 1,	/* .namelen */
	name_conf,		/* .name */
	0444 | S_IFREG,		/* .mode */
	1,			/* .nlink */
	0,			/* .uid */
	0,			/* .gid */
	0,			/* .size */
	&router_inode,		/* .ops */
	&config_get_info,	/* .get_info */
	NULL,			/* .fill_node */
	NULL,			/* .next */
	NULL,			/* .parent */
	NULL,			/* .subdir */
	NULL,			/* .data */
};

/*
 *	/proc/net/router/status 
 */
 
static struct proc_dir_entry proc_router_stat =
{
	0,			/* .low_ino */
	sizeof(name_stat) - 1,	/* .namelen */
	name_stat,		/* .name */
	0444 | S_IFREG,		/* .mode */
	1,			/* .nlink */
	0,			/* .uid */
	0,			/* .gid */
	0,			/* .size */
	&router_inode,		/* .ops */
	status_get_info,	/* .get_info */
	NULL,			/* .fill_node */
	NULL,			/* .next */
	NULL,			/* .parent */
	NULL,			/* .subdir */
	NULL,			/* .data */
};

/*
 *	Interface functions
 */

/*
 *	Initialize router proc interface.
 */

int wanrouter_proc_init (void)
{
	int err = proc_register_dynamic(&proc_net, &proc_router);

	if (!err)
	{
		proc_register_dynamic(&proc_router, &proc_router_info);
		proc_register_dynamic(&proc_router, &proc_router_conf);
		proc_register_dynamic(&proc_router, &proc_router_stat);
	}
	return err;
}

/*
 *	Clean up router proc interface.
 */

void wanrouter_proc_cleanup (void)
{
	proc_unregister(&proc_router, proc_router_info.low_ino);
	proc_unregister(&proc_router, proc_router_conf.low_ino);
	proc_unregister(&proc_router, proc_router_stat.low_ino);
	proc_unregister(&proc_net, proc_router.low_ino);
}

/*
 *	Add directory entry for WAN device.
 */

int wanrouter_proc_add (wan_device_t* wandev)
{
	if (wandev->magic != ROUTER_MAGIC)
		return -EINVAL;
		
	memset(&wandev->dent, 0, sizeof(wandev->dent));
	wandev->dent.namelen	= strlen(wandev->name);
	wandev->dent.name	= wandev->name;
	wandev->dent.mode	= 0444 | S_IFREG;
	wandev->dent.nlink	= 1;
	wandev->dent.ops	= &wandev_inode;
	wandev->dent.get_info	= &wandev_get_info;
	wandev->dent.data	= wandev;
	return proc_register_dynamic(&proc_router, &wandev->dent);
}

/*
 *	Delete directory entry for WAN device.
 */
 
int wanrouter_proc_delete(wan_device_t* wandev)
{
	if (wandev->magic != ROUTER_MAGIC)
		return -EINVAL;
	proc_unregister(&proc_router, wandev->dent.low_ino);
	return 0;
}

/****** Proc filesystem entry points ****************************************/

/*
 *	Verify access rights.
 */

static int router_proc_perms (struct inode* inode, int op)
{
	return 0;
}

/*
 *	Read router proc directory entry.
 *	This is universal routine for reading all entries in /proc/net/router
 *	directory.  Each directory entry contains a pointer to the 'method' for
 *	preparing data for that entry.
 *	o verify arguments
 *	o allocate kernel buffer
 *	o call get_info() to prepare data
 *	o copy data to user space
 *	o release kernel buffer
 *
 *	Return:	number of bytes copied to user space (0, if no data)
 *		<0	error
 */

static long router_proc_read(struct inode* inode, struct file* file, 
				char* buf, unsigned long count)
{
	struct proc_dir_entry* dent;
	char* page;
	int pos, offs, len;

	if (count <= 0)
		return 0;
		
	dent = inode->u.generic_ip;
	if ((dent == NULL) || (dent->get_info == NULL))
		return 0;
		
	page = kmalloc(ROUTER_PAGE_SZ, GFP_KERNEL);
	if (page == NULL)
		return -ENOBUFS;
		
	pos = dent->get_info(page, dent->data, 0, 0, 0);
	offs = file->f_pos;
	if (offs < pos)
	{
		len = min(pos - offs, count);
		if(copy_to_user(buf, (page + offs), len))
			return -EFAULT;
		file->f_pos += len;
	}
	else
		len = 0;
	kfree(page);
	return len;
}

/*
 *	Prepare data for reading 'About' entry.
 *	Return length of data.
 */

static int about_get_info(char* buf, char** start, off_t offs, int len, 
				int dummy)
{
	int cnt = 0;

	cnt += sprintf(&buf[cnt], "%12s : %u.%u\n",
		"version", ROUTER_VERSION, ROUTER_RELEASE);
	return cnt;
}

/*
 *	Prepare data for reading 'Config' entry.
 *	Return length of data.
 *	NOT YET IMPLEMENTED
 */

static int config_get_info(char* buf, char** start, off_t offs, int len, 
	int dummy)
{
	int cnt = 0;

	cnt += sprintf(&buf[cnt], "%12s : %u.%u\n",
		"version", ROUTER_VERSION, ROUTER_RELEASE);
	return cnt;
}

/*
 *	Prepare data for reading 'Status' entry.
 *	Return length of data.
 *	NOT YET IMPLEMENTED
 */

static int status_get_info(char* buf, char** start, off_t offs, int len, 
			int dummy)
{
	int cnt = 0;

	cnt += sprintf(&buf[cnt], "%12s : %u.%u\n",
		"version", ROUTER_VERSION, ROUTER_RELEASE);
	return cnt;
}

/*
 *	Prepare data for reading <device> entry.
 *	Return length of data.
 *
 *	On entry, the 'start' argument will contain a pointer to WAN device
 *	data space.
 */

static int wandev_get_info(char* buf, char** start, off_t offs, int len, 
			int dummy)
{
	wan_device_t* wandev = (void*)start;
	int cnt = 0;

	if ((wandev == NULL) || (wandev->magic != ROUTER_MAGIC))
		return 0;
	cnt += sprintf(&buf[cnt], "%12s : %s\n", "name", wandev->name);
	return cnt;
}

/*
 *	End
 */
 

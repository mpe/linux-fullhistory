/*****************************************************************************
* wanproc.c	WAN Router Module. /proc filesystem interface.
*
*		This module is completely hardware-independent and provides
*		access to the router using Linux /proc filesystem.
*
* Author:	Gene Kozin	<genek@compuserve.com>
*
* Copyright:	(c) 1995-1997 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Jun 29, 1997	Alan Cox	Merged with 1.0.3 vendor code
* Jan 29, 1997	Gene Kozin	v1.0.1. Implemented /proc read routines
* Jan 30, 1997	Alan Cox	Hacked around for 2.1
* Dec 13, 1996	Gene Kozin	Initial version (based on Sangoma's WANPIPE)
*****************************************************************************/

#include <linux/config.h>
#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/kernel.h>
#include <linux/malloc.h>	/* kmalloc(), kfree() */
#include <linux/mm.h>		/* verify_area(), etc. */
#include <linux/string.h>	/* inline mem*, str* functions */
#include <linux/init.h>		/* __initfunc et al. */
#include <asm/segment.h>	/* kernel <-> user copy */
#include <asm/byteorder.h>	/* htons(), etc. */
#include <asm/uaccess.h>	/* copy_to_user */
#include <asm/io.h>
#include <linux/wanrouter.h>	/* WAN router API definitions */


/****** Defines and Macros **************************************************/

#ifndef	min
#define min(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef	max
#define max(a,b) (((a)>(b))?(a):(b))
#endif

#define	PROC_BUFSZ	4000	/* buffer size for printing proc info */

/****** Data Types **********************************************************/

typedef struct wan_stat_entry
{
	struct wan_stat_entry *	next;
	char *description;		/* description string */
	void *data;			/* -> data */
	unsigned data_type;		/* data type */
} wan_stat_entry_t;

/****** Function Prototypes *************************************************/

#ifdef CONFIG_PROC_FS

/* Proc filesystem interface */
static int router_proc_perms(struct inode *, int);
static ssize_t router_proc_read(struct file* file, char* buf, size_t count, 					loff_t *ppos);

/* Methods for preparing data for reading proc entries */

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
static char name_conf[]	= "config";
static char name_stat[]	= "status";

/*
 *	Structures for interfacing with the /proc filesystem.
 *	Router creates its own directory /proc/net/router with the folowing
 *	entries:
 *	config		device configuration
 *	status		global device statistics
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
	NULL,			/* flush */
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
	NULL,			/* follow link */
	NULL,			/* readlink */
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
	NULL,			/* flush */
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

/* Strings */
static char conf_hdr[] =
	"Device name    | port |IRQ|DMA| mem.addr |mem.size|"
	"option1|option2|option3|option4\n";
	
static char stat_hdr[] =
	"Device name    |station|interface|clocking|baud rate| MTU |ndev"
	"|link state\n";


/*
 *	Interface functions
 */

/*
 *	Initialize router proc interface.
 */

__initfunc(int wanrouter_proc_init (void))
{
	int err = proc_register(proc_net, &proc_router);

	if (!err)
	{
		proc_register(&proc_router, &proc_router_conf);
		proc_register(&proc_router, &proc_router_stat);
	}
	return err;
}

/*
 *	Clean up router proc interface.
 */

void wanrouter_proc_cleanup (void)
{
	proc_unregister(&proc_router, proc_router_conf.low_ino);
	proc_unregister(&proc_router, proc_router_stat.low_ino);
	proc_unregister(proc_net, proc_router.low_ino);
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
	return proc_register(&proc_router, &wandev->dent);
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

static ssize_t router_proc_read(struct file* file, char* buf, size_t count,
				loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct proc_dir_entry* dent;
	char* page;
	int pos, offs, len;

	if (count <= 0)
		return 0;
		
	dent = inode->u.generic_ip;
	if ((dent == NULL) || (dent->get_info == NULL))
		return 0;
		
	page = kmalloc(PROC_BUFSZ, GFP_KERNEL);
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
 *	Prepare data for reading 'Config' entry.
 *	Return length of data.
 */

static int config_get_info(char* buf, char** start, off_t offs, int len, 
	int dummy)
{
	int cnt = sizeof(conf_hdr) - 1;
	wan_device_t* wandev;
	strcpy(buf, conf_hdr);
	for (wandev = router_devlist;
	     wandev && (cnt < (PROC_BUFSZ - 120));
	     wandev = wandev->next)
	{
		if (wandev->state) cnt += sprintf(&buf[cnt],
			"%-15s|0x%-4X|%3u|%3u| 0x%-8lX |0x%-6X|%7u|%7u|%7u|%7u\n",
			wandev->name,
			wandev->ioport,
			wandev->irq,
			wandev->dma,
			virt_to_phys(wandev->maddr),
			wandev->msize,
			wandev->hw_opt[0],
			wandev->hw_opt[1],
			wandev->hw_opt[2],
			wandev->hw_opt[3]);
	}

	return cnt;
}

/*
 *	Prepare data for reading 'Status' entry.
 *	Return length of data.
 */

static int status_get_info(char* buf, char** start, off_t offs, int len, 
			int dummy)
{
	int cnt = sizeof(stat_hdr) - 1;
	wan_device_t* wandev;
	strcpy(buf, stat_hdr);
	for (wandev = router_devlist;
	     wandev && (cnt < (PROC_BUFSZ - 80));
	     wandev = wandev->next)
	{
		if (!wandev->state) continue;
		cnt += sprintf(&buf[cnt],
			"%-15s|%-7s|%-9s|%-8s|%9u|%5u|%3u |",
			wandev->name,
			wandev->station ? " DCE" : " DTE",
			wandev->interface ? " V.35" : " RS-232",
			wandev->clocking ? "internal" : "external",
			wandev->bps,
			wandev->mtu,
			wandev->ndev)
		;
		switch (wandev->state)
		{
		case WAN_UNCONFIGURED:
			cnt += sprintf(&buf[cnt], "%-12s\n", "unconfigured");
			break;

		case WAN_DISCONNECTED:
			cnt += sprintf(&buf[cnt], "%-12s\n", "disconnected");
			break;

		case WAN_CONNECTING:
			cnt += sprintf(&buf[cnt], "%-12s\n", "connecting");
			break;

		case WAN_CONNECTED:
			cnt += sprintf(&buf[cnt], "%-12s\n", "connected");
			break;

		default:
			cnt += sprintf(&buf[cnt], "%-12s\n", "invalid");
			break;
		}
	}
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
	if (!wandev->state)
		return sprintf(&buf[cnt], "device is not configured!\n")
	;

	/* Update device statistics */
	if (wandev->update) wandev->update(wandev);

	cnt += sprintf(&buf[cnt], "%30s: %12lu\n",
		"total frames received", wandev->stats.rx_packets)
	;
	cnt += sprintf(&buf[cnt], "%30s: %12lu\n",
		"receiver overrun errors", wandev->stats.rx_over_errors)
	;
	cnt += sprintf(&buf[cnt], "%30s: %12lu\n",
		"CRC errors", wandev->stats.rx_crc_errors)
	;
	cnt += sprintf(&buf[cnt], "%30s: %12lu\n",
		"frame length errors", wandev->stats.rx_length_errors)
	;
	cnt += sprintf(&buf[cnt], "%30s: %12lu\n",
		"frame format errors", wandev->stats.rx_frame_errors)
	;
	cnt += sprintf(&buf[cnt], "%30s: %12lu\n",
		"aborted frames received", wandev->stats.rx_missed_errors)
	;
	cnt += sprintf(&buf[cnt], "%30s: %12lu\n",
		"reveived frames dropped", wandev->stats.rx_dropped)
	;
	cnt += sprintf(&buf[cnt], "%30s: %12lu\n",
		"other receive errors", wandev->stats.rx_errors)
	;
	cnt += sprintf(&buf[cnt], "\n%30s: %12lu\n",
		"total frames transmitted", wandev->stats.tx_packets)
	;
	cnt += sprintf(&buf[cnt], "%30s: %12lu\n",
		"aborted frames transmitted", wandev->stats.tx_aborted_errors)
	;
	cnt += sprintf(&buf[cnt], "%30s: %12lu\n",
		"transmit frames dropped", wandev->stats.tx_dropped)
	;
	cnt += sprintf(&buf[cnt], "%30s: %12lu\n",
		"transmit collisions", wandev->stats.collisions)
	;
	cnt += sprintf(&buf[cnt], "%30s: %12lu\n",
		"other transmit errors", wandev->stats.tx_errors)
	;
	return cnt;
}

/*
 *	End
 */
 
#else

/*
 *	No /proc - output stubs
 */
 
__initfunc(int wanrouter_proc_init(void))
{
	return 0;
}

void wanrouter_proc_cleanup(void)
{
	return;
}

int wanrouter_proc_add(wan_device_t *wandev)
{
	return 0;
}

int wanrouter_proc_delete(wan_device_t *wandev)
{
	return 0;
}

#endif

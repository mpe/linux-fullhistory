/*
 * udf_fs.h
 *
 * Included by fs/filesystems.c
 *
 * CONTACTS
 *	E-mail regarding any portion of the Linux UDF file system should be
 *	directed to the development team mailing list (run by majordomo):
 *		linux_udf@hootie.lvld.hp.com
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 * HISTORY
 *	July 21, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 *
 * 10/2/98 dgb	rearranged all headers
 * 11/26/98 bf  added byte order macros
 * 12/5/98 dgb  removed other includes to reduce kernel namespace pollution.
 *		This should only be included by the kernel now!
 */
#if !defined(_LINUX_UDF_FS_H)
#define _LINUX_UDF_FS_H

#define UDF_PREALLOCATE
#define UDF_DEFAULT_PREALLOC_BLOCKS		8
#define UDF_DEFAULT_PREALLOC_DIR_BLOCKS	0

#define UDFFS_DATE		"99/09/02"
#define UDFFS_VERSION	"0.8.9"
#define UDFFS_DEBUG

#ifdef UDFFS_DEBUG
#define udf_debug(f, a...) \
	{ \
		printk (KERN_DEBUG "UDF-fs DEBUG (%s, %d): %s: ", \
			__FILE__, __LINE__, __FUNCTION__); \
		printk (## f, ## a); \
	}
#else
#define udf_debug(f, a...) /**/
#endif

#define udf_info(f, a...) \
		printk (KERN_INFO "UDF-fs INFO " ## f, ## a);

struct udf_addr
{
	__u32 block;
	__u16 partition;
	unsigned error : 1;
	unsigned reserved : 15;
};
	

/* Prototype for fs/filesystem.c (the only thing really required in this file) */
extern int init_udf_fs(void);

#endif /* !defined(_LINUX_UDF_FS_H) */

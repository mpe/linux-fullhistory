/*
 * udf_fs.h
 *
 * PURPOSE
 *  Included by fs/filesystems.c
 *
 * DESCRIPTION
 *  OSTA-UDF(tm) = Optical Storage Technology Association
 *  Universal Disk Format.
 *
 *  This code is based on version 2.00 of the UDF specification,
 *  and revision 3 of the ECMA 167 standard [equivalent to ISO 13346].
 *    http://www.osta.org/ *    http://www.ecma.ch/
 *    http://www.iso.org/
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
 *  (C) 1999-2000 Ben Fennema
 *  (C) 1999-2000 Stelias Computing Inc
 *
 * HISTORY
 *
 * 10/02/98 dgb	rearranged all headers
 * 11/26/98 blf	added byte order macros
 * 12/05/98 dgb	removed other includes to reduce kernel namespace pollution.
 *		This should only be included by the kernel now!
 */

#if !defined(_LINUX_UDF_FS_H)
#define _LINUX_UDF_FS_H

#define UDF_PREALLOCATE
#define UDF_DEFAULT_PREALLOC_BLOCKS		8
#define UDF_DEFAULT_PREALLOC_DIR_BLOCKS	0

#define UDFFS_DATE		"2000/01/17"
#define UDFFS_VERSION	"0.9.0"
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

/* Prototype for fs/filesystem.c (the only thing really required in this file) */
extern int init_udf_fs(void);

#endif /* !defined(_LINUX_UDF_FS_H) */

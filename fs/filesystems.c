/*
 *  linux/fs/filesystems.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  table of configured filesystems
 */

#include <linux/config.h>
#include <linux/fs.h>
#ifdef MINIX_FS
#include <linux/minix_fs.h>
#endif
#ifdef PROC_FS
#include <linux/proc_fs.h>
#endif
#ifdef EXT_FS
#include <linux/ext_fs.h>
#endif
#ifdef MSDOS_FS
#include <linux/msdos_fs.h>
#endif
#ifdef NFS_FS
#include <linux/nfs_fs.h>
#endif
#ifdef ISO9660_FS
#include <linux/iso_fs.h>
#endif

struct file_system_type file_systems[] = {
#ifdef MINIX_FS
	{minix_read_super,	"minix",	1},
#endif
#ifdef EXT_FS
	{ext_read_super,	"ext",		1},
#endif
#ifdef MSDOS_FS
	{msdos_read_super,	"msdos",	1},
#endif
#ifdef PROC_FS
	{proc_read_super,	"proc",		0},
#endif
#ifdef NFS_FS
	{nfs_read_super,	"nfs",		0},
#endif
#ifdef ISO9660_FS
	{isofs_read_super,	"iso9660",	1},
#endif
	{NULL,			NULL,		0}
};

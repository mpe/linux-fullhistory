/* $Id: stat.h,v 1.8 1998/02/06 12:52:07 jj Exp $ */
#ifndef _SPARC_STAT_H
#define _SPARC_STAT_H

#include <linux/types.h>

struct __old_kernel_stat {
	unsigned short st_dev;
	unsigned short st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned long  st_size;
	unsigned long  st_atime;
	unsigned long  st_mtime;
	unsigned long  st_ctime;
};

struct stat {
	dev_t   st_dev;
	ino_t   st_ino;
	mode_t  st_mode;
	short   st_nlink;
	uid_t   st_uid;
	gid_t   st_gid;
	dev_t   st_rdev;
	off_t   st_size;
	time_t  st_atime;
	unsigned long  __unused1;
	time_t  st_mtime;
	unsigned long  __unused2;
	time_t  st_ctime;
	unsigned long  __unused3;
	off_t   st_blksize;
	off_t   st_blocks;
	unsigned long  __unused4[2];
};

typedef struct {
	__u32		major;
	__u32		minor;
} __new_dev_t;

struct stat64 {
	__new_dev_t	st_dev;
	__u64		st_ino;
	__u32		st_mode;
	__u32		st_nlink;
	__s32		st_uid;
	__s32		st_gid;
	__new_dev_t	st_rdev;
	__s64		st_size;
	__u64		st_blocks;
	__s32		st_atime;
	__u32		__unused1;
	__s32		st_mtime;
	__u32		__unused2;
	__s32		st_ctime;
	__u32		__unused3;
	__u32		st_blksize;
	__u32		__unused4;
};

#define __XSTAT_VER_1		1
#define __XSTAT_VER_2		2
#define __XSTAT_VER_MASK	0xff

#define __XSTAT_VER_XSTAT	0x000
#define __XSTAT_VER_LXSTAT	0x100
#define __XSTAT_VER_FXSTAT	0x200
#define __XSTAT_VER_TYPEMASK	0xff00

#define __XMKNOD_VER_1		1

#endif

/* $Id: stat.h,v 1.4 1998/02/06 12:52:14 jj Exp $ */
#ifndef _SPARC64_STAT_H
#define _SPARC64_STAT_H

#include <linux/types.h>
#include <linux/time.h>

struct stat32 {
	__kernel_dev_t32   st_dev;
	__kernel_ino_t32   st_ino;
	__kernel_mode_t32  st_mode;
	short   	   st_nlink;
	__kernel_uid_t32   st_uid;
	__kernel_gid_t32   st_gid;
	__kernel_dev_t32   st_rdev;
	__kernel_off_t32   st_size;
	__kernel_time_t32  st_atime;
	unsigned int       __unused1;
	__kernel_time_t32  st_mtime;
	unsigned int       __unused2;
	__kernel_time_t32  st_ctime;
	unsigned int       __unused3;
	__kernel_off_t32   st_blksize;
	__kernel_off_t32   st_blocks;
	unsigned int  __unused4[2];
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
	time_t  st_mtime;
	time_t  st_ctime;
	off_t   st_blksize;
	off_t   st_blocks;
	unsigned long  __unused4[2];
};

typedef __u64	__new_dev_t;

struct stat64 {
	__new_dev_t	st_dev;
	__u64		st_ino;
	__u32		st_mode;
	__u32		st_nlink;
	__s32		st_uid;
	__s32		st_gid;
	__new_dev_t	st_rdev;
	__s64		st_size;
	struct timespec	st_atim;
	struct timespec	st_mtim;
	struct timespec	st_ctim;
	int		st_blksize;
	long		st_blocks;
	char		st_fstype[16];
};

struct stat64_32 {
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

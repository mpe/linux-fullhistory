/* $Id: stat.h,v 1.10 1999/12/21 14:09:41 jj Exp $ */
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

struct stat64 {
	unsigned char	__pad0[6];
	unsigned short	st_dev;
	unsigned char	__pad1[4];

	unsigned int	st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;

	unsigned int	st_uid;
	unsigned int	st_gid;

	unsigned char	__pad2[6];
	unsigned short	st_rdev;

	unsigned char	__pad3[8];

	long long	st_size;
	unsigned int	st_blksize;

	unsigned char	__pad4[8];
	unsigned int	st_blocks;

	unsigned int	st_atime;
	unsigned int	__unused1;

	unsigned int	st_mtime;
	unsigned int	__unused2;

	unsigned int	st_ctime;
	unsigned int	__unused3;

	unsigned int	__unused4;
	unsigned int	__unused5;
};

#endif

/* $Id: stat.h,v 1.1 1996/12/26 14:22:31 davem Exp $ */
#ifndef _SPARC64_STAT_H
#define _SPARC64_STAT_H

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

/* XXX Fix this for full backwards 32-bit compat. */
struct stat32 {
	dev_t   st_dev;
	ino_t   st_ino;
	mode_t  st_mode;
	short   st_nlink;
	uid_t   st_uid;
	gid_t   st_gid;
	dev_t   st_rdev;
	off_t   st_size;
	time_t  st_atime;
	unsigned int  __unused1;
	time_t  st_mtime;
	unsigned int  __unused2;
	time_t  st_ctime;
	unsigned int  __unused3;
	off_t   st_blksize;
	off_t   st_blocks;
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
	unsigned long  __unused1;
	time_t  st_mtime;
	unsigned long  __unused2;
	time_t  st_ctime;
	unsigned long  __unused3;
	off_t   st_blksize;
	off_t   st_blocks;
	unsigned long  __unused4[2];
};

#endif

#ifndef _PPC_STAT_H
#define _PPC_STAT_H

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
	dev_t		st_dev;
	ino_t		st_ino;
	mode_t		st_mode;
	nlink_t		st_nlink;
	uid_t 		st_uid;
	gid_t 		st_gid;
	dev_t		st_rdev;
	off_t		st_size;
	unsigned long  	st_blksize;
	unsigned long  	st_blocks;
	unsigned long  	st_atime;
	unsigned long  	__unused1;
	unsigned long  	st_mtime;
	unsigned long  	__unused2;
	unsigned long  	st_ctime;
	unsigned long  	__unused3;
	unsigned long  	__unused4;
	unsigned long  	__unused5;
};

#endif

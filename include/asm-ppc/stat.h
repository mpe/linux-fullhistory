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

/* This matches struct stat64 in glibc2.1, hence the absolutely
 * insane amounts of padding around dev_t's.
 */
struct stat64 {
	unsigned short	st_dev;
	unsigned char	__pad0[10];

	unsigned long	st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;

	unsigned long	st_uid;
	unsigned long	st_gid;

	unsigned short	st_rdev;
	unsigned char	__pad3[10];

	long long	st_size;
	unsigned long	st_blksize;

	unsigned long	st_blocks;	/* Number 512-byte blocks allocated. */
	unsigned long	__pad4;		/* future possible st_blocks high bits */

	unsigned long	st_atime;
	unsigned long	__pad5;

	unsigned long	st_mtime;
	unsigned long	__pad6;

	unsigned long	st_ctime;
	unsigned long	__pad7;		/* will be high 32 bits of ctime someday */

	unsigned long	__unused1;
	unsigned long	__unused2;
};
#endif

#ifndef __ASM_MIPS_STAT_H
#define __ASM_MIPS_STAT_H

#include <linux/types.h>

struct old_stat {
	unsigned int	st_dev;
	unsigned int	st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;
	unsigned int	st_uid;
	unsigned int	st_gid;
	unsigned int	st_rdev;
	long		st_size;
	unsigned int	st_atime, st_res1;
	unsigned int	st_mtime, st_res2;
	unsigned int	st_ctime, st_res3;
	unsigned int	st_blksize;
	int		st_blocks;
	unsigned int	st_flags;
	unsigned int	st_gen;
};

struct new_stat {
	dev_t		st_dev;
	long		st_pad1[3];		/* Reserved for network id */
	ino_t		st_ino;
	mode_t		st_mode;
	nlink_t		st_nlink;
	uid_t		st_uid;
	gid_t		st_gid;
	dev_t		st_rdev;
	long		st_pad2[2];
	off_t		st_size;
	long		st_pad3;
	/*
	 * Actually this should be timestruc_t st_atime, st_mtime and st_ctime
	 * but we don't have it under Linux.
	 */
	time_t		st_atime;
	long		reserved0;
	time_t		st_mtime;
	long		reserved1;
	time_t		st_ctime;
	long		reserved2;
	long		st_blksize;
	long		st_blocks;
	char		st_fstype[16];	/* Filesystem type name */
	long		st_pad4[8];
	/* Linux specific fields */
	unsigned int	st_flags;
	unsigned int	st_gen;
};

#endif /* __ASM_MIPS_STAT_H */

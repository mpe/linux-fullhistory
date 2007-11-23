/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1999 Ralf Baechle
 */
#ifndef _ASM_STAT_H
#define _ASM_STAT_H

#include <linux/types.h>

struct __old_kernel_stat {
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

struct stat32 {
	__kernel_dev_t32    st_dev;
	int		    st_pad1[3];
	__kernel_ino_t32    st_ino;
	__kernel_mode_t32   st_mode;
	__kernel_nlink_t32  st_nlink;
	__kernel_uid_t32    st_uid;
	__kernel_gid_t32    st_gid;
	__kernel_dev_t32    st_rdev;
	int		    st_pad2[2];
	__kernel_off_t32    st_size;
	int		    st_pad3;
	__kernel_time_t32   st_atime;
	int		    reserved0;
	__kernel_time_t32   st_mtime;
	int		    reserved1;
	__kernel_time_t32   st_ctime;
	int		    reserved2;
	int		    st_blksize;
	int		    st_blocks;
	char		    st_fstype[16];	/* Filesystem type name */
	int		    st_pad4[8];
	unsigned int	    st_flags;
	unsigned int	    st_gen;
};

struct stat {
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

#endif /* _ASM_STAT_H */

#ifndef _ALPHA_STATFS_H
#define _ALPHA_STATFS_H

#ifndef __KERNEL_STRICT_NAMES

#include <linux/posix_types.h>

typedef __kernel_fsid_t	fsid_t;

#endif

/*
 * The OSF/1 statfs structure is much larger, but this should
 * match the beginning, at least.
 */
struct statfs {
	short	f_type;
	short	f_flags;
	int	f_fsize;
	int	f_bsize;
	int	f_blocks;
	int	f_bfree;
	int	f_bavail;
	int	f_files;
	int	f_ffree;
	__kernel_fsid_t f_fsid;
	/* linux-specific entries start here.. */
	int	f_namelen;
};

#endif

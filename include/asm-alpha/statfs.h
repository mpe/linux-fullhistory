#ifndef _ALPHA_STATFS_H
#define _ALPHA_STATFS_H

typedef struct {
	int	val[2];
} fsid_t;

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
	fsid_t	f_fsid;
	/* linux-specific entries start here.. */
	int	f_namelen;
};

#endif

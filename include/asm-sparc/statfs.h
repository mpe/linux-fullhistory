/* $Id: statfs.h,v 1.3 1995/11/25 02:32:54 davem Exp $ */
#ifndef _SPARC_STATFS_H
#define _SPARC_STATFS_H

typedef struct {
	long    val[2];
} fsid_t;

struct statfs {
	long f_type;
	long f_bsize;
	long f_blocks;
	long f_bfree;
	long f_bavail;
	long f_files;
	long f_ffree;
	fsid_t f_fsid;
	long f_namelen;  /* SunOS ignores this field. */
	long f_spare[6];
};

#endif

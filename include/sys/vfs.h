#ifndef _SYS_VFS_H_
#define _SYS_VFS_H_

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
	long f_spare[7];
};

#endif

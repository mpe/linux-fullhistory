#ifndef __ASM_MIPS_STATFS_H
#define __ASM_MIPS_STATFS_H

typedef struct {
	long    val[2];
} fsid_t;

struct statfs {
	long	f_type;
#define f_fstyp f_type
	long	f_bsize;
	long	f_frsize;	/* Fragment size - unsupported */
	long	f_blocks;
	long	f_bfree;
	long	f_files;
	long	f_ffree;

	/* Linux specials */
	long	f_bavail;
	fsid_t	f_fsid;
	long	f_namelen;
	long	f_spare[6];
};

#endif /* __ASM_MIPS_STATFS_H */

#ifndef _M68K_STAT_H
#define _M68K_STAT_H

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
	unsigned short st_dev;
	unsigned short __pad1;
	unsigned long st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned short __pad2;
	unsigned long  st_size;
	unsigned long  st_blksize;
	unsigned long  st_blocks;
	unsigned long  st_atime;
	unsigned long  __unused1;
	unsigned long  st_mtime;
	unsigned long  __unused2;
	unsigned long  st_ctime;
	unsigned long  __unused3;
	unsigned long  __unused4;
	unsigned long  __unused5;
};

typedef struct {
	unsigned int	major;
	unsigned int	minor;
} __new_dev_t;

struct stat64 {
	__new_dev_t	st_dev;
	__u64		st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;
	unsigned int	st_uid;
	unsigned int	st_gid;
	__new_dev_t	st_rdev;
	__s64		st_size;
	__u64		st_blocks;
	unsigned long	st_atime;
	unsigned long	__unused1;
	unsigned long	st_mtime;
	unsigned long	__unused2;
	unsigned long	st_ctime;
	unsigned long	__unused3;
	unsigned long	st_blksize;
	unsigned long	__unused4;
};

#define __XSTAT_VER_1		1
#define __XSTAT_VER_2		2
#define __XSTAT_VER_MASK	0xff

#define __XSTAT_VER_XSTAT	0x000
#define __XSTAT_VER_LXSTAT	0x100
#define __XSTAT_VER_FXSTAT	0x200
#define __XSTAT_VER_TYPEMASK	0xff00

#define __XMKNOD_VER_1		1

#endif /* _M68K_STAT_H */

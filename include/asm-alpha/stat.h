#ifndef _ALPHA_STAT_H
#define _ALPHA_STAT_H

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
	unsigned int	st_blkize;
	int		st_blocks;
	unsigned int	st_flags;
	unsigned int	st_gen;
};

struct new_stat {
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

#endif

#ifndef _MSDOS_FS_SB
#define _MSDOS_FS_SB

struct msdos_sb_info { /* space in struct super_block is 28 bytes */
	unsigned short cluster_size; /* sectors/cluster */
	unsigned char fats,fat_bits; /* number of FATs, FAT bits (12 or 16) */
	unsigned short fat_start,fat_length; /* FAT start & length (sec.) */
	unsigned short dir_start,dir_entries; /* root dir start & entries */
	unsigned short data_start;   /* first data sector */
	unsigned long clusters;      /* number of clusters */
	uid_t fs_uid;
	gid_t fs_gid;
	unsigned short fs_umask;
	unsigned char name_check; /* r = relaxed, n = normal, s = strict */
	unsigned char conversion; /* b = binary, t = text, a = auto */
};

#endif

#ifndef _MSDOS_FS_SB
#define _MSDOS_FS_SB

/*
 * MS-DOS file system in-core superblock data
 */

struct fat_mount_options {
	uid_t fs_uid;
	gid_t fs_gid;
	unsigned short fs_umask;
	unsigned char name_check; /* r = relaxed, n = normal, s = strict */
	unsigned char conversion; /* b = binary, t = text, a = auto */
	unsigned quiet:1,         /* set = fake successful chmods and chowns */
		 showexec:1,      /* set = only set x bit for com/exe/bat */
		 sys_immutable:1, /* set = system files are immutable */
		 dotsOK:1,        /* set = hidden and system files are named '.filename' */
		 isvfat:1,        /* 0=no vfat long filename support, 1=vfat support */
		 unicode_xlate:1, /* create escape sequences for unhandled Unicode */
		 posixfs:1,       /* Allow names like makefile and Makefile to coexist */
		 numtail:1;       /* Does first alias have a numeric '~1' type tail? */
};


struct msdos_sb_info {
	unsigned short cluster_size; /* sectors/cluster */
	unsigned char fats,fat_bits; /* number of FATs, FAT bits (12 or 16) */
	unsigned short fat_start,fat_length; /* FAT start & length (sec.) */
	unsigned short dir_start,dir_entries; /* root dir start & entries */
	unsigned short data_start;   /* first data sector */
	unsigned long clusters;      /* number of clusters */
	struct wait_queue *fat_wait;
	int fat_lock;
	int prev_free;               /* previously returned free cluster number */
	int free_clusters;           /* -1 if undefined */
	struct fat_mount_options options;
};

#endif

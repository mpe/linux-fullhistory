/*
 *  macros.h
 *
 *  Copyright (C) 1995 Martin von Löwis
 *  Copyright (C) 1996 Régis Duchesne
 */

#define NTFS_FD(vol)		((vol)->u.fd)

/* Linux */
#ifdef NTFS_IN_LINUX_KERNEL
#define NTFS_SB(vol)		((struct super_block*)(vol)->sb)
#define NTFS_SB2VOL(sb)         (&(sb)->u.ntfs_sb)
#define NTFS_INO2VOL(ino)	(&((ino)->i_sb->u.ntfs_sb))
#define NTFS_LINO2NINO(ino)     (&((ino)->u.ntfs_i))
#else
#define NTFS_SB(vol)		((struct super_block*)(vol)->u.sb)
#define NTFS_SB2VOL(sb)		((ntfs_volume*)(sb)->u.generic_sbp)
#define NTFS_INO2VOL(ino)	((ntfs_volume*)((ino)->i_sb->u.generic_sbp))
#define NTFS_LINO2NINO(ino)     ((ntfs_inode*)((ino)->u.generic_ip))
#endif

/* BSD */
#define NTFS_MNT(vol)		((struct mount*)(vol)->u.sb)
#define NTFS_MNT2VOL(sb)	((ntfs_volume*)(sb)->mnt_data)
#define NTFS_V2INO(ino)		((ntfs_inode*)((ino)->v_data))

/* Classical min and max macros still missing in standard headers... */
#define min(a,b)	((a) <= (b) ? (a) : (b))
#define max(a,b)	((a) >= (b) ? (a) : (b))

#define IS_MAGIC(a,b)		(*(int*)(a)==*(int*)(b))
#define IS_MFT_RECORD(a)	IS_MAGIC((a),"FILE")
#define IS_NTFS_VOLUME(a)	IS_MAGIC((a)+3,"NTFS")
#define IS_INDEX_RECORD(a)	IS_MAGIC((a),"INDX")

/* 'NTFS' in little endian */
#define NTFS_SUPER_MAGIC	0x5346544E

/*
 * Local variables:
 *  c-file-style: "linux"
 * End:
 */

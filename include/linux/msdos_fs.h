#ifndef _LINUX_MSDOS_FS_H
#define _LINUX_MSDOS_FS_H

/*
 * The MS-DOS filesystem constants/structures
 */
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/fd.h>

#define MSDOS_ROOT_INO  1 /* == MINIX_ROOT_INO */
#define SECTOR_SIZE     512 /* sector size (bytes) */
#define SECTOR_BITS	9 /* log2(SECTOR_SIZE) */
#define MSDOS_DPB	(MSDOS_DPS) /* dir entries per block */
#define MSDOS_DPB_BITS	4 /* log2(MSDOS_DPB) */
#define MSDOS_DPS	(SECTOR_SIZE/sizeof(struct msdos_dir_entry))
#define MSDOS_DPS_BITS	4 /* log2(MSDOS_DPS) */
#define MSDOS_DIR_BITS	5 /* log2(sizeof(struct msdos_dir_entry)) */

#define MSDOS_SUPER_MAGIC 0x4d44 /* MD */

#define FAT_CACHE    8 /* FAT cache size */

#define MSDOS_MAX_EXTRA	3 /* tolerate up to that number of clusters which are
			     inaccessible because the FAT is too short */

#define ATTR_RO      1  /* read-only */
#define ATTR_HIDDEN  2  /* hidden */
#define ATTR_SYS     4  /* system */
#define ATTR_VOLUME  8  /* volume label */
#define ATTR_DIR     16 /* directory */
#define ATTR_ARCH    32 /* archived */

#define ATTR_NONE    0 /* no attribute bits */
#define ATTR_UNUSED  (ATTR_VOLUME | ATTR_ARCH | ATTR_SYS | ATTR_HIDDEN)
	/* attribute bits that are copied "as is" */
#define ATTR_EXT     (ATTR_RO | ATTR_HIDDEN | ATTR_SYS | ATTR_VOLUME)
	/* bits that are used by the Windows 95/Windows NT extended FAT */

#define ATTR_DIR_READ_BOTH 512 /* read both short and long names from the
				* vfat filesystem.  This is used by Samba
				* to export the vfat filesystem with correct
				* shortnames. */
#define ATTR_DIR_READ_SHORT 1024

#define CASE_LOWER_BASE 8	/* base is lower case */
#define CASE_LOWER_EXT  16	/* extension is lower case */

#define SCAN_ANY     0  /* either hidden or not */
#define SCAN_HID     1  /* only hidden */
#define SCAN_NOTHID  2  /* only not hidden */
#define SCAN_NOTANY  3  /* test name, then use SCAN_HID or SCAN_NOTHID */

#define DELETED_FLAG 0xe5 /* marks file as deleted when in name[0] */
#define IS_FREE(n) (!*(n) || *(const unsigned char *) (n) == DELETED_FLAG || \
  *(const unsigned char *) (n) == FD_FILL_BYTE)

#define MSDOS_VALID_MODE (S_IFREG | S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO)
	/* valid file mode bits */

#define MSDOS_SB(s) (&((s)->u.msdos_sb))
#define MSDOS_I(i) (&((i)->u.msdos_i))

#define MSDOS_NAME 11 /* maximum name length */
#define MSDOS_LONGNAME 256 /* maximum name length */
#define MSDOS_SLOTS 21  /* max # of slots needed for short and long names */
#define MSDOS_DOT    ".          " /* ".", padded to MSDOS_NAME chars */
#define MSDOS_DOTDOT "..         " /* "..", padded to MSDOS_NAME chars */

#define MSDOS_FAT12 4078 /* maximum number of clusters in a 12 bit FAT */

/*
 * Inode flags
 */
#define FAT_BINARY_FL		0x00000001 /* File contains binary data */

/*
 * ioctl commands
 */
#define	VFAT_IOCTL_READDIR_BOTH		_IOR('r', 1, long)
#define	VFAT_IOCTL_READDIR_SHORT	_IOW('r', 2, long)

/*
 * Conversion from and to little-endian byte order. (no-op on i386/i486)
 *
 * Naming: Ca_b_c, where a: F = from, T = to, b: LE = little-endian,
 * BE = big-endian, c: W = word (16 bits), L = longword (32 bits)
 */

#define CF_LE_W(v) (v)
#define CF_LE_L(v) (v)
#define CT_LE_W(v) (v)
#define CT_LE_L(v) (v)


struct msdos_boot_sector {
	__s8	ignored[3];	/* Boot strap short or near jump */
	__s8	system_id[8];	/* Name - can be used to special case
				   partition manager volumes */
	__u8	sector_size[2];	/* bytes per logical sector */
	__u8	cluster_size;	/* sectors/cluster */
	__u16	reserved;	/* reserved sectors */
	__u8	fats;		/* number of FATs */
	__u8	dir_entries[2];	/* root directory entries */
	__u8	sectors[2];	/* number of sectors */
	__u8	media;		/* media code (unused) */
	__u16	fat_length;	/* sectors/FAT */
	__u16	secs_track;	/* sectors per track */
	__u16	heads;		/* number of heads */
	__u32	hidden;		/* hidden sectors (unused) */
	__u32	total_sect;	/* number of sectors (if sectors == 0) */
};

struct msdos_dir_entry {
	__s8	name[8],ext[3];	/* name and extension */
	__u8	attr;		/* attribute bits */
	__u8    lcase;		/* Case for base and extension */
	__u8	ctime_ms;	/* Creation time, milliseconds */
	__u16	ctime;		/* Creation time */
	__u16	cdate;		/* Creation date */
	__u16	adate;		/* Last access date */
	__u8    unused[2];
	__u16	time,date,start;/* time, date and first cluster */
	__u32	size;		/* file size (in bytes) */
};

/* Up to 13 characters of the name */
struct msdos_dir_slot {
	__u8    id;		/* sequence number for slot */
	__u8    name0_4[10];	/* first 5 characters in name */
	__u8    attr;		/* attribute byte */
	__u8    reserved;	/* always 0 */
	__u8    alias_checksum;	/* checksum for 8.3 alias */
	__u8    name5_10[12];	/* 6 more characters in name */
	__u8    start[2];	/* starting cluster number */
	__u8    name11_12[4];	/* last 2 characters in name */
};

struct slot_info {
	int is_long;		       /* was the found entry long */
	int long_slots;		       /* number of long slots in filename */
	int total_slots;	       /* total slots (long and short) */
	loff_t longname_offset;	       /* dir offset for longname start */
	loff_t shortname_offset;       /* dir offset for shortname start */
	int ino;		       /* ino for the file */
};

/* Determine whether this FS has kB-aligned data. */
#define MSDOS_CAN_BMAP(mib) (!(((mib)->cluster_size & 1) || \
    ((mib)->data_start & 1)))

/* Convert attribute bits and a mask to the UNIX mode. */
#define MSDOS_MKMODE(a,m) (m & (a & ATTR_RO ? S_IRUGO|S_IXUGO : S_IRWXUGO))

/* Convert the UNIX mode to MS-DOS attribute bits. */
#define MSDOS_MKATTR(m) ((m & S_IWUGO) ? ATTR_NONE : ATTR_RO)


#ifdef __KERNEL__

typedef int (*fat_filldir_t)(filldir_t filldir, void *, const char *,
			     int, int, off_t, off_t, int, ino_t);

struct fat_cache {
	kdev_t device; /* device number. 0 means unused. */
	int ino; /* inode number. */
	int file_cluster; /* cluster number in the file. */
	int disk_cluster; /* cluster number on disk. */
	struct fat_cache *next; /* next cache entry */
};

/* misc.c */
extern int is_binary(char conversion,char *extension);
extern void lock_fat(struct super_block *sb);
extern void unlock_fat(struct super_block *sb);
extern int fat_add_cluster(struct inode *inode);
extern int date_dos2unix(__u16 time, __u16 date);
extern void fat_fs_panic(struct super_block *s,const char *msg);
extern void fat_lock_creation(void);
extern void fat_unlock_creation(void);
extern void fat_date_unix2dos(int unix_date,__u16 *time, __u16 *date);
extern int fat_get_entry(struct inode *dir,loff_t *pos,struct buffer_head **bh,
			 struct msdos_dir_entry **de);
extern int fat_scan(struct inode *dir,const char *name,struct buffer_head **res_bh,
		    struct msdos_dir_entry **res_de,int *ino,char scantype);
extern int fat_parent_ino(struct inode *dir,int locked);
extern int fat_subdirs(struct inode *dir);

/* fat.c */
extern int fat_access(struct super_block *sb,int nr,int new_value);
extern int fat_smap(struct inode *inode,int sector);
extern int fat_free(struct inode *inode,int skip);
void fat_cache_inval_inode(struct inode *inode);
void fat_cache_inval_dev(kdev_t device);
extern void cache_init(void);
void cache_lookup(struct inode *inode,int cluster,int *f_clu,int *d_clu);
void cache_add(struct inode *inode,int f_clu,int d_clu);
int get_cluster(struct inode *inode,int cluster);

/* inode.c */
extern int fat_bmap(struct inode *inode,int block);
extern int fat_notify_change(struct inode *,struct iattr *);
extern void fat_put_inode(struct inode *inode);
extern void fat_put_super(struct super_block *sb);
extern void fat_read_inode(struct inode *inode, struct inode_operations *dir_ops);
extern struct super_block *fat_read_super(struct super_block *s, void *data, int silent);
extern void msdos_put_super(struct super_block *sb);
extern void fat_statfs(struct super_block *sb,struct statfs *buf, int);
extern void fat_write_inode(struct inode *inode);

/* dir.c */
extern struct file_operations fat_dir_operations;
extern int fat_readdirx(struct inode *inode, struct file *filp, void *dirent,
			fat_filldir_t fat_filldir, filldir_t filldir,
			int shortnames, int longnames, int both);
extern int fat_readdir(struct inode *inode, struct file *filp,
		       void *dirent, filldir_t);
extern int fat_dir_ioctl(struct inode * inode, struct file * filp,
			 unsigned int cmd, unsigned long arg);

/* file.c */
extern struct inode_operations fat_file_inode_operations;
extern struct inode_operations fat_file_inode_operations_1024;
extern int fat_file_read(struct inode *, struct file *, char *, int);
extern int fat_file_write(struct inode *, struct file *, const char *, int);
extern void fat_truncate(struct inode *inode);

/* mmap.c */
extern int fat_mmap(struct inode *, struct file *, struct vm_area_struct *);


/* vfat.c */
extern int init_vfat_fs(void);


/* msdosfs_syms.c */
extern int init_msdos_fs(void);
extern struct file_system_type msdos_fs_type;

/* msdos.c */
extern struct super_block *msdos_read_super(struct super_block *sb,void *data, int silent);

/* msdos.c - these are for Umsdos */
extern void msdos_read_inode(struct inode *inode);
extern int msdos_lookup(struct inode *dir,const char *name,int len, 
			struct inode **result);
extern int msdos_create(struct inode *dir,const char *name,int len,int mode,
			struct inode **result);
extern int msdos_rmdir(struct inode *dir,const char *name,int len);
extern int msdos_mkdir(struct inode *dir,const char *name,int len,int mode);
extern int msdos_unlink(struct inode *dir,const char *name,int len);
extern int msdos_unlink_umsdos(struct inode *dir,const char *name,int len);
extern int msdos_rename(struct inode *old_dir,const char *old_name,int old_len,
			struct inode *new_dir,const char *new_name,int new_len,
			int must_be_dir);

/* fatfs_syms.c */
extern int init_fat_fs(void);

/* vfat/namei.c - these are for dmsdos */
extern int vfat_create(struct inode *dir,const char *name,int len,int mode,
		       struct inode **result);
extern int vfat_unlink(struct inode *dir,const char *name,int len);
extern int vfat_mkdir(struct inode *dir,const char *name,int len,int mode);
extern int vfat_rmdir(struct inode *dir,const char *name,int len);
extern int vfat_rename(struct inode *old_dir,const char *old_name,int old_len,
		       struct inode *new_dir,const char *new_name,int new_len,
		       int must_be_dir);
extern void vfat_put_super(struct super_block *sb);
extern struct super_block *vfat_read_super(struct super_block *sb,void *data,
					   int silent);
extern void vfat_read_inode(struct inode *inode);
extern int vfat_lookup(struct inode *dir,const char *name,int len,
		       struct inode **result);

#endif /* __KERNEL__ */

#endif

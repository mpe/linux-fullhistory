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

#define ATTR_RO      1  /* read-only */
#define ATTR_HIDDEN  2  /* hidden */
#define ATTR_SYS     4  /* system */
#define ATTR_VOLUME  8  /* volume label */
#define ATTR_DIR     16 /* directory */
#define ATTR_ARCH    32 /* archived */

#define ATTR_NONE    0 /* no attribute bits */
#define ATTR_UNUSED  (ATTR_VOLUME | ATTR_ARCH | ATTR_SYS | ATTR_HIDDEN)
	/* attribute bits that are copied "as is" */

#define DELETED_FLAG 0xe5 /* marks file as deleted when in name[0] */
#define IS_FREE(n) (!*(n) || *(unsigned char *) (n) == DELETED_FLAG || \
  *(unsigned char *) (n) == FD_FILL_BYTE)

#define MSDOS_VALID_MODE (S_IFREG | S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO)
	/* valid file mode bits */

#define MSDOS_SB(s) (&((s)->u.msdos_sb))
#define MSDOS_I(i) (&((i)->u.msdos_i))

#define MSDOS_NAME 11 /* maximum name length */
#define MSDOS_DOT    ".          " /* ".", padded to MSDOS_NAME chars */
#define MSDOS_DOTDOT "..         " /* "..", padded to MSDOS_NAME chars */

#define MSDOS_FAT12 4078 /* maximum number of clusters in a 12 bit FAT */

/*
 * Conversion from and to little-endian byte order. (no-op on i386/i486)
 *
 * Naming: Ca_b_c, where a: F = from, T = to, b: LE = little-endian, BE = big-
 * endian, c: W = word (16 bits), L = longword (32 bits)
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
	__u8	unused[10];
	__u16	time,date,start;/* time, date and first cluster */
	__u32	size;		/* file size (in bytes) */
};

struct fat_cache {
	int device; /* device number. 0 means unused. */
	int ino; /* inode number. */
	int file_cluster; /* cluster number in the file. */
	int disk_cluster; /* cluster number on disk. */
	struct fat_cache *next; /* next cache entry */
};

/* Determine whether this FS has kB-aligned data. */

#define MSDOS_CAN_BMAP(mib) (!(((mib)->cluster_size & 1) || \
    ((mib)->data_start & 1)))

/* Convert attribute bits and a mask to the UNIX mode. */

#define MSDOS_MKMODE(a,m) (m & (a & ATTR_RO ? S_IRUGO|S_IXUGO : S_IRWXUGO))

/* Convert the UNIX mode to MS-DOS attribute bits. */

#define MSDOS_MKATTR(m) ((m & S_IWUGO) ? ATTR_NONE : ATTR_RO)

#ifdef __KERNEL__

/* misc.c */

extern void fs_panic(struct super_block *s,char *msg);
extern int is_binary(char conversion,char *extension);
extern void lock_creation(void);
extern void unlock_creation(void);
extern void lock_fat(struct super_block *sb);
extern void unlock_fat(struct super_block *sb);
extern int msdos_add_cluster(struct inode *inode);
extern int date_dos2unix(__u16 time, __u16 date);
extern void date_unix2dos(int unix_date,__u16 *time, __u16 *date);
extern int msdos_get_entry(struct inode *dir,loff_t *pos,struct buffer_head **bh,
    struct msdos_dir_entry **de);
extern int msdos_scan(struct inode *dir,char *name,struct buffer_head **res_bh,
    struct msdos_dir_entry **res_de,int *ino);
extern int msdos_parent_ino(struct inode *dir,int locked);
extern int msdos_subdirs(struct inode *dir);

/* fat.c */

extern int fat_access(struct super_block *sb,int nr,int new_value);
extern int msdos_smap(struct inode *inode,int sector);
extern int fat_free(struct inode *inode,int skip);
extern void cache_init(void);
void cache_lookup(struct inode *inode,int cluster,int *f_clu,int *d_clu);
void cache_add(struct inode *inode,int f_clu,int d_clu);
void cache_inval_inode(struct inode *inode);
void cache_inval_dev(int device);
int get_cluster(struct inode *inode,int cluster);

/* namei.c */

extern int msdos_lookup(struct inode *dir,const char *name,int len,
	struct inode **result);
extern int msdos_create(struct inode *dir,const char *name,int len,int mode,
	struct inode **result);
extern int msdos_mkdir(struct inode *dir,const char *name,int len,int mode);
extern int msdos_rmdir(struct inode *dir,const char *name,int len);
extern int msdos_unlink(struct inode *dir,const char *name,int len);
extern int msdos_unlink_umsdos(struct inode *dir,const char *name,int len);
extern int msdos_rename(struct inode *old_dir,const char *old_name,int old_len,
	struct inode *new_dir,const char *new_name,int new_len);

/* inode.c */

extern void msdos_put_inode(struct inode *inode);
extern void msdos_put_super(struct super_block *sb);
extern struct super_block *msdos_read_super(struct super_block *s,
					    void *data,int);
extern void msdos_statfs(struct super_block *sb,struct statfs *buf);
extern int msdos_bmap(struct inode *inode,int block);
extern void msdos_read_inode(struct inode *inode);
extern void msdos_write_inode(struct inode *inode);
extern int msdos_notify_change(struct inode *,struct iattr *);

/* dir.c */

extern struct inode_operations msdos_dir_inode_operations;
extern int msdos_readdir (struct inode *inode, struct file *filp,
	struct dirent *dirent, int count);
/* file.c */

extern struct inode_operations msdos_file_inode_operations;
extern struct inode_operations msdos_file_inode_operations_1024;
extern int msdos_file_read(struct inode *, struct file *, char *, int);
extern int msdos_file_write(struct inode *, struct file *, char *, int);
extern struct inode_operations msdos_file_inode_operations_no_bmap;

extern void msdos_truncate(struct inode *inode);

/* mmap.c */
extern int msdos_mmap(struct inode *, struct file *, struct vm_area_struct *);

#endif /* __KERNEL__ */

#endif

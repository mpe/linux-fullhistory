
#ifndef _AFFS_FS_H
#define _AFFS_FS_H

#include <linux/types.h>
/*
 * The affs filesystem constants/structures
 */

#define AFFS_BLOCK_BITS 9
#define AFFS_BLOCK_SIZE 512

#define AFFS_BUFFER_BITS 9
#define AFFS_BUFFER_SIZE 512

#define AFFS_BLOCK_NUMBER(X) (X<<1)

#define AFFS_SUPER_MAGIC 0xadff

/* Get the filesystem block size given an inode. */
#define AFFS_I2BSIZE(inode) ((inode)->i_sb->u.affs_sb.s_block_size)

/* Read the device block that contains filesystem block ("sector"). */

static inline struct buffer_head *affs_sread(int dev,int sector,void **start)
{
 	struct buffer_head *bh;
	int mask;

	bh = bread (dev, sector >> (BLOCK_SIZE_BITS - AFFS_BLOCK_BITS), 1024);
	if (!bh)
		return NULL;
	mask = (1 << (BLOCK_SIZE_BITS - AFFS_BLOCK_BITS)) - 1;
    	*start = bh->b_data + ((sector & mask) << AFFS_BLOCK_BITS);
	return bh;
}

/* Use affs_sread() to read a "sector", but take the filesystems partition
   offset into account. */

static inline struct buffer_head *affs_pread(struct inode *inode,
					     int sector, void **start)
{
	int offset = inode->i_sb->u.affs_sb.s_partition_offset;
	return affs_sread (inode->i_dev, sector + offset, start);
}

/* amigaffs.c prototypes */

extern int affs_get_key_entry (int bsize, void *data, int entry_pos);
extern int affs_find_next_hash_entry (int bsize, void *dir_data, int *hash_pos);
extern int affs_get_fh_hash_link (int bsize, void *fh_data);
extern int affs_get_file_name (int bsize, void *fh_data, char **name);
extern int affs_get_extension (int bsize, void *fh_data);
extern int affs_checksum_block (int bsize, void *data, int *ptype, int *stype);

/* The stuff that follows may be totally unneeded. I have not checked to see 
 which prototypes we are still using.  */

extern int affs_open(struct inode * inode, struct file * filp);
extern void affs_release(struct inode * inode, struct file * filp);
extern int affs_lookup(struct inode * dir,const char * name, int len,
	struct inode ** result);
extern unsigned long affs_count_free_inodes(struct super_block *sb);
extern int affs_new_block(int dev);
extern int affs_free_block(int dev, int block);
extern int affs_bmap(struct inode *,int);

extern void affs_put_super(struct super_block *);
extern struct super_block *affs_read_super(struct super_block *,void *,int);
extern void affs_read_inode(struct inode *);
extern void affs_put_inode(struct inode *);
extern void affs_statfs(struct super_block *, struct statfs *, int);
extern int affs_parent_ino(struct inode *dir);
extern int affs_lseek(struct inode *, struct file *, off_t, int);
extern int affs_read(struct inode *, struct file *, char *, int);
extern int affs_file_read(struct inode *, struct file *, char *, int);
extern int init_affs_fs(void);

extern struct inode_operations affs_file_inode_operations;
extern struct inode_operations affs_dir_inode_operations;
extern struct inode_operations affs_symlink_inode_operations;
extern struct inode_operations affs_chrdev_inode_operations;
extern struct inode_operations affs_blkdev_inode_operations;

extern struct file_operations affs_file_operations;
extern struct file_operations affs_dir_operations;

/* The following macros are used to check for memory leaks. */
#ifdef LEAK_CHECK
#define free_s leak_check_free_s
#define malloc leak_check_malloc
#define bread leak_check_bread
#define brelse leak_check_brelse
extern void * leak_check_malloc(unsigned int size);
extern void leak_check_free_s(void * obj, int size);
extern struct buffer_head * leak_check_bread(int dev, int block, int size);
extern void leak_check_brelse(struct buffer_head * bh);
#endif /* LEAK_CHECK */

#endif

#ifndef _AFFS_FS_H
#define _AFFS_FS_H
/*
 * The affs filesystem constants/structures
 */

#include <linux/types.h>
#include <linux/amigaffs.h>

#define AFFS_SUPER_MAGIC 0xadff

/* Get the filesystem block size given an inode. */
#define AFFS_I2BSIZE(inode) ((inode)->i_sb->s_blocksize)

/* Get the filesystem hash table size given an inode. */
#define AFFS_I2HSIZE(inode) ((inode)->i_sb->u.affs_sb.s_hashsize)

/* Get the block number bits given an inode */
#define AFFS_I2BITS(inode) ((inode)->i_sb->s_blocksize_bits)

/* Get the fs type given an inode */
#define AFFS_I2FSTYPE(inode) ((inode)->i_sb->u.affs_sb.s_flags & SF_INTL)

/* --- Prototypes -----------------------------------------------------------------------------	*/

/* amigaffs.c */

extern int		   affs_get_key_entry(int bsize, void *data, int entry_pos);
extern int		   affs_find_next_hash_entry(int bsize, void *dir_data, ULONG *hash_pos);
extern int		   affs_get_file_name(int bsize, void *fh_data, char **name);
extern ULONG		   affs_checksum_block(int bsize, void *data, LONG *ptype, LONG *stype);
extern void		   affs_fix_checksum(int bsize, void *data, int cspos);
extern void		   secs_to_datestamp(int secs, struct DateStamp *ds);
extern int		   prot_to_mode(ULONG prot);
extern ULONG		   mode_to_prot(int mode);
extern int		   affs_fix_hash_pred(struct inode *startino, int startoffset,
		 			      LONG key, LONG newkey);
extern int		   affs_fix_link_pred(struct inode *startino, LONG key, LONG newkey);

/* bitmap. c */

extern int		   affs_count_free_blocks(struct super_block *s);
extern int		   affs_count_free_bits(int blocksize, const UBYTE *data);
extern void		   affs_free_block(struct super_block *sb, LONG block);
extern LONG		   affs_new_header(struct inode *inode);
extern LONG		   affs_new_data(struct inode *inode);
extern void		   affs_make_zones(struct super_block *sb);

/* namei.c */

extern int		   affs_hash_name(const char *name, int len, int intl, int hashsize);
extern int		   affs_lookup(struct inode *dir,const char *name, int len,
				       struct inode **result);
extern int		   affs_unlink(struct inode *dir, const char *name, int len);
extern int		   affs_create(struct inode *dir, const char *name, int len, int mode,
				       struct inode **result);
extern int		   affs_mkdir(struct inode *dir, const char *name, int len, int mode);
extern int		   affs_rmdir(struct inode *dir, const char *name, int len);
extern int		   affs_link(struct inode *oldinode, struct inode *dir,
				     const char *name, int len);
extern int		   affs_symlink(struct inode *dir, const char *name, int len,
				        const char *symname);
extern int		   affs_fixup(struct buffer_head *bh, struct inode *inode);
extern int		   affs_rename(struct inode *old_dir, const char *old_name, int old_len,
				       struct inode *new_dir, const char *new_name, int new_len);

/* inode.c */

extern struct buffer_head *affs_bread(kdev_t dev, int block, int size);
extern void		   affs_brelse(struct buffer_head *buf);
extern void		   affs_put_super(struct super_block *);
extern int		   affs_parent_ino(struct inode *dir);
extern struct super_block *affs_read_super(struct super_block *,void *, int);
extern void		   affs_statfs(struct super_block *, struct statfs *, int bufsiz);
extern void		   affs_read_inode(struct inode *);
extern void		   affs_write_inode(struct inode *);
extern int		   affs_notify_change(struct inode *inode, struct iattr *attr);
extern void		   affs_put_inode(struct inode *);
extern struct inode	  *affs_new_inode(const struct inode *dir);
extern int		   affs_add_entry(struct inode *dir, struct inode *link, struct inode *inode,
					  const char *name, int len, LONG type);

/* file.c */

extern int		   affs_bmap(struct inode *inode, int block);
extern struct buffer_head *affs_getblock(struct inode *inode, int block);
extern void		   affs_truncate(struct inode *);
extern void		   affs_truncate_ofs(struct inode *);

/* dir.c */

extern void		   affs_dir_truncate(struct inode *);

/* jump tables */

extern struct inode_operations	 affs_file_inode_operations;
extern struct inode_operations	 affs_file_inode_operations_ofs;
extern struct inode_operations	 affs_dir_inode_operations;
extern struct inode_operations	 affs_symlink_inode_operations;
extern struct inode_operations	 affs_chrdev_inode_operations;
extern struct inode_operations	 affs_blkdev_inode_operations;

extern int init_affs_fs(void);
#endif

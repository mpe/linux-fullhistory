#ifndef __UDF_DECL_H
#define __UDF_DECL_H

#define UDF_VERSION_NOTICE "v0.8.9.3"

#include <linux/udf_udf.h>

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/udf_fs.h>
#include <linux/config.h>

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,7)
#error "The UDF Module Current Requires Kernel Version 2.3.7 or greater"
#endif

#include <linux/fs.h>
/* if we're not defined, we must be compiling outside of the kernel tree */
#if !defined(CONFIG_UDF_FS) && !defined(CONFIG_UDF_FS_MODULE)
/* ... so override config */
#define CONFIG_UDF_FS_MODULE
/* explicitly include udf_fs_sb.h and udf_fs_i.h */
#include <linux/udf_fs_sb.h>
#include <linux/udf_fs_i.h>
#endif

struct dentry;
struct inode;
struct task_struct;
struct buffer_head;
struct super_block;

extern struct inode_operations udf_dir_inode_operations;
extern struct inode_operations udf_file_inode_operations;
extern struct inode_operations udf_file_inode_operations_adinicb;
extern struct inode_operations udf_symlink_inode_operations;

struct udf_fileident_bh
{
	struct buffer_head *sbh;
	struct buffer_head *ebh;
	int soffset;
	int eoffset;
};

extern void udf_error(struct super_block *, const char *, const char *, ...);
extern void udf_warning(struct super_block *, const char *, const char *, ...);
extern int udf_write_fi(struct FileIdentDesc *, struct FileIdentDesc *, struct udf_fileident_bh *, Uint8 *, Uint8 *);
extern struct dentry * udf_lookup(struct inode *, struct dentry *);
extern int udf_create(struct inode *, struct dentry *, int);
extern int udf_mknod(struct inode *, struct dentry *, int, int);
extern int udf_mkdir(struct inode *, struct dentry *, int);
extern int udf_rmdir(struct inode *, struct dentry *);
extern int udf_unlink(struct inode *, struct dentry *);
extern int udf_symlink(struct inode *, struct dentry *, const char *);
extern int udf_link(struct dentry *, struct inode *, struct dentry *);
extern int udf_rename(struct inode *, struct dentry *, struct inode *, struct dentry *);
extern int udf_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
extern struct inode *udf_iget(struct super_block *, lb_addr);
extern int udf_sync_inode(struct inode *);
extern struct buffer_head * udf_expand_adinicb(struct inode *, int *, int, int *);
extern struct buffer_head * udf_getblk(struct inode *, long, int, int *);
extern int udf_get_block(struct inode *, long, struct buffer_head *, int);
extern struct buffer_head * udf_bread(struct inode *, int, int, int *);
extern void udf_read_inode(struct inode *);
extern void udf_put_inode(struct inode *);
extern void udf_delete_inode(struct inode *);
extern void udf_write_inode(struct inode *);
extern long udf_locked_block_map(struct inode *, long);
extern long udf_block_map(struct inode *, long);
extern int inode_bmap(struct inode *, int, lb_addr *, Uint32 *, lb_addr *, Uint32 *, Uint32 *, struct buffer_head **);
extern int udf_add_aext(struct inode *, lb_addr *, int *, lb_addr, Uint32, struct buffer_head **, int);
extern int udf_write_aext(struct inode *, lb_addr, int *, lb_addr, Uint32, struct buffer_head **, int);
extern int udf_insert_aext(struct inode *, lb_addr, int, lb_addr, Uint32, struct buffer_head *);
extern int udf_delete_aext(struct inode *, lb_addr, int, lb_addr, Uint32, struct buffer_head *);
extern int udf_next_aext(struct inode *, lb_addr *, int *, lb_addr *, Uint32 *, struct buffer_head **, int);
extern int udf_current_aext(struct inode *, lb_addr *, int *, lb_addr *, Uint32 *, struct buffer_head **, int);

extern int udf_read_tagged_data(char *, int size, int fd, int block, int partref);

extern struct buffer_head *udf_tread(struct super_block *, int, int);
extern struct GenericAttrFormat *udf_add_extendedattr(struct inode *, Uint32, Uint32, Uint8, struct buffer_head **);
extern struct GenericAttrFormat *udf_get_extendedattr(struct inode *, Uint32, Uint8, struct buffer_head **);
extern struct buffer_head *udf_read_tagged(struct super_block *, Uint32, Uint32, Uint16 *);
extern struct buffer_head *udf_read_ptagged(struct super_block *, lb_addr, Uint32, Uint16 *);
extern struct buffer_head *udf_read_untagged(struct super_block *, Uint32, Uint32);
extern void udf_release_data(struct buffer_head *);

extern unsigned int udf_get_last_session(kdev_t);
extern unsigned int udf_get_last_block(kdev_t, int *);

extern Uint32 udf_get_pblock(struct super_block *, Uint32, Uint16, Uint32);
extern Uint32 udf_get_lb_pblock(struct super_block *, lb_addr, Uint32);

extern int udf_get_filename(Uint8 *, Uint8 *, int);

extern void udf_free_inode(struct inode *);
extern struct inode * udf_new_inode (const struct inode *, int, int *);
extern void udf_discard_prealloc(struct inode *);
extern void udf_truncate(struct inode *);
extern void udf_truncate_adinicb(struct inode *);
extern void udf_free_blocks(const struct inode *, lb_addr, Uint32, Uint32);
extern int udf_alloc_blocks(const struct inode *, Uint16, Uint32, Uint32);
extern int udf_new_block(const struct inode *, Uint16, Uint32, int *);
extern int udf_sync_file(struct file *, struct dentry *);

#else

#include <sys/types.h>

#endif /* __KERNEL__ */

#include "udfend.h"

/* structures */
struct udf_directory_record
{
	Uint32	d_parent;
	Uint32	d_inode;
	Uint32	d_name[255];
};

#define VDS_POS_PRIMARY_VOL_DESC	0
#define VDS_POS_UNALLOC_SPACE_DESC	1
#define VDS_POS_LOGICAL_VOL_DESC	2
#define VDS_POS_PARTITION_DESC		3
#define VDS_POS_IMP_USE_VOL_DESC	4
#define VDS_POS_VOL_DESC_PTR		5
#define VDS_POS_TERMINATING_DESC	6
#define VDS_POS_LENGTH				7

struct udf_vds_record
{
	Uint32 block;
	Uint32 volDescSeqNum;
};

struct ktm
{
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_isdst;
};

struct ustr
{
	Uint8 u_cmpID;
	Uint8 u_name[UDF_NAME_LEN-1];
	Uint8 u_len;
	Uint8 padding;
	unsigned long u_hash;
};


#define udf_fixed_to_variable(x) ( ( ( (x) >> 5 ) * 39 ) + ( (x) & 0x0000001F ) )
#define udf_variable_to_fixed(x) ( ( ( (x) / 39 ) << 5 ) + ( (x) % 39 ) )

#ifdef __KERNEL__

#define CURRENT_UTIME	(xtime.tv_usec)

#define udf_file_entry_alloc_offset(inode)\
	((UDF_I_EXTENDED_FE(inode) ?\
		sizeof(struct ExtendedFileEntry) :\
		sizeof(struct FileEntry)) + UDF_I_LENEATTR(inode))

#define udf_clear_bit(nr,addr) ext2_clear_bit(nr,addr)
#define udf_set_bit(nr,addr) ext2_set_bit(nr,addr)
#define udf_test_bit(nr, addr) ext2_test_bit(nr, addr)
#define udf_find_first_one_bit(addr, size) find_first_one_bit(addr, size)
#define udf_find_next_one_bit(addr, size, offset) find_next_one_bit(addr, size, offset)

#define leBPL_to_cpup(x) leNUM_to_cpup(BITS_PER_LONG, x)
#define leNUM_to_cpup(x,y) xleNUM_to_cpup(x,y)
#define xleNUM_to_cpup(x,y) (le ## x ## _to_cpup(y))

extern inline int find_next_one_bit (void * addr, int size, int offset)
{
	unsigned long * p = ((unsigned long *) addr) + (offset / BITS_PER_LONG);
	unsigned long result = offset & ~(BITS_PER_LONG-1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= (BITS_PER_LONG-1);
	if (offset)
	{
		tmp = leBPL_to_cpup(p++);
		tmp &= ~0UL << offset;
		if (size < BITS_PER_LONG)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}
	while (size & ~(BITS_PER_LONG-1))
	{
		if ((tmp = leBPL_to_cpup(p++)))
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = leBPL_to_cpup(p);
found_first:
	tmp &= ~0UL >> (BITS_PER_LONG-size);
found_middle:
	return result + ffz(~tmp);
}

#define find_first_one_bit(addr, size)\
	find_next_one_bit((addr), (size), 0)

#endif

/* Miscellaneous UDF Prototypes */

extern int udf_ustr_to_dchars(Uint8 *, const struct ustr *, int);
extern int udf_ustr_to_char(Uint8 *, const struct ustr *, int);
extern int udf_ustr_to_dstring(dstring *, const struct ustr *, int);
extern int udf_dchars_to_ustr(struct ustr *, const Uint8 *, int);
extern int udf_char_to_ustr(struct ustr *, const Uint8 *, int);
extern int udf_dstring_to_ustr(struct ustr *, const dstring *, int);

extern Uint16 udf_crc(Uint8 *, Uint32, Uint16);
extern int udf_translate_to_linux(Uint8 *, Uint8 *, int, Uint8 *, int);
extern int udf_build_ustr(struct ustr *, dstring *, int);
extern int udf_build_ustr_exact(struct ustr *, dstring *, int);
extern int udf_CS0toUTF8(struct ustr *, struct ustr *);
extern int udf_UTF8toCS0(dstring *, struct ustr *, int);

extern uid_t  udf_convert_uid(int);
extern gid_t  udf_convert_gid(int);
extern Uint32 udf64_low32(Uint64);
extern Uint32 udf64_high32(Uint64);


extern time_t *udf_stamp_to_time(time_t *, long *, timestamp);
extern timestamp *udf_time_to_stamp(timestamp *, time_t, long);
extern time_t udf_converttime (struct ktm *);

#ifdef __KERNEL__
extern Uint8 *
udf_filead_read(struct inode *, Uint8 *, Uint8, lb_addr, int *, int *,
	struct buffer_head **, int *);

extern struct FileIdentDesc *
udf_fileident_read(struct inode *, int *,
	struct udf_fileident_bh *,
	struct FileIdentDesc *,
	lb_addr *, Uint32 *,
	Uint32 *, struct buffer_head **);
#endif
extern struct FileIdentDesc * 
udf_get_fileident(void * buffer, int bufsize, int * offset);
extern extent_ad * udf_get_fileextent(void * buffer, int bufsize, int * offset);
extern long_ad * udf_get_filelongad(void * buffer, int bufsize, int * offset, int);
extern short_ad * udf_get_fileshortad(void * buffer, int bufsize, int * offset, int);
extern Uint8 * udf_get_filead(struct FileEntry *, Uint8 *, int, int, int, int *);

extern void udf_update_tag(char *, int);
extern void udf_new_tag(char *, Uint16, Uint16, Uint16, Uint32, int);

#endif

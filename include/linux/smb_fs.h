/*
 *  smb_fs.h
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *
 */

#ifndef _LINUX_SMB_FS_H
#define _LINUX_SMB_FS_H

#include <linux/smb.h>
#include <linux/fs.h>
#include <linux/in.h>
#include <linux/types.h>

#include <linux/smb_mount.h>
#include <linux/smb_fs_sb.h>
#include <linux/smb_fs_i.h>

/*
 * ioctl commands
 */
#define	SMB_IOC_GETMOUNTUID		_IOR('u', 1, uid_t)

#ifdef __KERNEL__

/*
 * The readdir cache size controls how many directory entries are cached.
 */
#define SMB_READDIR_CACHE_SIZE        64

/*
 * This defines the number of filenames cached in memory to avoid 
 * constructing filenames from \
 */
#define SMB_CACHE_TABLE_SIZE          64

#define SMB_SUPER_MAGIC               0x517B



#define SMB_SBP(sb)          ((struct smb_sb_info *)(sb->u.generic_sbp))
#define SMB_INOP(inode)      ((struct smb_inode_info *)(inode->u.generic_ip))

#define SMB_SERVER(inode)    (&(SMB_SBP(inode->i_sb)->s_server))
#define SMB_SERVATTR(inode)  (&(SMB_SBP(inode->i_sb)->s_attr))

#define SMB_FINFO(inode)     (&(SMB_INOP(inode)->finfo))

#define SMB_HEADER_LEN   37     /* includes everything up to, but not
                                 * including smb_bcc */

#ifdef DEBUG_SMB_MALLOC

#include <linux/malloc.h>

extern int smb_malloced;
extern int smb_current_malloced;

static inline void *
smb_kmalloc(unsigned int size, int priority)
{
        smb_malloced += 1;
        smb_current_malloced += 1;
        return kmalloc(size, priority);
}

static inline void
smb_kfree_s(void *obj, int size)
{
        smb_current_malloced -= 1;
        kfree_s(obj, size);
}

#else /* DEBUG_SMB_MALLOC */

#define smb_kmalloc(s,p) kmalloc(s,p)
#define smb_kfree_s(o,s) kfree_s(o,s)

#endif /* DEBUG_SMB_MALLOC */

#if DEBUG_SMB > 0
#define DPRINTK(format, args...) printk(format , ## args)
#else
#define DPRINTK(format, args...)
#endif

#if DEBUG_SMB > 1
#define DDPRINTK(format, args...) printk(format , ## args)
#else
#define DDPRINTK(format, args...)
#endif


/* linux/fs/smbfs/file.c */
extern struct inode_operations smb_file_inode_operations;
int smb_make_open(struct inode *i, int right);

/* linux/fs/smbfs/dir.c */
extern struct inode_operations smb_dir_inode_operations;
void smb_free_inode_info(struct smb_inode_info *i);
void smb_free_all_inodes(struct smb_server *server);
void smb_init_root(struct smb_server *server);
int  smb_stat_root(struct smb_server *server);
void smb_init_dir_cache(void);
void smb_invalid_dir_cache(unsigned long ino);
void smb_invalidate_all_inodes(struct smb_server *server);
void smb_free_dir_cache(void);

/* linux/fs/smbfs/ioctl.c */
int smb_ioctl (struct inode * inode, struct file * filp,
               unsigned int cmd, unsigned long arg);

/* linux/fs/smbfs/inode.c */
struct super_block *smb_read_super(struct super_block *sb,
                                   void *raw_data, int silent);
extern int init_smb_fs(void);
int smb_notify_change(struct inode *inode, struct iattr *attr);
void smb_invalidate_connection(struct smb_server *server);
int smb_conn_is_valid(struct smb_server *server);

/* linux/fs/smbfs/proc.c */
dword smb_len(unsigned char *packet);
byte *smb_encode_smb_length(byte *p, dword len);
int smb_proc_open(struct smb_server *server, const char *pathname,
                  int len, struct smb_dirent *entry);
int smb_proc_close(struct smb_server *server, struct smb_dirent *finfo);
int smb_proc_read(struct smb_server *server, struct smb_dirent *finfo, 
		  off_t offset, long count, char *data, int fs);
int smb_proc_read_raw(struct smb_server *server, struct smb_dirent *finfo, 
                      off_t offset, long count, char *data);
int smb_proc_write(struct smb_server *server, struct smb_dirent *finfo,
		   off_t offset, int count, const char *data);
int smb_proc_write_raw(struct smb_server *server, struct smb_dirent *finfo, 
                       off_t offset, long count, const char *data);
int smb_proc_create(struct smb_server *server, const char *path,
                    int len, struct smb_dirent *entry);
int smb_proc_mknew(struct smb_server *server, const char *path, int len,
                   struct smb_dirent *entry);
int smb_proc_mv(struct smb_server *server, const char *opath, const int olen, 
		const char *npath, const int nlen);
int smb_proc_mkdir(struct smb_server *server, const char *path, const int len);
int smb_proc_rmdir(struct smb_server *server, const char *path, const int len);
int smb_proc_unlink(struct smb_server *server, const char *path,
                    const int len);
int smb_proc_readdir(struct smb_server *server, struct inode *dir,
                     int fpos, int cache_size, 
		     struct smb_dirent *entry);
int smb_proc_getattr(struct smb_server *server, const char *path,
                     int len, struct smb_dirent *entry);
int smb_proc_setattr(struct smb_server *server,
                     struct inode *ino,
                     struct smb_dirent *new_finfo);
int smb_proc_chkpath(struct smb_server *server, char *path, int len,
                     int *result);
int smb_proc_dskattr(struct super_block *super, struct smb_dskattr *attr);
int smb_proc_reconnect(struct smb_server *server);
int smb_proc_connect(struct smb_server *server);
int smb_proc_disconnect(struct smb_server *server);
int smb_proc_trunc(struct smb_server *server, word fid, dword length);

/* linux/fs/smbfs/sock.c */
int smb_release(struct smb_server *server);
int smb_connect(struct smb_server *server);
int smb_request(struct smb_server *server);
int smb_request_read_raw(struct smb_server *server,
                         unsigned char *target, int max_len);
int smb_request_write_raw(struct smb_server *server,
                          unsigned const char *source, int length);
int smb_catch_keepalive(struct smb_server *server);
int smb_dont_catch_keepalive(struct smb_server *server);
int smb_trans2_request(struct smb_server *server,
                       int *data_len, int *param_len,
                       char **data, char **param);

/* linux/fs/smbfs/mmap.c */
int smb_mmap(struct inode * inode, struct file * file, struct vm_area_struct * vma);

#endif /* __KERNEL__ */

#endif /* _LINUX_SMB_FS_H */

/*
 *  smb_fs.h
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 */

#ifndef _LINUX_SMB_FS_H
#define _LINUX_SMB_FS_H

#include <linux/smb.h>

/*
 * ioctl commands
 */
#define	SMB_IOC_GETMOUNTUID		_IOR('u', 1, uid_t)
#define SMB_IOC_NEWCONN                 _IOW('u', 2, struct smb_conn_opt)

#ifdef __KERNEL__

#include <asm/unaligned.h>

#define WVAL(buf,pos) \
(le16_to_cpu(get_unaligned((__u16 *)((__u8 *)(buf) + (pos)))))
#define DVAL(buf,pos) \
(le32_to_cpu(get_unaligned((__u32 *)((__u8 *)(buf) + (pos)))))
#define WSET(buf,pos,val) \
put_unaligned(cpu_to_le16((__u16)(val)), (__u16 *)((__u8 *)(buf) + (pos)))
#define DSET(buf,pos,val) \
put_unaligned(cpu_to_le32((__u32)(val)), (__u32 *)((__u8 *)(buf) + (pos)))

/* where to find the base of the SMB packet proper */
#define smb_base(buf) ((__u8 *)(((__u8 *)(buf))+4))

#include <linux/vmalloc.h>

#ifdef DEBUG_SMB_MALLOC

extern int smb_malloced;
extern int smb_current_vmalloced;

static inline void *
smb_vmalloc(unsigned int size)
{
        smb_malloced += 1;
        smb_current_vmalloced += 1;
        return vmalloc(size);
}

static inline void
smb_vfree(void *obj)
{
        smb_current_vmalloced -= 1;
        vfree(obj);
}

#else /* DEBUG_SMB_MALLOC */

#define smb_kmalloc(s,p) kmalloc(s,p)
#define smb_kfree_s(o,s) kfree_s(o,s)
#define smb_vmalloc(s)   vmalloc(s)
#define smb_vfree(o)     vfree(o)

#endif /* DEBUG_SMB_MALLOC */

struct smb_sb_info;

/* linux/fs/smbfs/file.c */
extern struct inode_operations smb_file_inode_operations;

/* linux/fs/smbfs/dir.c */
extern struct inode_operations smb_dir_inode_operations;
struct smb_inode_info *smb_find_inode(struct smb_sb_info *server, ino_t ino);
void smb_free_inode_info(struct smb_inode_info *i);
void smb_free_all_inodes(struct smb_sb_info *server);
void smb_init_root(struct smb_sb_info *server);
int  smb_stat_root(struct smb_sb_info *server);
void smb_init_dir_cache(void);
void smb_invalid_dir_cache(struct inode *);
void smb_free_dir_cache(void);

/* linux/fs/smbfs/ioctl.c */
int smb_ioctl (struct inode * inode, struct file * filp,
               unsigned int cmd, unsigned long arg);

/* linux/fs/smbfs/inode.c */
struct super_block *smb_read_super(struct super_block *sb,
                                   void *raw_data, int silent);
extern int init_smb_fs(void);
void smb_invalidate_inodes(struct smb_sb_info *server);
int smb_revalidate_inode(struct inode *i);
int smb_refresh_inode(struct inode *i);
int smb_notify_change(struct inode *inode, struct iattr *attr);
void smb_invalidate_connection(struct smb_sb_info *server);
int smb_conn_is_valid(struct smb_sb_info *server);
unsigned long smb_invent_inos(unsigned long n);
struct inode *smb_iget(struct super_block *, struct smb_fattr *);

/* linux/fs/smbfs/proc.c */
__u32 smb_len(unsigned char *packet);
__u8 *smb_encode_smb_length(__u8 *p, __u32 len);
__u8 *smb_setup_header(struct smb_sb_info *server, __u8 command,
		       __u16 wct, __u16 bcc);
int smb_offerconn(struct smb_sb_info *server);
int smb_newconn(struct smb_sb_info *server, struct smb_conn_opt *opt);
int smb_close(struct inode *);
int smb_open(struct dentry *, int);
static inline int
smb_is_open(struct inode *i)
{
	return (i->u.smbfs_i.open == SMB_SERVER(i)->generation);
}

int smb_proc_read(struct inode *, off_t, long, char *);
int smb_proc_write(struct inode *, off_t, int, const char *);
int smb_proc_create(struct dentry *, struct qstr *, __u16, time_t);
int smb_proc_mv(struct dentry *, struct qstr *, struct dentry *, struct qstr *);
int smb_proc_mkdir(struct dentry *, struct qstr *);
int smb_proc_rmdir(struct dentry *, struct qstr *);
int smb_proc_unlink(struct dentry *dir, struct qstr *);
int smb_proc_readdir(struct dentry *dir, int fpos, int cache_size, struct smb_dirent *entry);
int smb_proc_getattr(struct dentry *dir, struct qstr *name,
		     struct smb_fattr *entry);
int smb_proc_setattr(struct smb_sb_info *server,
                     struct dentry *dir,
                     struct smb_fattr *new_finfo);
int smb_proc_dskattr(struct super_block *sb, struct statfs *attr);
int smb_proc_reconnect(struct smb_sb_info *server);
int smb_proc_connect(struct smb_sb_info *server);
int smb_proc_disconnect(struct smb_sb_info *server);
int smb_proc_trunc(struct smb_sb_info *server, __u16 fid, __u32 length);
void smb_init_root_dirent(struct smb_sb_info *server, struct smb_fattr *);

/* linux/fs/smbfs/sock.c */
int smb_release(struct smb_sb_info *server);
int smb_connect(struct smb_sb_info *server);
int smb_request(struct smb_sb_info *server);
int smb_request_read_raw(struct smb_sb_info *server,
                         unsigned char *target, int max_len);
int smb_request_write_raw(struct smb_sb_info *server,
                          unsigned const char *source, int length);
int smb_catch_keepalive(struct smb_sb_info *server);
int smb_dont_catch_keepalive(struct smb_sb_info *server);
int smb_trans2_request(struct smb_sb_info *server, __u16 trans2_command,
		       int ldata, unsigned char *data,
		       int lparam, unsigned char *param,
		       int *lrdata, unsigned char **rdata,
		       int *lrparam, unsigned char **rparam);

/* linux/fs/smbfs/mmap.c */
int smb_mmap(struct file * file, struct vm_area_struct * vma);

#endif /* __KERNEL__ */

#endif /* _LINUX_SMB_FS_H */

/*
 *  ncp_fs.h
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *
 */

#ifndef _LINUX_NCP_FS_H
#define _LINUX_NCP_FS_H

#include <linux/fs.h>
#include <linux/in.h>
#include <linux/types.h>

#include <linux/ncp_mount.h>
#include <linux/ncp_fs_sb.h>
#include <linux/ncp_fs_i.h>

/*
 * ioctl commands
 */

struct ncp_ioctl_request {
	unsigned int   function;
	unsigned int   size;
	char          *data;
};

struct ncp_fs_info {
	int    version;
	struct sockaddr_ipx addr;
	uid_t  mounted_uid;
	int    connection;	/* Connection number the server assigned us */
	int    buffer_size;	/* The negotiated buffer size, to be
				   used for read/write requests! */

	int    volume_number;
	__u32  directory_id;
};	

#define	NCP_IOC_NCPREQUEST		_IOR('n', 1, struct ncp_ioctl_request)
#define	NCP_IOC_GETMOUNTUID		_IOW('n', 2, uid_t)
#define NCP_IOC_CONN_LOGGED_IN          _IO('n', 3)

#define NCP_GET_FS_INFO_VERSION (1)
#define NCP_IOC_GET_FS_INFO             _IOWR('n', 4, struct ncp_fs_info)

/*
 * The packet size to allocate. One page should be enough.
 */
#define NCP_PACKET_SIZE 4070

#define NCP_MAXPATHLEN 255
#define NCP_MAXNAMELEN 14

#define NCP_MSG_COMMAND "/sbin/nwmsg"

#ifdef __KERNEL__

/* The readdir cache size controls how many directory entries are
 * cached.
 */
#define NCP_READDIR_CACHE_SIZE        64


#define NCP_MAX_RPC_TIMEOUT (6*HZ)

/* Guess, what 0x564c is :-) */
#define NCP_SUPER_MAGIC  0x564c


#define NCP_SBP(sb)          ((struct ncp_server *)((sb)->u.generic_sbp))
#define NCP_INOP(inode)      ((struct ncp_inode_info *)((inode)->u.generic_ip))

#define NCP_SERVER(inode)    NCP_SBP((inode)->i_sb)
#define NCP_FINFO(inode)     (&(NCP_INOP(inode)->finfo))
#define NCP_ISTRUCT(inode)   (&(NCP_FINFO(inode)->i))

#ifdef DEBUG_NCP_MALLOC

#include <linux/malloc.h>

extern int ncp_malloced;
extern int ncp_current_malloced;

static inline void *
ncp_kmalloc(unsigned int size, int priority)
{
        ncp_malloced += 1;
        ncp_current_malloced += 1;
        return kmalloc(size, priority);
}

static inline void
ncp_kfree_s(void *obj, int size)
{
        ncp_current_malloced -= 1;
        kfree_s(obj, size);
}

#else /* DEBUG_NCP_MALLOC */

#define ncp_kmalloc(s,p) kmalloc(s,p)
#define ncp_kfree_s(o,s) kfree_s(o,s)

#endif /* DEBUG_NCP_MALLOC */

#if DEBUG_NCP > 0
#define DPRINTK(format, args...) printk(format , ## args)
#else
#define DPRINTK(format, args...)
#endif

#if DEBUG_NCP > 1
#define DDPRINTK(format, args...) printk(format , ## args)
#else
#define DDPRINTK(format, args...)
#endif


/* linux/fs/ncpfs/file.c */
extern struct inode_operations ncp_file_inode_operations;
int ncp_make_open(struct inode *i, int right);

/* linux/fs/ncpfs/dir.c */
extern struct inode_operations ncp_dir_inode_operations;
void ncp_free_inode_info(struct ncp_inode_info *i);
void ncp_free_all_inodes(struct ncp_server *server);
void ncp_init_root(struct ncp_server *server);
int  ncp_conn_logged_in(struct ncp_server *server);
void ncp_init_dir_cache(void);
void ncp_invalid_dir_cache(struct inode *ino);
struct ncp_inode_info *ncp_find_inode(struct inode *inode);
ino_t ncp_info_ino(struct ncp_server *server, struct ncp_inode_info *info);
void ncp_free_dir_cache(void);
int  ncp_date_dos2unix(__u16 time, __u16 date);
void ncp_date_unix2dos(int unix_date, __u16 *time, __u16 *date);


/* linux/fs/ncpfs/ioctl.c */
int ncp_ioctl (struct inode * inode, struct file * filp,
               unsigned int cmd, unsigned long arg);

/* linux/fs/ncpfs/inode.c */
struct super_block *ncp_read_super(struct super_block *sb,
                                   void *raw_data, int silent);
extern int init_ncp_fs(void);
void ncp_trigger_message(struct ncp_server *server);

/* linux/fs/ncpfs/sock.c */
int ncp_request(struct ncp_server *server, int function);
int ncp_connect(struct ncp_server *server);
int ncp_disconnect(struct ncp_server *server);
int ncp_catch_watchdog(struct ncp_server *server);
int ncp_dont_catch_watchdog(struct ncp_server *server);
int ncp_catch_message(struct ncp_server *server);
void ncp_lock_server(struct ncp_server *server);
void ncp_unlock_server(struct ncp_server *server);

/* linux/fs/ncpfs/mmap.c */
int ncp_mmap(struct inode * inode, struct file * file, struct vm_area_struct * vma);

#endif /* __KERNEL__ */

#endif /* _LINUX_NCP_FS_H */

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
	unsigned int function;
	unsigned int size;
	char *data;
};

struct ncp_fs_info {
	int version;
	struct sockaddr_ipx addr;
	uid_t mounted_uid;
	int connection;		/* Connection number the server assigned us */
	int buffer_size;	/* The negotiated buffer size, to be
				   used for read/write requests! */

	int volume_number;
	__u32 directory_id;
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

#ifdef __KERNEL__

#undef NCPFS_PARANOIA
#define DEBUG_NCP 0
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

/* The readdir cache size controls how many directory entries are
 * cached.
 */
#define NCP_READDIR_CACHE_SIZE        64

#define NCP_MAX_RPC_TIMEOUT (6*HZ)

/*
 * This is the ncpfs part of the inode structure. This must contain
 * all the information we need to work with an inode after creation.
 * (Move to ncp_fs_i.h once it stabilizes, and add a union in fs.h)
 */
struct ncpfs_i {
	__u32	dirEntNum __attribute__((packed));
	__u32	DosDirNum __attribute__((packed));
	__u32	volNumber __attribute__((packed));
	int	opened;
	int	access;
	__u32	server_file_handle __attribute__((packed));
	__u8	open_create_action __attribute__((packed));
	__u8	file_handle[6] __attribute__((packed));
};

/*
 * This is an extension of the nw_file_info structure with
 * the additional information we need to create an inode.
 */
struct ncpfs_inode_info {
	ino_t	ino;		/* dummy inode number */
	struct nw_file_info nw_info;
};

/* Guess, what 0x564c is :-) */
#define NCP_SUPER_MAGIC  0x564c


#define NCP_SBP(sb)          ((struct ncp_server *)((sb)->u.generic_sbp))

#define NCP_SERVER(inode)    NCP_SBP((inode)->i_sb)
/* We don't have an ncpfs union yet, so use smbfs ... */
#define NCP_FINFO(inode)     ((struct ncpfs_i *)&((inode)->u.smbfs_i))

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

static inline void ncp_kfree_s(void *obj, int size)
{
	ncp_current_malloced -= 1;
	kfree_s(obj, size);
}

#else				/* DEBUG_NCP_MALLOC */

#define ncp_kmalloc(s,p) kmalloc(s,p)
#define ncp_kfree_s(o,s) kfree_s(o,s)

#endif				/* DEBUG_NCP_MALLOC */

/* linux/fs/ncpfs/inode.c */
struct super_block *ncp_read_super(struct super_block *, void *, int);
struct inode *ncp_iget(struct super_block *, struct ncpfs_inode_info *);
void ncp_update_inode(struct inode *, struct nw_file_info *);
extern int init_ncp_fs(void);

/* linux/fs/ncpfs/dir.c */
extern struct inode_operations ncp_dir_inode_operations;
int ncp_conn_logged_in(struct ncp_server *);
void ncp_init_dir_cache(void);
void ncp_invalid_dir_cache(struct inode *);
void ncp_free_dir_cache(void);
int ncp_date_dos2unix(__u16 time, __u16 date);
void ncp_date_unix2dos(int unix_date, __u16 * time, __u16 * date);

/* linux/fs/ncpfs/ioctl.c */
int ncp_ioctl(struct inode *, struct file *, unsigned int, unsigned long);

/* linux/fs/ncpfs/sock.c */
int ncp_request(struct ncp_server *server, int function);
int ncp_connect(struct ncp_server *server);
int ncp_disconnect(struct ncp_server *server);
void ncp_lock_server(struct ncp_server *server);
void ncp_unlock_server(struct ncp_server *server);

/* linux/fs/ncpfs/file.c */
extern struct inode_operations ncp_file_inode_operations;
int ncp_make_open(struct inode *, int);

/* linux/fs/ncpfs/mmap.c */
int ncp_mmap(struct file *, struct vm_area_struct *);

/* linux/fs/ncpfs/ncplib_kernel.c */
int ncp_make_closed(struct inode *);

static inline void str_upper(char *name)
{
	while (*name) {
		if (*name >= 'a' && *name <= 'z') {
			*name -= ('a' - 'A');
		}
		name++;
	}
}

static inline void str_lower(char *name)
{
	while (*name) {
		if (*name >= 'A' && *name <= 'Z') {
			*name += ('a' - 'A');
		}
		name++;
	}
}

static inline int ncp_namespace(struct inode *inode)
{
	struct ncp_server *server = NCP_SERVER(inode);
	return server->name_space[NCP_FINFO(inode)->volNumber];
}

static inline int ncp_preserve_case(struct inode *i)
{
       	return (ncp_namespace(i) == NW_NS_OS2);
}

static inline int ncp_case_sensitive(struct inode *i)
{
	return 0;
} 

#endif				/* __KERNEL__ */

#endif				/* _LINUX_NCP_FS_H */

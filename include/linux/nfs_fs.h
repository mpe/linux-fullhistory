/*
 *  linux/include/linux/nfs_fs.h
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  OS-specific nfs filesystem definitions and declarations
 */

#ifndef _LINUX_NFS_FS_H
#define _LINUX_NFS_FS_H

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/in.h>

#include <linux/sunrpc/debug.h>
#include <linux/nfs.h>
#include <linux/nfs_mount.h>

/*
 * Enable debugging support for nfs client.
 * Requires RPC_DEBUG.
 */
#ifdef RPC_DEBUG
# define NFS_DEBUG
#endif

/*
 * NFS_MAX_DIRCACHE controls the number of simultaneously cached
 * directory chunks. Each chunk holds the list of nfs_entry's returned
 * in a single readdir call in a memory region of size PAGE_SIZE.
 *
 * Note that at most server->rsize bytes of the cache memory are used.
 */
#define NFS_MAX_DIRCACHE		16

#define NFS_MAX_FILE_IO_BUFFER_SIZE	16384
#define NFS_DEF_FILE_IO_BUFFER_SIZE	4096

/*
 * The upper limit on timeouts for the exponential backoff algorithm.
 */
#define NFS_MAX_RPC_TIMEOUT		(6*HZ)

/*
 * Size of the lookup cache in units of number of entries cached.
 * It is better not to make this too large although the optimum
 * depends on a usage and environment.
 */
#define NFS_LOOKUP_CACHE_SIZE		64

/*
 * superblock magic number for NFS
 */
#define NFS_SUPER_MAGIC			0x6969

#define NFS_FH(dentry)			((struct nfs_fh *) ((dentry)->d_fsdata))
#define NFS_DSERVER(dentry)		(&(dentry)->d_sb->u.nfs_sb.s_server)
#define NFS_SERVER(inode)		(&(inode)->i_sb->u.nfs_sb.s_server)
#define NFS_CLIENT(inode)		(NFS_SERVER(inode)->client)
#define NFS_ADDR(inode)			(RPC_PEERADDR(NFS_CLIENT(inode)))
#define NFS_CONGESTED(inode)		(RPC_CONGESTED(NFS_CLIENT(inode)))

#define NFS_READTIME(inode)		((inode)->u.nfs_i.read_cache_jiffies)
#define NFS_OLDMTIME(inode)		((inode)->u.nfs_i.read_cache_mtime)
#define NFS_CACHEINV(inode) \
do { \
	NFS_READTIME(inode) = jiffies - 1000000; \
	NFS_OLDMTIME(inode) = 0; \
} while (0)
#define NFS_ATTRTIMEO(inode)		((inode)->u.nfs_i.attrtimeo)
#define NFS_MINATTRTIMEO(inode) \
	(S_ISDIR(inode->i_mode)? NFS_SERVER(inode)->acdirmin \
			       : NFS_SERVER(inode)->acregmin)
#define NFS_MAXATTRTIMEO(inode) \
	(S_ISDIR(inode->i_mode)? NFS_SERVER(inode)->acdirmax \
			       : NFS_SERVER(inode)->acregmax)

#define NFS_FLAGS(inode)		((inode)->u.nfs_i.flags)
#define NFS_REVALIDATING(inode)		(NFS_FLAGS(inode) & NFS_INO_REVALIDATE)

#define NFS_RENAMED_DIR(inode)		((inode)->u.nfs_i.silly_inode)
#define NFS_WRITEBACK(inode)		((inode)->u.nfs_i.writeback)

/*
 * These are the default flags for swap requests
 */
#define NFS_RPC_SWAPFLAGS		(RPC_TASK_SWAPPER|RPC_TASK_ROOTCREDS)

#ifdef __KERNEL__

/*
 * linux/fs/nfs/proc.c
 */
extern int nfs_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fattr *fattr);
extern int nfs_proc_setattr(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_sattr *sattr, struct nfs_fattr *fattr);
extern int nfs_proc_lookup(struct nfs_server *server, struct nfs_fh *dir,
			const char *name, struct nfs_fh *fhandle,
			struct nfs_fattr *fattr);
extern int nfs_proc_readlink(struct nfs_server *server, struct nfs_fh *fhandle,
			void **p0, char **string, unsigned int *len,
			unsigned int maxlen);
extern int nfs_proc_read(struct nfs_server *server, struct nfs_fh *fhandle,
			int swap, unsigned long offset, unsigned int count,
			void *buffer, struct nfs_fattr *fattr);
extern int nfs_proc_write(struct nfs_server *server, struct nfs_fh *fhandle,
			int swap, unsigned long offset, unsigned int count,
			const void *buffer, struct nfs_fattr *fattr);
extern int nfs_proc_create(struct nfs_server *server, struct nfs_fh *dir,
			const char *name, struct nfs_sattr *sattr,
			struct nfs_fh *fhandle, struct nfs_fattr *fattr);
extern int nfs_proc_remove(struct nfs_server *server, struct nfs_fh *dir,
			const char *name);
extern int nfs_proc_rename(struct nfs_server *server,
			struct nfs_fh *old_dir, const char *old_name,
			struct nfs_fh *new_dir, const char *new_name);
extern int nfs_proc_link(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fh *dir, const char *name);
extern int nfs_proc_symlink(struct nfs_server *server, struct nfs_fh *dir,
			const char *name, const char *path,
			struct nfs_sattr *sattr);
extern int nfs_proc_mkdir(struct nfs_server *server, struct nfs_fh *dir,
			const char *name, struct nfs_sattr *sattr,
			struct nfs_fh *fhandle, struct nfs_fattr *fattr);
extern int nfs_proc_rmdir(struct nfs_server *server, struct nfs_fh *dir,
			const char *name);
extern int nfs_proc_readdir(struct nfs_server *server, struct nfs_fh *fhandle,
			u32 cookie, unsigned int size, __u32 *entry);
extern int nfs_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fsinfo *res);


/*
 * linux/fs/nfs/inode.c
 */
extern struct super_block *nfs_read_super(struct super_block *, void *, int);
extern int init_nfs_fs(void);
extern struct inode *nfs_fhget(struct super_block *, struct nfs_fh *,
			       struct nfs_fattr *);
extern int nfs_refresh_inode(struct inode *, struct nfs_fattr *);
extern int nfs_revalidate(struct dentry *);
extern int _nfs_revalidate_inode(struct nfs_server *, struct dentry *);

/*
 * linux/fs/nfs/file.c
 */
extern struct inode_operations nfs_file_inode_operations;

/*
 * linux/fs/nfs/dir.c
 */
extern struct inode_operations nfs_dir_inode_operations;
extern struct dentry_operations nfs_dentry_operations;
extern void nfs_free_dircache(void);
extern void nfs_invalidate_dircache(struct inode *);
extern void nfs_invalidate_dircache_sb(struct super_block *);

/*
 * linux/fs/nfs/symlink.c
 */
extern struct inode_operations nfs_symlink_inode_operations;

/*
 * linux/fs/nfs/locks.c
 */
extern int nfs_lock(struct file *, int, struct file_lock *);

/*
 * linux/fs/nfs/write.c
 */
extern int  nfs_writepage(struct dentry *, struct page *);
extern int  nfs_check_failed_request(struct inode *);
extern int  nfs_check_error(struct inode *);
extern int  nfs_flush_dirty_pages(struct inode *, pid_t, off_t, off_t);
extern int  nfs_truncate_dirty_pages(struct inode *, unsigned long);
extern void nfs_invalidate_pages(struct inode *);
extern int  nfs_updatepage(struct dentry *, struct page *, const char *,
			unsigned long, unsigned int, int);

/*
 * linux/fs/nfs/read.c
 */
extern int  nfs_readpage(struct dentry *, struct page *);

/*
 * linux/fs/mount_clnt.c
 * (Used only by nfsroot module)
 */
extern int  nfs_mount(struct sockaddr_in *, char *, struct nfs_fh *);

/*
 * inline functions
 */
static inline int
nfs_revalidate_inode(struct nfs_server *server, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	if (jiffies - NFS_READTIME(inode) < NFS_ATTRTIMEO(inode))
		return 0;
	return _nfs_revalidate_inode(server, dentry);
}

extern struct nfs_wreq *	nfs_failed_requests;
static inline int
nfs_write_error(struct inode *inode)
{
	if (nfs_failed_requests == NULL)
		return 0;
	return nfs_check_error(inode);
}

/* NFS root */

extern int nfs_root_mount(struct super_block *sb);

#endif /* __KERNEL__ */

/*
 * NFS debug flags
 */
#define NFSDBG_VFS		0x0001
#define NFSDBG_DIRCACHE		0x0002
#define NFSDBG_LOOKUPCACHE	0x0004
#define NFSDBG_PAGECACHE	0x0008
#define NFSDBG_PROC		0x0010
#define NFSDBG_XDR		0x0020
#define NFSDBG_FILE		0x0040
#define NFSDBG_ROOT		0x0080
#define NFSDBG_ALL		0xFFFF

#ifdef __KERNEL__
# undef ifdebug
# ifdef NFS_DEBUG
#  define ifdebug(fac)		if (nfs_debug & NFSDBG_##fac)
# else
#  define ifdebug(fac)		if (0)
# endif
#endif /* __KERNEL */

#endif

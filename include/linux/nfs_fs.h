/*
 *  linux/include/linux/nfs_fs.h
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  OS-specific nfs filesystem definitions and declarations
 */

#ifndef _LINUX_NFS_FS_H
#define _LINUX_NFS_FS_H

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/in.h>

#include <linux/sunrpc/sched.h>
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

#define NFS_MAX_FILE_IO_BUFFER_SIZE	32768
#define NFS_DEF_FILE_IO_BUFFER_SIZE	4096

/*
 * The upper limit on timeouts for the exponential backoff algorithm.
 */
#define NFS_MAX_RPC_TIMEOUT		(6*HZ)
#define NFS_WRITEBACK_DELAY		(5*HZ)
#define NFS_WRITEBACK_LOCKDELAY		(60*HZ)
#define NFS_COMMIT_DELAY		(5*HZ)

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
#define NFS_REQUESTLIST(inode)		(NFS_SERVER(inode)->rw_requests)
#define NFS_ADDR(inode)			(RPC_PEERADDR(NFS_CLIENT(inode)))
#define NFS_CONGESTED(inode)		(RPC_CONGESTED(NFS_CLIENT(inode)))

#define NFS_READTIME(inode)		((inode)->u.nfs_i.read_cache_jiffies)
#define NFS_OLDMTIME(inode)		((inode)->u.nfs_i.read_cache_mtime)
#define NFS_NEXTSCAN(inode)		((inode)->u.nfs_i.nextscan)
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
#define NFS_REVALIDATING(inode)		(NFS_FLAGS(inode) & NFS_INO_REVALIDATING)
#define NFS_COOKIES(inode)		((inode)->u.nfs_i.cookies)
#define NFS_DIREOF(inode)		((inode)->u.nfs_i.direof)

#define NFS_FILEID(inode)		((inode)->u.nfs_i.fileid)
#define NFS_FSID(inode)			((inode)->u.nfs_i.fsid)

/*
 * These are the default flags for swap requests
 */
#define NFS_RPC_SWAPFLAGS		(RPC_TASK_SWAPPER|RPC_TASK_ROOTCREDS)

/* Flags in the RPC client structure */
#define NFS_CLNTF_BUFSIZE	0x0001	/* readdir buffer in longwords */

#define NFS_RW_SYNC		0x0001	/* O_SYNC handling */
#define NFS_RW_SWAP		0x0002	/* This is a swap request */

/*
 * When flushing a cluster of dirty pages, there can be different
 * strategies:
 */
#define FLUSH_AGING		0	/* only flush old buffers */
#define FLUSH_SYNC		1	/* file being synced, or contention */
#define FLUSH_WAIT		2	/* wait for completion */
#define FLUSH_STABLE		4	/* commit to stable storage */

static inline
loff_t page_offset(struct page *page)
{
	return ((loff_t)page->index) << PAGE_CACHE_SHIFT;
}

static inline
unsigned long page_index(struct page *page)
{
	return page->index;
}

#ifdef __KERNEL__

/*
 * linux/fs/nfs/proc.c
 */
extern int nfs_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fattr *fattr);
extern int nfs_proc_setattr(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fattr *fattr, struct iattr *sattr);
extern int nfs_proc_lookup(struct nfs_server *server, struct nfs_fh *dir,
			const char *name, struct nfs_fh *fhandle,
			struct nfs_fattr *fattr);
extern int nfs_proc_read(struct nfs_server *server, struct nfs_fh *fhandle,
			int swap, unsigned long offset, unsigned int count,
			void *buffer, struct nfs_fattr *fattr);
extern int nfs_proc_write(struct nfs_server *server, struct nfs_fh *fhandle,
			int swap, unsigned long offset, unsigned int count,
			const void *buffer, struct nfs_fattr *fattr);
extern int nfs_proc_create(struct nfs_server *server, struct nfs_fh *dir,
			const char *name, struct iattr *sattr,
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
			struct iattr *sattr);
extern int nfs_proc_mkdir(struct nfs_server *server, struct nfs_fh *dir,
			const char *name, struct iattr *sattr,
			struct nfs_fh *fhandle, struct nfs_fattr *fattr);
extern int nfs_proc_rmdir(struct nfs_server *server, struct nfs_fh *dir,
			const char *name);
extern int nfs_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fsinfo *res);


/*
 * linux/fs/nfs/inode.c
 */
extern struct super_block *nfs_read_super(struct super_block *, void *, int);
extern int init_nfs_fs(void);
extern void nfs_zap_caches(struct inode *);
extern struct inode *nfs_fhget(struct dentry *, struct nfs_fh *,
				struct nfs_fattr *);
extern int nfs_refresh_inode(struct inode *, struct nfs_fattr *);
extern int nfs_revalidate(struct dentry *);
extern int nfs_open(struct inode *, struct file *);
extern int nfs_release(struct inode *, struct file *);
extern int __nfs_revalidate_inode(struct nfs_server *, struct dentry *);
extern int nfs_notify_change(struct dentry *, struct iattr *);

/*
 * linux/fs/nfs/file.c
 */
extern struct inode_operations nfs_file_inode_operations;
extern struct file_operations nfs_file_operations;
extern struct address_space_operations nfs_file_aops;

/*
 * linux/fs/nfs/dir.c
 */
extern struct inode_operations nfs_dir_inode_operations;
extern struct file_operations nfs_dir_operations;
extern struct dentry_operations nfs_dentry_operations;
extern void nfs_flush_dircache(struct inode *);
extern void nfs_free_dircache(struct inode *);

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
extern struct nfs_page* nfs_find_request(struct inode *, struct page *);
extern void nfs_release_request(struct nfs_page *req);
extern int  nfs_flush_incompatible(struct file *file, struct page *page);
extern int  nfs_updatepage(struct file *, struct page *, unsigned long, unsigned int);
/*
 * Try to write back everything synchronously (but check the
 * return value!)
 */
extern int  nfs_sync_file(struct inode *, struct file *, unsigned long, unsigned int, int);
extern int  nfs_flush_file(struct inode *, struct file *, unsigned long, unsigned int, int);
extern int  nfs_flush_timeout(struct inode *, int);
#ifdef CONFIG_NFS_V3
extern int  nfs_commit_file(struct inode *, struct file *, unsigned long, unsigned int, int);
extern int  nfs_commit_timeout(struct inode *, int);
#endif

static inline int
nfs_have_writebacks(struct inode *inode)
{
	return !list_empty(&inode->u.nfs_i.writeback);
}

static inline int
nfs_wb_all(struct inode *inode)
{
	int error = nfs_sync_file(inode, 0, 0, 0, FLUSH_WAIT);
	return (error < 0) ? error : 0;
}

/*
 * Write back all requests on one page - we do this before reading it.
 */
static inline int
nfs_wb_page(struct inode *inode, struct page* page)
{
	int error = nfs_sync_file(inode, 0, page_offset(page), PAGE_CACHE_SIZE, FLUSH_WAIT | FLUSH_STABLE);
	return (error < 0) ? error : 0;
}

/*
 * Write back all pending writes for one user.. 
 */
static inline int
nfs_wb_file(struct inode *inode, struct file *file)
{
	int error = nfs_sync_file(inode, file, 0, 0, FLUSH_WAIT);
	return (error < 0) ? error : 0;
}

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
	if (time_before(jiffies, NFS_READTIME(inode)+NFS_ATTRTIMEO(inode)))
		return 0;
	return __nfs_revalidate_inode(server, dentry);
}

/* NFS root */

extern int nfs_root_mount(struct super_block *sb);

#define nfs_wait_event(clnt, wq, condition)				\
({									\
	int __retval = 0;						\
	if (clnt->cl_intr) {						\
		sigset_t oldmask;					\
		rpc_clnt_sigmask(clnt, &oldmask);			\
		__retval = wait_event_interruptible(wq, condition);	\
		rpc_clnt_sigunmask(clnt, &oldmask);			\
	} else								\
		wait_event(wq, condition);				\
	__retval;							\
})

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

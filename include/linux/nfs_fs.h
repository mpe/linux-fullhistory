#ifndef _LINUX_NFS_FS_H
#define _LINUX_NFS_FS_H

/*
 *  linux/include/linux/nfs_fs.h
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  OS-specific nfs filesystem definitions and declarations
 */

#include <linux/nfs.h>

#include <linux/in.h>
#include <linux/nfs_mount.h>

/*
 * The readdir cache size controls how many directory entries are cached.
 * Its size is limited by the number of nfs_entry structures that can fit
 * in one page, currently, the limit is 256 when using 4KB pages.
 */

#define NFS_READDIR_CACHE_SIZE		64

#define NFS_MAX_FILE_IO_BUFFER_SIZE	16384
#define NFS_DEF_FILE_IO_BUFFER_SIZE	1024

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

#define NFS_SUPER_MAGIC			0x6969

#define NFS_SERVER(inode)		(&(inode)->i_sb->u.nfs_sb.s_server)
#define NFS_FH(inode)			(&(inode)->u.nfs_i.fhandle)
#define NFS_RENAMED_DIR(inode)		((inode)->u.nfs_i.silly_rename_dir)
#define NFS_READTIME(inode)		((inode)->u.nfs_i.read_cache_jiffies)
#define NFS_OLDMTIME(inode)		((inode)->u.nfs_i.read_cache_mtime)
#define NFS_CACHEINV(inode) \
do { \
	NFS_READTIME(inode) = jiffies - 1000000; \
	NFS_OLDMTIME(inode) = 0; \
} while (0)


#ifdef __KERNEL__

/* linux/fs/nfs/proc.c */

extern int nfs_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle,
			    struct nfs_fattr *fattr);
extern int nfs_proc_setattr(struct nfs_server *server, struct nfs_fh *fhandle,
			    struct nfs_sattr *sattr, struct nfs_fattr *fattr);
extern int nfs_proc_lookup(struct nfs_server *server, struct nfs_fh *dir,
			   const char *name, struct nfs_fh *fhandle,
			   struct nfs_fattr *fattr);
extern int nfs_proc_readlink(struct nfs_server *server, struct nfs_fh *fhandle,
			int **p0, char **string, unsigned int *len,
			unsigned int maxlen);
extern int nfs_proc_read(struct nfs_server *server, struct nfs_fh *fhandle,
			 int offset, int count, char *data,
			 struct nfs_fattr *fattr);
extern int nfs_proc_write(struct inode * inode, int offset,
			  int count, const char *data, struct nfs_fattr *fattr);
extern int nfs_proc_create(struct nfs_server *server, struct nfs_fh *dir,
			   const char *name, struct nfs_sattr *sattr,
			   struct nfs_fh *fhandle, struct nfs_fattr *fattr);
extern int nfs_proc_remove(struct nfs_server *server, struct nfs_fh *dir,
			   const char *name);
extern int nfs_proc_rename(struct nfs_server *server,
			   struct nfs_fh *old_dir, const char *old_name,
			   struct nfs_fh *new_dir, const char *new_name,
			   int must_be_dir);
extern int nfs_proc_link(struct nfs_server *server, struct nfs_fh *fhandle,
			 struct nfs_fh *dir, const char *name);
extern int nfs_proc_symlink(struct nfs_server *server, struct nfs_fh *dir,
			    const char *name, const char *path, struct nfs_sattr *sattr);
extern int nfs_proc_mkdir(struct nfs_server *server, struct nfs_fh *dir,
			  const char *name, struct nfs_sattr *sattr,
			  struct nfs_fh *fhandle, struct nfs_fattr *fattr);
extern int nfs_proc_rmdir(struct nfs_server *server, struct nfs_fh *dir,
			  const char *name);
extern int nfs_proc_readdir(struct nfs_server *server, struct nfs_fh *fhandle,
			    int cookie, int count, struct nfs_entry *entry);
extern int nfs_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle,
			    struct nfs_fsinfo *res);
extern int nfs_proc_read_request(struct rpc_ioreq *, struct nfs_server *,
				 struct nfs_fh *, unsigned long offset,
				 unsigned long count, __u32 *buf);
extern int nfs_proc_read_reply(struct rpc_ioreq *, struct nfs_fattr *);
extern int *rpc_header(int *p, int procedure, int program, int version,
				int uid, int gid, int *groups);
extern int *rpc_verify(int *p);

/* linux/fs/nfs/sock.c */

extern int nfs_rpc_call(struct nfs_server *server, int *start,
				int *end, int size);
extern int nfs_rpc_doio(struct nfs_server *server, struct rpc_ioreq *,
				int async);

/* linux/fs/nfs/inode.c */

extern struct super_block *nfs_read_super(struct super_block *sb, 
					  void *data,int);
extern int init_nfs_fs(void);
extern struct inode *nfs_fhget(struct super_block *sb, struct nfs_fh *fhandle,
			       struct nfs_fattr *fattr);
extern void nfs_refresh_inode(struct inode *inode, struct nfs_fattr *fattr);

/* linux/fs/nfs/file.c */

extern struct inode_operations nfs_file_inode_operations;

/* linux/fs/nfs/dir.c */

extern struct inode_operations nfs_dir_inode_operations;
extern void nfs_sillyrename_cleanup(struct inode *);
extern void nfs_kfree_cache(void);

/* linux/fs/nfs/symlink.c */

extern struct inode_operations nfs_symlink_inode_operations;

/* linux/fs/nfs/mmap.c */

extern int nfs_mmap(struct inode * inode, struct file * file, struct vm_area_struct * vma);

/* linux/fs/nfs/bio.c */

extern int nfs_readpage(struct inode *, struct page *);

/* NFS root */

#define NFS_ROOT		"/tftpboot/%s"
#define NFS_ROOT_NAME_LEN	256
#define NFS_ROOT_ADDRS_LEN	128

extern int nfs_root_mount(struct super_block *sb);
extern int nfs_root_init(char *nfsname, char *nfsaddrs);
extern char nfs_root_name[];
extern char nfs_root_addrs[];

#endif /* __KERNEL__ */

#endif

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
 * in one 4096-byte page, currently 256.
 */

#define NFS_READDIR_CACHE_SIZE		64

#define NFS_MAX_FILE_IO_BUFFER_SIZE	16384
#define NFS_DEF_FILE_IO_BUFFER_SIZE	1024

/*
 * The upper limit on timeouts for the exponential backoff algorithm
 * in tenths of a second.
 */

#define NFS_MAX_RPC_TIMEOUT		600

/*
 * Size of the lookup cache in units of number of entries cached.
 * It is better not to make this too large although the optimum
 * depends on a usage and environment.
 */

#define NFS_LOOKUP_CACHE_SIZE		64

#define NFS_SUPER_MAGIC			0x6969

#define NFS_SERVER(inode)		(&(inode)->i_sb->u.nfs_sb.s_server)
#define NFS_FH(inode)			(&(inode)->u.nfs_i.fhandle)

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
			 struct nfs_fattr *fattr, int fs);
extern int nfs_proc_write(struct nfs_server *server, struct nfs_fh *fhandle,
			  int offset, int count, char *data,
			  struct nfs_fattr *fattr);
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

/* linux/fs/nfs/sock.c */

extern int nfs_rpc_call(struct nfs_server *server, int *start, int *end, int size);

/* linux/fs/nfs/inode.c */

extern struct super_block *nfs_read_super(struct super_block *sb, 
					  void *data,int);
extern struct inode *nfs_fhget(struct super_block *sb, struct nfs_fh *fhandle,
			       struct nfs_fattr *fattr);
extern void nfs_refresh_inode(struct inode *inode, struct nfs_fattr *fattr);

/* linux/fs/nfs/file.c */

extern struct inode_operations nfs_file_inode_operations;

/* linux/fs/nfs/dir.c */

extern struct inode_operations nfs_dir_inode_operations;

/* linux/fs/nfs/symlink.c */

extern struct inode_operations nfs_symlink_inode_operations;

/* linux/fs/nfs/mmap.c */

extern int nfs_mmap(struct inode * inode, struct file * file, struct vm_area_struct * vma);

#endif /* __KERNEL__ */

#endif

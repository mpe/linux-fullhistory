#ifndef _LINUX_NFS_XDR_H
#define _LINUX_NFS_XDR_H

extern struct rpc_program	nfs_program;
extern struct rpc_stat		nfs_rpcstat;

struct nfs_fattr {
	unsigned short		valid;		/* which fields are valid */
	__u64			pre_size;	/* pre_op_attr.size	  */
	__u64			pre_mtime;	/* pre_op_attr.mtime	  */
	__u64			pre_ctime;	/* pre_op_attr.ctime	  */
	enum nfs_ftype		type;		/* always use NFSv2 types */
	__u32			mode;
	__u32			nlink;
	__u32			uid;
	__u32			gid;
	__u64			size;
	union {
		struct {
			__u32	blocksize;
			__u32	blocks;
		} nfs2;
		struct {
			__u64	used;
		} nfs3;
	} du;
	__u32			rdev;
	__u64			fsid;
	__u64			fileid;
	__u64			atime;
	__u64			mtime;
	__u64			ctime;
};

#define NFS_ATTR_WCC		0x0001		/* pre-op WCC data    */
#define NFS_ATTR_FATTR		0x0002		/* post-op attributes */
#define NFS_ATTR_FATTR_V3	0x0004		/* NFSv3 attributes */

struct nfs_fsinfo {
	__u32			tsize;
	__u32			bsize;
	__u32			blocks;
	__u32			bfree;
	__u32			bavail;
};

/* Arguments to the write call.
 * Note that NFS_WRITE_MAXIOV must be <= (MAX_IOVEC-2) from sunrpc/xprt.h
 */
#define NFS_WRITE_MAXIOV        8
struct nfs_writeargs {
	struct nfs_fh *		fh;
	__u32			offset;
	__u32			count;
	enum nfs3_stable_how	stable;
	unsigned int		nriov;
	struct iovec		iov[NFS_WRITE_MAXIOV];
};

struct nfs_writeverf {
	enum nfs3_stable_how	committed;
	__u32			verifier[2];
};

struct nfs_writeres {
	struct nfs_fattr *	fattr;
	struct nfs_writeverf *	verf;
	__u32			count;
};

struct nfs_readargs {
	struct nfs_fh *		fh;
	__u32			offset;
	__u32			count;
	void *			buffer;
};

struct nfs_readres {
	struct nfs_fattr *	fattr;
	unsigned int		count;
};

/*
 * Argument struct for decode_entry function
 */
struct nfs_entry {
	struct page *		page;
	__u64			ino;
	__u64			cookie,
				prev_cookie;
	const char *		name;
	unsigned int		len;
	int			eof;
	struct nfs_fh		fh;
	struct nfs_fattr	fattr;
	unsigned long		offset,
				prev;
};

/*
 * The following types are for NFSv2 only.
 */
struct nfs_sattrargs {
	struct nfs_fh *		fh;
	struct iattr *		sattr;
};

struct nfs_diropargs {
	struct nfs_fh *		fh;
	const char *		name;
};

struct nfs_createargs {
	struct nfs_fh *		fh;
	const char *		name;
	struct iattr *		sattr;
};

struct nfs_renameargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	struct nfs_fh *		tofh;
	const char *		toname;
};

struct nfs_linkargs {
	struct nfs_fh *		fromfh;
	struct nfs_fh *		tofh;
	const char *		toname;
};

struct nfs_symlinkargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	const char *		topath;
	struct iattr *		sattr;
};

struct nfs_readdirargs {
	struct nfs_fh *		fh;
	__u32			cookie;
	void *			buffer;
	unsigned int		bufsiz;
};

struct nfs_diropok {
	struct nfs_fh *		fh;
	struct nfs_fattr *	fattr;
};

struct nfs_readlinkargs {
	struct nfs_fh *		fh;
	const void *		buffer;
};

struct nfs_readdirres {
	void *			buffer;
	unsigned int		bufsiz;
};

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
extern int nfs_proc_readdir(struct dentry *, struct nfs_fattr *,
			    u64 cookie, void *, unsigned int size, int plus);
extern int nfs_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fsinfo *res);
extern u32 * nfs_decode_dirent(u32 *p, struct nfs_entry *entry, int plus);

#endif

/*
 * NFSv3 protocol definitions
 */
#ifndef _LINUX_NFS3_H
#define _LINUX_NFS3_H

#define NFS3_PORT		2049
#define NFS3_MAXDATA		32768
#define NFS3_MAXPATHLEN		PATH_MAX
#define NFS3_MAXNAMLEN		NAME_MAX
#define NFS3_MAXGROUPS		16
#define NFS3_FHSIZE		64
#define NFS3_COOKIESIZE		4
#define NFS3_FIFO_DEV		(-1)
#define NFS3MODE_FMT		0170000
#define NFS3MODE_DIR		0040000
#define NFS3MODE_CHR		0020000
#define NFS3MODE_BLK		0060000
#define NFS3MODE_REG		0100000
#define NFS3MODE_LNK		0120000
#define NFS3MODE_SOCK		0140000
#define NFS3MODE_FIFO		0010000

/* Flags for access() call */
#define NFS3_ACCESS_READ	0x0001
#define NFS3_ACCESS_LOOKUP	0x0002
#define NFS3_ACCESS_MODIFY	0x0004
#define NFS3_ACCESS_EXTEND	0x0008
#define NFS3_ACCESS_DELETE	0x0010
#define NFS3_ACCESS_EXECUTE	0x0020

/* Flags for create mode */
enum nfs3_createmode {
	NFS3_CREATE_UNCHECKED = 0,
	NFS3_CREATE_GUARDED = 1,
	NFS3_CREATE_EXCLUSIVE = 2
};

/* NFSv3 file system properties */
#define NFS3_FSF_LINK		0x0001
#define NFS3_FSF_SYMLINK	0x0002
#define NFS3_FSF_HOMOGENEOUS	0x0008
#define NFS3_FSF_CANSETTIME	0x0010
/* Some shorthands. See fs/nfsd/nfs3proc.c */
#define NFS3_FSF_DEFAULT	0x001B
#define NFS3_FSF_BILLYBOY	0x0018
#define NFS3_FSF_READONLY	0x0008

enum nfs3_ftype {
	NF3NON  = 0,
	NF3REG  = 1,
	NF3DIR  = 2,
	NF3BLK  = 3,
	NF3CHR  = 4,
	NF3LNK  = 5,
	NF3SOCK = 6,
	NF3FIFO = 7,	/* changed from NFSv2 (was 8) */
	NF3BAD  = 8
};

#define NFS3_VERSION		3
#define NFS3PROC_NULL		0
#define NFS3PROC_GETATTR	1
#define NFS3PROC_SETATTR	2
#define NFS3PROC_LOOKUP		3
#define NFS3PROC_ACCESS		4
#define NFS3PROC_READLINK	5
#define NFS3PROC_READ		6
#define NFS3PROC_WRITE		7
#define NFS3PROC_CREATE		8
#define NFS3PROC_MKDIR		9
#define NFS3PROC_SYMLINK	10
#define NFS3PROC_MKNOD		11
#define NFS3PROC_REMOVE		12
#define NFS3PROC_RMDIR		13
#define NFS3PROC_RENAME		14
#define NFS3PROC_LINK		15
#define NFS3PROC_READDIR	16
#define NFS3PROC_READDIRPLUS	17
#define NFS3PROC_FSSTAT		18
#define NFS3PROC_FSINFO		19
#define NFS3PROC_PATHCONF	20
#define NFS3PROC_COMMIT		21

#define NFS_MNT3_PROGRAM	100005
#define NFS_MNT3_VERSION	3
#define MOUNTPROC3_NULL		0
#define MOUNTPROC3_MNT		1
#define MOUNTPROC3_UMNT		3
#define MOUNTPROC3_UMNTALL	4
 

#if defined(__KERNEL__) || defined(NFS_NEED_KERNEL_TYPES)

/* Number of 32bit words in post_op_attr */
#define NFS3_POST_OP_ATTR_WORDS		22

#ifdef NFS_NEED_NFS3_XDR_TYPES

struct nfs3_sattrargs {
	struct nfs_fh *		fh;
	struct iattr *		sattr;
	unsigned int		guard;
	__u64			guardtime;
};

struct nfs3_diropargs {
	struct nfs_fh *		fh;
	const char *		name;
	int			len;
};

struct nfs3_accessargs {
	struct nfs_fh *		fh;
	__u32			access;
};

struct nfs3_createargs {
	struct nfs_fh *		fh;
	const char *		name;
	int			len;
	struct iattr *		sattr;
	enum nfs3_createmode	createmode;
	__u32			verifier[2];
};

struct nfs3_mkdirargs {
	struct nfs_fh *		fh;
	const char *		name;
	int			len;
	struct iattr *		sattr;
};

struct nfs3_symlinkargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	int			fromlen;
	const char *		topath;
	int			tolen;
	struct iattr *		sattr;
};

struct nfs3_mknodargs {
	struct nfs_fh *		fh;
	const char *		name;
	int			len;
	enum nfs3_ftype		type;
	struct iattr *		sattr;
	dev_t			rdev;
};

struct nfs3_renameargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	int			fromlen;
	struct nfs_fh *		tofh;
	const char *		toname;
	int			tolen;
};

struct nfs3_linkargs {
	struct nfs_fh *		fromfh;
	struct nfs_fh *		tofh;
	const char *		toname;
	int			tolen;
};

struct nfs3_readdirargs {
	struct nfs_fh *		fh;
	__u64			cookie;
	__u32			verf[2];
	void *			buffer;
	unsigned int		bufsiz;
	int			plus;
};

struct nfs3_diropres {
	struct nfs_fattr *	dir_attr;
	struct nfs_fh *		fh;
	struct nfs_fattr *	fattr;
};

struct nfs3_accessres {
	struct nfs_fattr *	fattr;
	__u32			access;
};

struct nfs3_readlinkargs {
	struct nfs_fh *		fh;
	void *			buffer;
	unsigned int		bufsiz;
};

struct nfs3_readlinkres {
	struct nfs_fattr *	fattr;
	void *			buffer;
	unsigned int		bufsiz;
};

struct nfs3_renameres {
	struct nfs_fattr *	fromattr;
	struct nfs_fattr *	toattr;
};

struct nfs3_linkres {
	struct nfs_fattr *	dir_attr;
	struct nfs_fattr *	fattr;
};

struct nfs3_readdirres {
	struct nfs_fattr *	dir_attr;
	__u32 *			verf;
	void *			buffer;
	unsigned int		bufsiz;
	int			plus;
};

#endif /* NFS_NEED_XDR_TYPES */


#endif /* __KERNEL__ */
#endif /* _LINUX_NFS3_H */

/*
 * linux/fs/nfsd/nfs3proc.c
 *
 * Process version 3 NFS requests.
 *
 * Copyright (C) 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/linkage.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/version.h>
#include <linux/unistd.h>
#include <linux/malloc.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfsd/xdr3.h>

typedef struct svc_rqst	svc_rqst;
typedef struct svc_buf	svc_buf;

#define NFSDDBG_FACILITY		NFSDDBG_PROC

#define RETURN(st)	{ resp->status = (st); return (st); }

static void
svcbuf_reserve(struct svc_buf *buf, u32 **ptr, int *len, int nr)
{
	*ptr = buf->buf + nr;
	*len = buf->buflen - buf->len - nr;
}

static int
nfsd3_proc_null(struct svc_rqst *rqstp, void *argp, void *resp)
{
	return nfs_ok;
}

/*
 * Get a file's attributes
 * N.B. After this call resp->fh needs an fh_put
 */
static int
nfsd3_proc_getattr(struct svc_rqst *rqstp, struct nfsd_fhandle  *argp,
					   struct nfsd3_attrstat *resp)
{
	int	nfserr;

	dprintk("nfsd: GETATTR  %x/%ld\n",
				SVCFH_DEV(&argp->fh),
				SVCFH_INO(&argp->fh));

	resp->fh = argp->fh;
	nfserr = fh_verify(rqstp, &resp->fh, 0, MAY_NOP);
	RETURN(nfserr);
}

/*
 * Set a file's attributes
 * N.B. After this call resp->fh needs an fh_put
 */
static int
nfsd3_proc_setattr(struct svc_rqst *rqstp, struct nfsd3_sattrargs *argp,
					   struct nfsd3_attrstat  *resp)
{
	int	nfserr;

	dprintk("nfsd: SETATTR  %x/%ld\n",
				SVCFH_DEV(&argp->fh),
				SVCFH_INO(&argp->fh));

	resp->fh = argp->fh;
	nfserr = nfsd_setattr(rqstp, &resp->fh, &argp->attrs);
	RETURN(nfserr);
}

/*
 * Look up a path name component
 * N.B. After this call _both_ resp->dirfh and resp->fh need an fh_put
 */
static int
nfsd3_proc_lookup(struct svc_rqst *rqstp, struct nfsd3_diropargs *argp,
					  struct nfsd3_lookupres *resp)
{
	int	nfserr;

	dprintk("nfsd: LOOKUP   %x/%ld %s\n",
				SVCFH_DEV(&argp->fh),
				SVCFH_INO(&argp->fh),
				argp->name);

	resp->dirfh = argp->fh;
	nfserr = nfsd_lookup(rqstp, &resp->dirfh,
				    argp->name,
				    argp->len,
				    &resp->fh);
	RETURN(nfserr);
}

/*
 * Check file access
 */
static int
nfsd3_proc_access(struct svc_rqst *rqstp, struct nfsd_fhandle   *argp,
					  struct nfsd3_accessres *resp)
{
	/* to be done */
	resp->fh = argp->fh;
	return nfserr_notsupp;
}

/*
 * Read a symlink.
 */
static int
nfsd3_proc_readlink(struct svc_rqst *rqstp, struct nfsd_fhandle     *argp,
					   struct nfsd3_readlinkres *resp)
{
	u32		*path;
	int		dummy, nfserr;

	dprintk("nfsd: READLINK %x/%ld\n",
				SVCFH_DEV(&argp->fh),
				SVCFH_INO(&argp->fh));

	/* Reserve room for status, post_op_attr, and path length */
	svcbuf_reserve(&rqstp->rq_resbuf, &path, &dummy, 1 + 22 + 1);

	/* Read the symlink. */
	resp->len = NFS3_MAXPATHLEN;
	nfserr = nfsd_readlink(rqstp, &argp->fh, (char *) path, &resp->len);
	fh_put(&argp->fh);
	RETURN(nfserr);
}

/*
 * Read a portion of a file.
 * N.B. After this call resp->fh needs an fh_put
 */
static int
nfsd3_proc_read(struct svc_rqst *rqstp, struct nfsd3_readargs *argp,
				        struct nfsd3_readres  *resp)
{
	u32 *	buffer;
	int	nfserr, avail;

	dprintk("nfsd: READ %x/%ld %lu bytes at %lu\n",
				SVCFH_DEV(&argp->fh),
				SVCFH_INO(&argp->fh),
				(unsigned long) argp->count,
				(unsigned long) argp->offset);

	/* Obtain buffer pointer for payload.
	 * 1 (status) + 22 (post_op_attr) + 1 (count) + 1 (eof)
	 * + 1 (xdr opaque byte count) = 26
	 */
	svcbuf_reserve(&rqstp->rq_resbuf, &buffer, &avail, 26);

	if ((avail << 2) < argp->count) {
		printk(KERN_NOTICE
			"oversized read request from %08lx:%d (%d bytes)\n",
				ntohl(rqstp->rq_addr.sin_addr.s_addr),
				ntohs(rqstp->rq_addr.sin_port),
				argp->count);
		argp->count = avail;
	}

	resp->count = argp->count;
	resp->fh    = argp->fh;
	nfserr = nfsd_read(rqstp, &resp->fh,
				  argp->offset,
				  (char *) buffer,
				  &resp->count);

	RETURN(nfserr);
}

/*
 * Write data to a file
 * N.B. After this call resp->fh needs an fh_put
 */
static int
nfsd3_proc_write(struct svc_rqst *rqstp, struct nfsd3_writeargs *argp,
					 struct nfsd3_writeres  *resp)
{
	int	nfserr;

	dprintk("nfsd: WRITE    %x/%ld %d bytes at %ld\n",
				SVCFH_DEV(&argp->fh),
				SVCFH_INO(&argp->fh),
				argp->len,
				(unsigned long) argp->offset);

	resp->fh = argp->fh;
	nfserr = nfsd_write(rqstp, &resp->fh,
				   argp->offset,
				   argp->data,
				   argp->len,
				   argp->stable);
	resp->committed = argp->stable;
	RETURN(nfserr);
}

/*
 * With NFSv3, CREATE processing is a lot easier than with NFSv2.
 * At least in theory; we'll see how it fares in practice when the
 * first reports about SunOS compatibility problems start to pour in...
 * N.B. After this call _both_ resp->dirfh and resp->fh need an fh_put
 */
static int
nfsd3_proc_create(struct svc_rqst *rqstp, struct nfsd3_createargs *argp,
					  struct nfsd3_createres  *resp)
{
	svc_fh		*dirfhp, *newfhp = NULL;
	struct iattr	*attr;
	int		mode;
	u32		nfserr;

	dprintk("nfsd: CREATE   %x/%ld %s\n",
				SVCFH_DEV(&argp->fh),
				SVCFH_INO(&argp->fh),
				argp->name);

	dirfhp = fh_copy(&resp->dirfh, &argp->fh);
	newfhp = fh_init(&resp->fh);
	attr   = &argp->attrs;

	/* Get the directory inode */
	nfserr = fh_verify(rqstp, dirfhp, S_IFDIR, MAY_CREATE);
	if (nfserr)
		RETURN(nfserr);

	/* Unfudge the mode bits */
	attr->ia_mode &= ~S_IFMT;
	if (!(attr->ia_valid & ATTR_MODE)) { 
		attr->ia_valid |= ATTR_MODE;
		attr->ia_mode = S_IFREG;
	}
	mode = attr->ia_mode & ~S_IFMT;

	/* Now create the file and set attributes */
	nfserr = nfsd_create(rqstp, dirfhp, argp->name, argp->len,
				attr, S_IFREG, 0, newfhp);

	RETURN(nfserr);
}

/* N.B. Is nfsd3_attrstat * correct for resp?? table says "void" */
static int
nfsd3_proc_remove(struct svc_rqst *rqstp, struct nfsd3_diropargs *argp,
					  struct nfsd3_attrstat  *resp)
{
	int	nfserr;

	dprintk("nfsd: REMOVE   %x/%ld %s\n",
				SVCFH_DEV(&argp->fh),
				SVCFH_INO(&argp->fh),
				argp->name);

	/* Is this correct?? */
	fh_copy(&resp->fh, &argp->fh);

	/* Unlink. -S_IFDIR means file must not be a directory */
	nfserr = nfsd_unlink(rqstp, &resp->fh, -S_IFDIR, argp->name, argp->len);
	/* 
	 * N.B. Should be an fh_put here ... nfsd3_proc_rmdir has one,
	 * or else as an xdr release function
	 */
	fh_put(&resp->fh);
	RETURN(nfserr);
}

static int
nfsd3_proc_rename(struct svc_rqst *rqstp, struct nfsd3_renameargs *argp,
				  	 void		        *resp)
{
	int	nfserr;

	dprintk("nfsd: RENAME   %x/%ld %s -> %x/%ld %s\n",
				SVCFH_DEV(&argp->ffh),
				SVCFH_INO(&argp->ffh),
				argp->fname,
				SVCFH_DEV(&argp->tfh),
				SVCFH_INO(&argp->tfh),
				argp->tname);

	nfserr = nfsd_rename(rqstp, &argp->ffh, argp->fname, argp->flen,
				    &argp->tfh, argp->tname, argp->tlen);
	fh_put(&argp->ffh);
	fh_put(&argp->tfh);
	RETURN(nfserr);
}

static int
nfsd3_proc_link(struct svc_rqst *rqstp, struct nfsd3_linkargs *argp,
				void			    *resp)
{
	int	nfserr;

	dprintk("nfsd: LINK     %x/%ld -> %x/%ld %s\n",
				SVCFH_DEV(&argp->ffh),
				SVCFH_INO(&argp->ffh),
				SVCFH_DEV(&argp->tfh),
				SVCFH_INO(&argp->tfh),
				argp->tname);

	nfserr = nfsd_link(rqstp, &argp->tfh, argp->tname, argp->tlen,
				  &argp->ffh);
	fh_put(&argp->ffh);
	fh_put(&argp->tfh);
	RETURN(nfserr);
}

static int
nfsd3_proc_symlink(struct svc_rqst *rqstp, struct nfsd3_symlinkargs *argp,
				          void			  *resp)
{
	struct svc_fh	newfh;
	int		nfserr;

	dprintk("nfsd: SYMLINK  %x/%ld %s -> %s\n",
				SVCFH_DEV(&argp->ffh),
				SVCFH_INO(&argp->ffh),
				argp->fname, argp->tname);

	memset(&newfh, 0, sizeof(newfh));

	/*
	 * Create the link, look up new file and set attrs.
	 */
	nfserr = nfsd_symlink(rqstp, &argp->ffh, argp->fname, argp->flen,
						 argp->tname, argp->tlen,
						 &newfh);
	if (nfserr)
		nfserr = nfsd_setattr(rqstp, &newfh, &argp->attrs);

	fh_put(&argp->ffh);
	fh_put(&newfh);
	RETURN(nfserr);
}

/*
 * Make directory. This operation is not idempotent.
 * N.B. After this call resp->fh needs an fh_put
 */
static int
nfsd3_proc_mkdir(struct svc_rqst *rqstp, struct nfsd3_createargs *argp,
					struct nfsd3_diropres   *resp)
{
	int	nfserr;

	dprintk("nfsd: MKDIR    %x/%ld %s\n",
				SVCFH_DEV(&argp->fh),
				SVCFH_INO(&argp->fh),
				argp->name);

	nfserr = nfsd_create(rqstp, &argp->fh, argp->name, argp->len,
				    &argp->attrs, S_IFDIR, 0, &resp->fh);
	fh_put(&argp->fh);
	RETURN(nfserr);
}

/*
 * Remove a directory
 */
static int
nfsd3_proc_rmdir(struct svc_rqst *rqstp, struct nfsd3_diropargs *argp,
				 	void		      *resp)
{
	int	nfserr;

	dprintk("nfsd: RMDIR    %x/%ld %s\n",
				SVCFH_DEV(&argp->fh),
				SVCFH_INO(&argp->fh),
				argp->name);

	nfserr = nfsd_unlink(rqstp, &argp->fh, S_IFDIR, argp->name, argp->len);
	fh_put(&argp->fh);
	RETURN(nfserr);
}

/*
 * Read a portion of a directory.
 */
static int
nfsd3_proc_readdir(struct svc_rqst *rqstp, struct nfsd3_readdirargs *argp,
					  struct nfsd3_readdirres  *resp)
{
	u32 *	buffer;
	int	nfserr, count;

	dprintk("nfsd: READDIR  %x/%ld %d bytes at %d\n",
				SVCFH_DEV(&argp->fh),
				SVCFH_INO(&argp->fh),
				argp->count, argp->cookie);

	/* Reserve buffer space for status */
	svcbuf_reserve(&rqstp->rq_resbuf, &buffer, &count, 1);

	/* Make sure we've room for the NULL ptr & eof flag, and shrink to
	 * client read size */
	if ((count -= 8) > argp->count)
		count = argp->count;

	/* Read directory and encode entries on the fly */
	nfserr = nfsd_readdir(rqstp, &argp->fh, (loff_t) argp->cookie, 
					nfssvc_encode_entry,
					buffer, &count);
	resp->count = count;

	fh_put(&argp->fh);
	RETURN(nfserr);
}

/*
 * Get file system info
 */
static int
nfsd3_proc_statfs(struct svc_rqst * rqstp, struct nfsd_fhandle   *argp,
					  struct nfsd3_statfsres *resp)
{
	int	nfserr;

	dprintk("nfsd: STATFS   %x/%ld\n",
				SVCFH_DEV(&argp->fh),
				SVCFH_INO(&argp->fh));

	nfserr = nfsd_statfs(rqstp, &argp->fh, &resp->stats);
	fh_put(&argp->fh);
	RETURN(nfserr);
}

/*
 * NFSv2 Server procedures.
 * Only the results of non-idempotent operations are cached.
 */
#define nfsd3_proc_none		NULL
#define nfssvc_encode_void	NULL
#define nfssvc_decode_void	NULL
#define nfssvc_release_void	NULL
struct nfsd3_void { int dummy; };

#define PROC(name, argt, rest, relt, cache)	\
 { (svc_procfunc) nfsd3_proc_##name,	\
   (kxdrproc_t) nfssvc_decode_##argt,	\
   (kxdrproc_t) nfssvc_encode_##rest,	\
   (kxdrproc_t) nfssvc_release_##relt,	\
   sizeof(struct nfsd3_##argt),		\
   sizeof(struct nfsd3_##rest),		\
   0,					\
   cache				\
 }
struct svc_procedure		nfsd3_procedures2[18] = {
  PROC(null,	 void,		void,		void,	 RC_NOCACHE),
  PROC(getattr,	 fhandle,	attrstat,	fhandle, RC_NOCACHE),
  PROC(setattr,  sattrargs,	attrstat,	fhandle, RC_REPLBUFF),
  PROC(none,	 void,		void,		void,	 RC_NOCACHE),
  PROC(lookup,	 diropargs,	diropres,	fhandle2,RC_NOCACHE),
  PROC(readlink, fhandle,	readlinkres,	void,	 RC_NOCACHE),
  PROC(read,	 readargs,	readres,	fhandle, RC_NOCACHE),
  PROC(none,	 void,		void,		void,	 RC_NOCACHE),
  PROC(write,	 writeargs,	attrstat,	fhandle, RC_REPLBUFF),
  PROC(create,	 createargs,	diropres,	fhandle2,RC_REPLBUFF),
  PROC(remove,	 diropargs,	void,/* ??*/	void,	 RC_REPLSTAT),
  PROC(rename,	 renameargs,	void,		void,	 RC_REPLSTAT),
  PROC(link,	 linkargs,	void,		void,	 RC_REPLSTAT),
  PROC(symlink,	 symlinkargs,	void,		void,	 RC_REPLSTAT),
  PROC(mkdir,	 createargs,	diropres,	fhandle, RC_REPLBUFF),
  PROC(rmdir,	 diropargs,	void,		void,	 RC_REPLSTAT),
  PROC(readdir,	 readdirargs,	readdirres,	void,	 RC_REPLSTAT),
  PROC(statfs,	 fhandle,	statfsres,	void,	 RC_NOCACHE),
};


/*
 * Map errnos to NFS errnos.
 */
int
nfserrno (int errno)
{
	static struct {
		int	nfserr;
		int	syserr;
	} nfs_errtbl[] = {
		{ NFS_OK, 0 },
		{ NFSERR_PERM, EPERM },
		{ NFSERR_NOENT, ENOENT },
		{ NFSERR_IO, EIO },
		{ NFSERR_NXIO, ENXIO },
		{ NFSERR_ACCES, EACCES },
		{ NFSERR_EXIST, EEXIST },
		{ NFSERR_NODEV, ENODEV },
		{ NFSERR_NOTDIR, ENOTDIR },
		{ NFSERR_ISDIR, EISDIR },
		{ NFSERR_INVAL, EINVAL },
		{ NFSERR_FBIG, EFBIG },
		{ NFSERR_NOSPC, ENOSPC },
		{ NFSERR_ROFS, EROFS },
		{ NFSERR_NAMETOOLONG, ENAMETOOLONG },
		{ NFSERR_NOTEMPTY, ENOTEMPTY },
#ifdef EDQUOT
		{ NFSERR_DQUOT, EDQUOT },
#endif
		{ NFSERR_STALE, ESTALE },
		{ NFSERR_WFLUSH, EIO },
		{ -1, EIO }
	};
	int	i;

	for (i = 0; nfs_errtbl[i].nfserr != -1; i++) {
		if (nfs_errtbl[i].syserr == errno)
			return htonl (nfs_errtbl[i].nfserr);
	}
	printk (KERN_INFO "nfsd: non-standard errno: %d\n", errno);
	return nfserr_io;
}

#if 0
static void
nfsd3_dump(char *tag, u32 *buf, int len)
{
	int	i;

	printk(KERN_NOTICE
		"nfsd: %s (%d words)\n", tag, len);

	for (i = 0; i < len && i < 32; i += 8)
		printk(KERN_NOTICE
			" %08lx %08lx %08lx %08lx"
			" %08lx %08lx %08lx %08lx\n",
			buf[i],   buf[i+1], buf[i+2], buf[i+3],
			buf[i+4], buf[i+5], buf[i+6], buf[i+7]);
}
#endif

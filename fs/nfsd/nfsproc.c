/*
 * nfsproc2.c	Process version 2 NFS requests.
 *
 * Copyright (C) 1995 Olaf Kirch <okir@monad.swb.de>
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
#include <linux/nfsd/xdr.h>

typedef struct svc_rqst	svc_rqst;
typedef struct svc_buf	svc_buf;

#define NFSDDBG_FACILITY		NFSDDBG_PROC

#define sleep(msec)		\
	{	printk(KERN_NOTICE "nfsd: sleeping %d msecs\n", msec); \
		current->state = TASK_INTERRUPTIBLE;	\
		current->timeout = jiffies + msec / 10;	\
		schedule();	\
	}
#define RETURN(st)	return st

static void
svcbuf_reserve(struct svc_buf *buf, u32 **ptr, int *len, int nr)
{
	*ptr = buf->buf + nr;
	*len = buf->buflen - buf->len - nr;
}

static int
nfsd_proc_null(struct svc_rqst *rqstp, void *argp, void *resp)
{
	RETURN(nfs_ok);
}

/*
 * Get a file's attributes
 * N.B. After this call resp->fh needs an fh_put
 */
static int
nfsd_proc_getattr(struct svc_rqst *rqstp, struct nfsd_fhandle  *argp,
					  struct nfsd_attrstat *resp)
{
	dprintk("nfsd: GETATTR  %p\n", SVCFH_DENTRY(&argp->fh));

	fh_copy(&resp->fh, &argp->fh);
	RETURN(fh_verify(rqstp, &resp->fh, 0, MAY_NOP));
}

/*
 * Set a file's attributes
 * N.B. After this call resp->fh needs an fh_put
 */
static int
nfsd_proc_setattr(struct svc_rqst *rqstp, struct nfsd_sattrargs *argp,
					  struct nfsd_attrstat  *resp)
{
	dprintk("nfsd: SETATTR  %p\n", SVCFH_DENTRY(&argp->fh));

	fh_copy(&resp->fh, &argp->fh);
	RETURN(nfsd_setattr(rqstp, &resp->fh, &argp->attrs));
}

/*
 * Look up a path name component
 * N.B. After this call resp->fh needs an fh_put
 */
static int
nfsd_proc_lookup(struct svc_rqst *rqstp, struct nfsd_diropargs *argp,
					 struct nfsd_diropres  *resp)
{
	int	nfserr;

	dprintk("nfsd: LOOKUP   %p %s\n", SVCFH_DENTRY(&argp->fh), argp->name);

	nfserr = nfsd_lookup(rqstp, &argp->fh,
				    argp->name,
				    argp->len,
				    &resp->fh);

	fh_put(&argp->fh);
	RETURN(nfserr);
}

/*
 * Read a symlink.
 */
static int
nfsd_proc_readlink(struct svc_rqst *rqstp, struct nfsd_fhandle     *argp,
					   struct nfsd_readlinkres *resp)
{
	u32		*path;
	int		dummy, nfserr;

	dprintk("nfsd: READLINK %p\n", SVCFH_DENTRY(&argp->fh));

	/* Reserve room for status and path length */
	svcbuf_reserve(&rqstp->rq_resbuf, &path, &dummy, 2);

	/* Read the symlink. */
	resp->len = NFS_MAXPATHLEN;
	nfserr = nfsd_readlink(rqstp, &argp->fh, (char *) path, &resp->len);

	fh_put(&argp->fh);
	RETURN(nfserr);
}

/*
 * Read a portion of a file.
 * N.B. After this call resp->fh needs an fh_put
 */
static int
nfsd_proc_read(struct svc_rqst *rqstp, struct nfsd_readargs *argp,
				       struct nfsd_readres  *resp)
{
	u32 *	buffer;
	int	nfserr, avail;

	dprintk("nfsd: READ %p %d bytes at %d\n",
		SVCFH_DENTRY(&argp->fh),
		argp->count, argp->offset);

	/* Obtain buffer pointer for payload. 19 is 1 word for
	 * status, 17 words for fattr, and 1 word for the byte count.
	 */
	svcbuf_reserve(&rqstp->rq_resbuf, &buffer, &avail, 19);

	if ((avail << 2) < argp->count) {
		printk(KERN_NOTICE
			"oversized read request from %08lx:%d (%d bytes)\n",
				ntohl(rqstp->rq_addr.sin_addr.s_addr),
				ntohs(rqstp->rq_addr.sin_port),
				argp->count);
		argp->count = avail;
	}

	resp->count = argp->count;
	nfserr = nfsd_read(rqstp, fh_copy(&resp->fh, &argp->fh),
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
nfsd_proc_write(struct svc_rqst *rqstp, struct nfsd_writeargs *argp,
					struct nfsd_attrstat  *resp)
{
	int	nfserr;

	dprintk("nfsd: WRITE    %p %d bytes at %d\n",
		SVCFH_DENTRY(&argp->fh),
		argp->len, argp->offset);

	nfserr = nfsd_write(rqstp, fh_copy(&resp->fh, &argp->fh),
				   argp->offset,
				   argp->data,
				   argp->len,
				   0);
	RETURN(nfserr);
}

/*
 * CREATE processing is complicated. The keyword here is `overloaded.'
 * There's a small race condition here between the check for existence
 * and the actual create() call, but one could even consider this a
 * feature because this only happens if someone else creates the file
 * at the same time.
 * N.B. After this call _both_ argp->fh and resp->fh need an fh_put
 */
static int
nfsd_proc_create(struct svc_rqst *rqstp, struct nfsd_createargs *argp,
					 struct nfsd_diropres   *resp)
{
	struct inode	*dirp, *inode = NULL;
	struct iattr	*attr;
	svc_fh		*dirfhp, *newfhp;
	int		nfserr, type, mode;
	int		rdonly = 0, exists;
	dev_t		rdev = NODEV;

	dprintk("nfsd: CREATE   %p %s\n", SVCFH_DENTRY(&argp->fh), argp->name);

	dirfhp = &argp->fh;
	newfhp = &resp->fh;
	attr = &argp->attrs;

	/* Get the directory inode */
	nfserr = fh_verify(rqstp, dirfhp, S_IFDIR, MAY_EXEC);
	if (nfserr)
		goto done; /* must fh_put dirfhp even on error */
	dirp = dirfhp->fh_handle.fh_dentry->d_inode;

	/* Check for MAY_WRITE separately. */
	nfserr = nfsd_permission(dirfhp->fh_export,
				 dirfhp->fh_handle.fh_dentry,
				 MAY_WRITE);
	if (nfserr == nfserr_rofs) {
		rdonly = 1;	/* Non-fatal error for echo > /dev/null */
	} else if (nfserr)
		goto done;

	/* First, check if the file already exists.  */
	exists = !nfsd_lookup(rqstp, dirfhp, argp->name, argp->len, newfhp);

	if (newfhp->fh_dverified)
		inode = newfhp->fh_handle.fh_dentry->d_inode;

	/* Get rid of this soon... */
	if (exists && !inode) {
		printk("nfsd_proc_create: Wheee... exists but d_inode==NULL\n");
		nfserr = nfserr_rofs;
		goto done;
	}		

	/* Unfudge the mode bits */
	if (attr->ia_valid & ATTR_MODE) { 
		type = attr->ia_mode & S_IFMT;
		mode = attr->ia_mode & ~S_IFMT;
		if (!type)	/* HP weirdness */
			type = S_IFREG;
	} else if (exists) {
		type = inode->i_mode & S_IFMT;
		mode = inode->i_mode & ~S_IFMT;
	} else {
		type = S_IFREG;
		mode = 0;	/* ??? */
	}

	/* This is for "echo > /dev/null" a la SunOS. Argh. */
	if (rdonly && (!exists || type == S_IFREG)) {
		nfserr = nfserr_rofs;
		goto done;
	}

	attr->ia_valid |= ATTR_MODE;
	attr->ia_mode = type | mode;

	/* Special treatment for non-regular files according to the
	 * gospel of sun micro
	 */
	nfserr = 0;
	if (type != S_IFREG) {
		int	is_borc = 0;
		u32	size = attr->ia_size;

		rdev = (dev_t) size;
		if (type != S_IFBLK && type != S_IFCHR) {
			rdev = 0;
		} else if (type == S_IFCHR && size == ~(u32) 0) {
			/* If you think you've seen the worst, grok this. */
			attr->ia_mode = S_IFIFO | mode;
			type = S_IFIFO;
		} else if (size != rdev) {
			/* dev got truncated because of 16bit Linux dev_t */
			nfserr = nfserr_io;	/* or nfserr_inval? */
			goto done;
		} else {
			/* Okay, char or block special */
			is_borc = 1;
		}

		/* Make sure the type and device matches */
		if (exists && (type != (inode->i_mode & S_IFMT)
		 || (is_borc && inode->i_rdev != rdev))) {
			nfserr = nfserr_exist;
			goto done;
		}
	}
	
	if (!exists) {
		/* File doesn't exist. Create it and set attrs */
		nfserr = nfsd_create(rqstp, dirfhp, argp->name, argp->len,
					attr, type, rdev, newfhp);
	} else if (type == S_IFREG) {
		/* File already exists. We ignore all attributes except
		 * size, so that creat() behaves exactly like
		 * open(..., O_CREAT|O_TRUNC|O_WRONLY).
		 */
		if ((attr->ia_valid &= ~(ATTR_SIZE)) != 0)
			nfserr = nfsd_setattr(rqstp, newfhp, attr);
	}

done:
	fh_put(dirfhp);
	RETURN(nfserr);
}

static int
nfsd_proc_remove(struct svc_rqst *rqstp, struct nfsd_diropargs *argp,
					 void		       *resp)
{
	int	nfserr;

	dprintk("nfsd: REMOVE   %p %s\n", SVCFH_DENTRY(&argp->fh), argp->name);

	/* Unlink. -SIFDIR means file must not be a directory */
	nfserr = nfsd_unlink(rqstp, &argp->fh, -S_IFDIR, argp->name, argp->len);
	fh_put(&argp->fh);
	RETURN(nfserr);
}

static int
nfsd_proc_rename(struct svc_rqst *rqstp, struct nfsd_renameargs *argp,
				  	 void		        *resp)
{
	int	nfserr;

	dprintk("nfsd: RENAME   %p %s -> %p %s\n",
		SVCFH_DENTRY(&argp->ffh), argp->fname,
		SVCFH_DENTRY(&argp->tfh), argp->tname);

	nfserr = nfsd_rename(rqstp, &argp->ffh, argp->fname, argp->flen,
				    &argp->tfh, argp->tname, argp->tlen);
	fh_put(&argp->ffh);
	fh_put(&argp->tfh);
	RETURN(nfserr);
}

static int
nfsd_proc_link(struct svc_rqst *rqstp, struct nfsd_linkargs *argp,
				void			    *resp)
{
	int	nfserr;

	dprintk("nfsd: LINK     %p -> %p %s\n",
		SVCFH_DENTRY(&argp->ffh),
		SVCFH_DENTRY(&argp->tfh),
		argp->tname);

	nfserr = nfsd_link(rqstp, &argp->tfh, argp->tname, argp->tlen,
				  &argp->ffh);
	fh_put(&argp->ffh);
	fh_put(&argp->tfh);
	RETURN(nfserr);
}

static int
nfsd_proc_symlink(struct svc_rqst *rqstp, struct nfsd_symlinkargs *argp,
				          void			  *resp)
{
	struct svc_fh	newfh;
	int		nfserr;

	dprintk("nfsd: SYMLINK  %p %s -> %s\n",
		SVCFH_DENTRY(&argp->ffh),
		argp->fname, argp->tname);

	/*
	 * Create the link, look up new file and set attrs.
	 */
	nfserr = nfsd_symlink(rqstp, &argp->ffh, argp->fname, argp->flen,
						 argp->tname, argp->tlen,
						 &newfh);
	if (!nfserr)
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
nfsd_proc_mkdir(struct svc_rqst *rqstp, struct nfsd_createargs *argp,
					struct nfsd_diropres   *resp)
{
	int	nfserr;

	dprintk("nfsd: MKDIR    %p %s\n",
		SVCFH_DENTRY(&argp->fh),
		argp->name);

	/* N.B. what about the dentry count?? */
	resp->fh.fh_dverified = 0; /* paranoia */
	nfserr = nfsd_create(rqstp, &argp->fh, argp->name, argp->len,
				    &argp->attrs, S_IFDIR, 0, &resp->fh);
	fh_put(&argp->fh);
	RETURN(nfserr);
}

/*
 * Remove a directory
 */
static int
nfsd_proc_rmdir(struct svc_rqst *rqstp, struct nfsd_diropargs *argp,
				 	void		      *resp)
{
	int	nfserr;

	dprintk("nfsd: RMDIR    %p %s\n", SVCFH_DENTRY(&argp->fh), argp->name);

	nfserr = nfsd_unlink(rqstp, &argp->fh, S_IFDIR, argp->name, argp->len);
	fh_put(&argp->fh);
	RETURN(nfserr);
}

/*
 * Read a portion of a directory.
 */
static int
nfsd_proc_readdir(struct svc_rqst *rqstp, struct nfsd_readdirargs *argp,
					  struct nfsd_readdirres  *resp)
{
	u32 *	buffer;
	int	nfserr, count;

	dprintk("nfsd: READDIR  %p %d bytes at %d\n",
		SVCFH_DENTRY(&argp->fh),
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
nfsd_proc_statfs(struct svc_rqst * rqstp, struct nfsd_fhandle   *argp,
					  struct nfsd_statfsres *resp)
{
	int	nfserr;

	dprintk("nfsd: STATFS   %p\n", SVCFH_DENTRY(&argp->fh));

	nfserr = nfsd_statfs(rqstp, &argp->fh, &resp->stats);
	fh_put(&argp->fh);
	RETURN(nfserr);
}

/*
 * NFSv2 Server procedures.
 * Only the results of non-idempotent operations are cached.
 */
#define nfsd_proc_none		NULL
#define nfssvc_release_none	NULL
struct nfsd_void { int dummy; };

#define PROC(name, argt, rest, relt, cache)	\
 { (svc_procfunc) nfsd_proc_##name,		\
   (kxdrproc_t) nfssvc_decode_##argt,		\
   (kxdrproc_t) nfssvc_encode_##rest,		\
   (kxdrproc_t) nfssvc_release_##relt,		\
   sizeof(struct nfsd_##argt),			\
   sizeof(struct nfsd_##rest),			\
   0,						\
   cache					\
 }
struct svc_procedure		nfsd_procedures2[18] = {
  PROC(null,	 void,		void,		none,		RC_NOCACHE),
  PROC(getattr,	 fhandle,	attrstat,	fhandle,	RC_NOCACHE),
  PROC(setattr,  sattrargs,	attrstat,	fhandle,	RC_REPLBUFF),
  PROC(none,	 void,		void,		none,		RC_NOCACHE),
  PROC(lookup,	 diropargs,	diropres,	fhandle,	RC_NOCACHE),
  PROC(readlink, fhandle,	readlinkres,	none,		RC_NOCACHE),
  PROC(read,	 readargs,	readres,	fhandle,	RC_NOCACHE),
  PROC(none,	 void,		void,		none,		RC_NOCACHE),
  PROC(write,	 writeargs,	attrstat,	fhandle,	RC_REPLBUFF),
  PROC(create,	 createargs,	diropres,	fhandle,	RC_REPLBUFF),
  PROC(remove,	 diropargs,	void,		none,		RC_REPLSTAT),
  PROC(rename,	 renameargs,	void,		none,		RC_REPLSTAT),
  PROC(link,	 linkargs,	void,		none,		RC_REPLSTAT),
  PROC(symlink,	 symlinkargs,	void,		none,		RC_REPLSTAT),
  PROC(mkdir,	 createargs,	diropres,	fhandle,	RC_REPLBUFF),
  PROC(rmdir,	 diropargs,	void,		none,		RC_REPLSTAT),
  PROC(readdir,	 readdirargs,	readdirres,	none,		RC_REPLSTAT),
  PROC(statfs,	 fhandle,	statfsres,	none,		RC_NOCACHE),
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
nfsd_dump(char *tag, u32 *buf, int len)
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

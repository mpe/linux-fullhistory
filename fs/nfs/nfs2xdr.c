/*
 * linux/fs/nfs/xdr.c
 *
 * XDR functions to encode/decode NFS RPC arguments and results.
 *
 * Copyright (C) 1992, 1993, 1994  Rick Sladkey
 * Copyright (C) 1996 Olaf Kirch
 */

#define NFS_NEED_XDR_TYPES

#include <linux/param.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/utsname.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>

/* Uncomment this to support servers requiring longword lengths */
#define NFS_PAD_WRITES 1

#define NFSDBG_FACILITY		NFSDBG_XDR
/* #define NFS_PARANOIA 1 */

#define QUADLEN(len)		(((len) + 3) >> 2)
static int			nfs_stat_to_errno(int stat);

/* Mapping from NFS error code to "errno" error code. */
#define errno_NFSERR_IO		EIO

/*
 * Declare the space requirements for NFS arguments and replies as
 * number of 32bit-words
 */
#define NFS_fhandle_sz		8
#define NFS_sattr_sz		8
#define NFS_filename_sz		1+(NFS_MAXNAMLEN>>2)
#define NFS_path_sz		1+(NFS_MAXPATHLEN>>2)
#define NFS_fattr_sz		17
#define NFS_info_sz		5
#define NFS_entry_sz		NFS_filename_sz+3

#define NFS_enc_void_sz		0
#define NFS_diropargs_sz	NFS_fhandle_sz+NFS_filename_sz
#define NFS_sattrargs_sz	NFS_fhandle_sz+NFS_sattr_sz
#define NFS_readargs_sz		NFS_fhandle_sz+3
#define NFS_writeargs_sz	NFS_fhandle_sz+4
#define NFS_createargs_sz	NFS_diropargs_sz+NFS_sattr_sz
#define NFS_renameargs_sz	NFS_diropargs_sz+NFS_diropargs_sz
#define NFS_linkargs_sz		NFS_fhandle_sz+NFS_diropargs_sz
#define NFS_symlinkargs_sz	NFS_diropargs_sz+NFS_path_sz+NFS_sattr_sz
#define NFS_readdirargs_sz	NFS_fhandle_sz+2

#define NFS_dec_void_sz		0
#define NFS_attrstat_sz		1+NFS_fattr_sz
#define NFS_diropres_sz		1+NFS_fhandle_sz+NFS_fattr_sz
#define NFS_readlinkres_sz	1+NFS_path_sz
#define NFS_readres_sz		1+NFS_fattr_sz+1
#define NFS_stat_sz		1
#define NFS_readdirres_sz	1
#define NFS_statfsres_sz	1+NFS_info_sz

/*
 * Common NFS XDR functions as inlines
 */
static inline u32 *
xdr_encode_fhandle(u32 *p, struct nfs_fh *fhandle)
{
	*((struct nfs_fh *) p) = *fhandle;
	return p + QUADLEN(sizeof(*fhandle));
}

static inline u32 *
xdr_decode_fhandle(u32 *p, struct nfs_fh *fhandle)
{
	*fhandle = *((struct nfs_fh *) p);
	return p + QUADLEN(sizeof(*fhandle));
}

static inline u32 *
xdr_decode_string2(u32 *p, char **string, unsigned int *len,
			unsigned int maxlen)
{
	*len = ntohl(*p++);
	if (*len > maxlen)
		return NULL;
	*string = (char *) p;
	return p + QUADLEN(*len);
}

static inline u32 *
xdr_decode_fattr(u32 *p, struct nfs_fattr *fattr)
{
	fattr->type = (enum nfs_ftype) ntohl(*p++);
	fattr->mode = ntohl(*p++);
	fattr->nlink = ntohl(*p++);
	fattr->uid = ntohl(*p++);
	fattr->gid = ntohl(*p++);
	fattr->size = ntohl(*p++);
	fattr->blocksize = ntohl(*p++);
	fattr->rdev = ntohl(*p++);
	fattr->blocks = ntohl(*p++);
	fattr->fsid = ntohl(*p++);
	fattr->fileid = ntohl(*p++);
	fattr->atime.seconds = ntohl(*p++);
	fattr->atime.useconds = ntohl(*p++);
	fattr->mtime.seconds = ntohl(*p++);
	fattr->mtime.useconds = ntohl(*p++);
	fattr->ctime.seconds = ntohl(*p++);
	fattr->ctime.useconds = ntohl(*p++);
	return p;
}

static inline u32 *
xdr_encode_sattr(u32 *p, struct nfs_sattr *sattr)
{
	*p++ = htonl(sattr->mode);
	*p++ = htonl(sattr->uid);
	*p++ = htonl(sattr->gid);
	*p++ = htonl(sattr->size);
	*p++ = htonl(sattr->atime.seconds);
	*p++ = htonl(sattr->atime.useconds);
	*p++ = htonl(sattr->mtime.seconds);
	*p++ = htonl(sattr->mtime.useconds);
	return p;
}

/*
 * NFS encode functions
 */
/*
 * Encode void argument
 */
static int
nfs_xdr_enc_void(struct rpc_rqst *req, u32 *p, void *dummy)
{
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

/*
 * Encode file handle argument
 * GETATTR, READLINK, STATFS
 */
static int
nfs_xdr_fhandle(struct rpc_rqst *req, u32 *p, struct nfs_fh *fh)
{
	p = xdr_encode_fhandle(p, fh);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

/*
 * Encode SETATTR arguments
 */
static int
nfs_xdr_sattrargs(struct rpc_rqst *req, u32 *p, struct nfs_sattrargs *args)
{
	p = xdr_encode_fhandle(p, args->fh);
	p = xdr_encode_sattr(p, args->sattr);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

/*
 * Encode directory ops argument
 * LOOKUP, REMOVE, RMDIR
 */
static int
nfs_xdr_diropargs(struct rpc_rqst *req, u32 *p, struct nfs_diropargs *args)
{
	p = xdr_encode_fhandle(p, args->fh);
	p = xdr_encode_string(p, args->name);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

/*
 * Arguments to a READ call. Since we read data directly into the page
 * cache, we also set up the reply iovec here so that iov[1] points
 * exactly to the page we want to fetch.
 */
static int
nfs_xdr_readargs(struct rpc_rqst *req, u32 *p, struct nfs_readargs *args)
{
	struct rpc_auth	*auth = req->rq_task->tk_auth;
	int		replen, buflen;

	p = xdr_encode_fhandle(p, args->fh);
	*p++ = htonl(args->offset);
	*p++ = htonl(args->count);
	*p++ = htonl(args->count);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);

#if 1
	/* set up reply iovec */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + NFS_readres_sz) << 2;
	buflen = req->rq_rvec[0].iov_len;
	req->rq_rvec[0].iov_len  = replen;
	req->rq_rvec[1].iov_base = args->buffer;
	req->rq_rvec[1].iov_len  = args->count;
	req->rq_rvec[2].iov_base = (u8 *) req->rq_rvec[0].iov_base + replen;
	req->rq_rvec[2].iov_len  = buflen - replen;
	req->rq_rlen = args->count + buflen;
	req->rq_rnr = 3;
#else
	replen = (RPC_REPHDRSIZE + auth->au_rslack + NFS_readres_sz) << 2;
	req->rq_rvec[0].iov_len  = replen;
#endif

	return 0;
}

/*
 * Decode READ reply
 */
static int
nfs_xdr_readres(struct rpc_rqst *req, u32 *p, struct nfs_readres *res)
{
	struct iovec *iov = req->rq_rvec;
	int	status, count, recvd, hdrlen;

	dprintk("RPC:      readres OK status %lx\n", (long)ntohl(*p));
	if ((status = ntohl(*p++)))
		return -nfs_stat_to_errno(status);
	p = xdr_decode_fattr(p, res->fattr);

	count = ntohl(*p++);
	hdrlen = (u8 *) p - (u8 *) iov->iov_base;
	recvd = req->rq_rlen - hdrlen;
	if (p != iov[2].iov_base) {
		/* Unexpected reply header size. Punt.
		 * XXX: Move iovec contents to align data on page
		 * boundary and adjust RPC header size guess */
		printk("NFS: Odd RPC header size in read reply: %d\n", hdrlen);
		return -errno_NFSERR_IO;
	}
	if (count > recvd) {
		printk("NFS: server cheating in read reply: "
			"count %d > recvd %d\n", count, recvd);
		count = recvd;
	}

	dprintk("RPC:      readres OK count %d\n", count);
	if (count < res->count)
		memset((u8 *)(iov[1].iov_base+count), 0, res->count-count);

	return count;
}


/*
 * Write arguments. Splice the buffer to be written into the iovec.
 */
static int
nfs_xdr_writeargs(struct rpc_rqst *req, u32 *p, struct nfs_writeargs *args)
{
	u32 count = args->count;

	p = xdr_encode_fhandle(p, args->fh);
	*p++ = htonl(args->offset);
	*p++ = htonl(args->offset);
	*p++ = htonl(count);
	*p++ = htonl(count);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);

	req->rq_svec[1].iov_base = (void *) args->buffer;
	req->rq_svec[1].iov_len = count;
	req->rq_slen += count;
	req->rq_snr = 2;

#ifdef NFS_PAD_WRITES
	/*
	 * Some old servers require that the message length
	 * be a multiple of 4, so we pad it here if needed.
	 */
	count = ((count + 3) & ~3) - count;
	if (count) {
#if 0
printk("nfs_writeargs: padding write, len=%d, slen=%d, pad=%d\n",
req->rq_svec[1].iov_len, req->rq_slen, count);
#endif
		req->rq_svec[2].iov_base = (void *) "\0\0\0";
		req->rq_svec[2].iov_len  = count;
		req->rq_slen += count;
		req->rq_snr = 3;
	}
#endif

	return 0;
}

/*
 * Encode create arguments
 * CREATE, MKDIR
 */
static int
nfs_xdr_createargs(struct rpc_rqst *req, u32 *p, struct nfs_createargs *args)
{
	p = xdr_encode_fhandle(p, args->fh);
	p = xdr_encode_string(p, args->name);
	p = xdr_encode_sattr(p, args->sattr);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

/*
 * Encode RENAME arguments
 */
static int
nfs_xdr_renameargs(struct rpc_rqst *req, u32 *p, struct nfs_renameargs *args)
{
	p = xdr_encode_fhandle(p, args->fromfh);
	p = xdr_encode_string(p, args->fromname);
	p = xdr_encode_fhandle(p, args->tofh);
	p = xdr_encode_string(p, args->toname);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

/*
 * Encode LINK arguments
 */
static int
nfs_xdr_linkargs(struct rpc_rqst *req, u32 *p, struct nfs_linkargs *args)
{
	p = xdr_encode_fhandle(p, args->fromfh);
	p = xdr_encode_fhandle(p, args->tofh);
	p = xdr_encode_string(p, args->toname);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

/*
 * Encode SYMLINK arguments
 */
static int
nfs_xdr_symlinkargs(struct rpc_rqst *req, u32 *p, struct nfs_symlinkargs *args)
{
	p = xdr_encode_fhandle(p, args->fromfh);
	p = xdr_encode_string(p, args->fromname);
	p = xdr_encode_string(p, args->topath);
	p = xdr_encode_sattr(p, args->sattr);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

/*
 * Encode arguments to readdir call
 */
static int
nfs_xdr_readdirargs(struct rpc_rqst *req, u32 *p, struct nfs_readdirargs *args)
{
	struct rpc_task	*task = req->rq_task;
	struct rpc_auth	*auth = task->tk_auth;
	u32		bufsiz = args->bufsiz;
	int		replen;

	/*
	 * Some servers (e.g. HP OS 9.5) seem to expect the buffer size
	 * to be in longwords ... check whether to convert the size.
	 */
	if (task->tk_client->cl_flags & NFS_CLNTF_BUFSIZE)
		bufsiz = bufsiz >> 2;

	p = xdr_encode_fhandle(p, args->fh);
	*p++ = htonl(args->cookie);
	*p++ = htonl(bufsiz); /* see above */
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);

	/* set up reply iovec */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + NFS_readdirres_sz) << 2;
	/*
	dprintk("RPC: readdirargs: slack is 4 * (%d + %d + %d) = %d\n",
		RPC_REPHDRSIZE, auth->au_rslack, NFS_readdirres_sz, replen);
	 */
	req->rq_rvec[0].iov_len  = replen;
	req->rq_rvec[1].iov_base = args->buffer;
	req->rq_rvec[1].iov_len  = args->bufsiz;
	req->rq_rlen = replen + args->bufsiz;
	req->rq_rnr = 2;

	/*
	dprintk("RPC:      readdirargs set up reply vec:\n");
	dprintk("          rvec[0] = %p/%d\n",
			req->rq_rvec[0].iov_base,
			req->rq_rvec[0].iov_len);
	dprintk("          rvec[1] = %p/%d\n",
			req->rq_rvec[1].iov_base,
			req->rq_rvec[1].iov_len);
	 */

	return 0;
}

/*
 * Decode the result of a readdir call. We decode the result in place
 * to avoid a malloc of NFS_MAXNAMLEN+1 for each file name.
 * After decoding, the layout in memory looks like this:
 *	entry1 entry2 ... entryN <space> stringN ... string2 string1
 * Each entry consists of three __u32 values, the same space as NFS uses.
 * Note that the strings are not null-terminated so that the entire number
 * of entries returned by the server should fit into the buffer.
 */
static int
nfs_xdr_readdirres(struct rpc_rqst *req, u32 *p, struct nfs_readdirres *res)
{
	struct iovec		*iov = req->rq_rvec;
	int			 status, nr;
	char			*string, *start;
	u32			*end, *entry, len, fileid, cookie;

	if ((status = ntohl(*p++)))
		return -nfs_stat_to_errno(status);
	if ((void *) p != ((u8 *) iov->iov_base+iov->iov_len)) {
		/* Unexpected reply header size. Punt. */
		printk("NFS: Odd RPC header size in readdirres reply\n");
		return -errno_NFSERR_IO;
	}

	/* Get start and end address of XDR data */
	p   = (u32 *) iov[1].iov_base;
	end = (u32 *) ((u8 *) p + iov[1].iov_len);

	/* Get start and end of dirent buffer */
	entry  = (u32 *) res->buffer;
	start  = (char *) res->buffer;
	string = (char *) res->buffer + res->bufsiz;
	for (nr = 0; *p++; nr++) {
		fileid = ntohl(*p++);

		len = ntohl(*p++);
		/*
		 * Check whether the server has exceeded our reply buffer,
		 * and set a flag to convert the size to longwords.
		 */
		if ((p + QUADLEN(len) + 3) > end) {
			struct rpc_clnt *clnt = req->rq_task->tk_client;
			printk(KERN_WARNING
				"NFS: server %s, readdir reply truncated\n",
				clnt->cl_server);
			printk(KERN_WARNING "NFS: nr=%d, slots=%d, len=%d\n",
				nr, (end - p), len);
			clnt->cl_flags |= NFS_CLNTF_BUFSIZE;
			break;
		}
		if (len > NFS_MAXNAMLEN) {
			printk("NFS: giant filename in readdir (len %x)!\n",
						len);
			return -errno_NFSERR_IO;
		}
		string -= len;
		if ((void *) (entry+3) > (void *) string) {
			/* 
			 * This error is impossible as long as the temp
			 * buffer is no larger than the user buffer. The 
			 * current packing algorithm uses the same amount
			 * of space in the user buffer as in the XDR data,
			 * so it's guaranteed to fit.
			 */
			printk("NFS: incorrect buffer size in %s!\n",
				__FUNCTION__);
			break;
		}

		memmove(string, p, len);
		p += QUADLEN(len);
		cookie = ntohl(*p++);
		/*
		 * To make everything fit, we encode the length, offset,
		 * and eof flag into 32 bits. This works for filenames
		 * up to 32K and PAGE_SIZE up to 64K.
		 */
		status = !p[0] && p[1] ? (1 << 15) : 0; /* eof flag */
		*entry++ = fileid;
		*entry++ = cookie;
		*entry++ = ((string - start) << 16) | status | (len & 0x7FFF);
	}
#ifdef NFS_PARANOIA
printk("nfs_xdr_readdirres: %d entries, ent sp=%d, str sp=%d\n",
nr, ((char *) entry - start), (start + res->bufsiz - string));
#endif
	return nr;
}

/*
 * NFS XDR decode functions
 */
/*
 * Decode void reply
 */
static int
nfs_xdr_dec_void(struct rpc_rqst *req, u32 *p, void *dummy)
{
	return 0;
}

/*
 * Decode simple status reply
 */
static int
nfs_xdr_stat(struct rpc_rqst *req, u32 *p, void *dummy)
{
	int	status;

	if ((status = ntohl(*p++)) != 0)
		status = -nfs_stat_to_errno(status);
	return status;
}

/*
 * Decode attrstat reply
 * GETATTR, SETATTR, WRITE
 */
static int
nfs_xdr_attrstat(struct rpc_rqst *req, u32 *p, struct nfs_fattr *fattr)
{
	int	status;

	dprintk("RPC:      attrstat status %lx\n", (long)ntohl(*p));
	if ((status = ntohl(*p++)))
		return -nfs_stat_to_errno(status);
	xdr_decode_fattr(p, fattr);
	dprintk("RPC:      attrstat OK type %d mode %o dev %x ino %x\n",
		fattr->type, fattr->mode, fattr->fsid, fattr->fileid);
	return 0;
}

/*
 * Decode diropres reply
 * LOOKUP, CREATE, MKDIR
 */
static int
nfs_xdr_diropres(struct rpc_rqst *req, u32 *p, struct nfs_diropok *res)
{
	int	status;

	dprintk("RPC:      diropres status %lx\n", (long)ntohl(*p));
	if ((status = ntohl(*p++)))
		return -nfs_stat_to_errno(status);
	p = xdr_decode_fhandle(p, res->fh);
	xdr_decode_fattr(p, res->fattr);
	dprintk("RPC:      diropres OK type %x mode %o dev %x ino %x\n",
		res->fattr->type, res->fattr->mode,
		res->fattr->fsid, res->fattr->fileid);
	return 0;
}

/*
 * Decode READLINK reply
 */
static int
nfs_xdr_readlinkres(struct rpc_rqst *req, u32 *p, struct nfs_readlinkres *res)
{
	int	status;

	if ((status = ntohl(*p++)))
		return -nfs_stat_to_errno(status);
	xdr_decode_string2(p, res->string, res->lenp, res->maxlen);

	/* Caller takes over the buffer here to avoid extra copy */
	res->buffer = req->rq_task->tk_buffer;
	req->rq_task->tk_buffer = NULL;
	return 0;
}

/*
 * Decode STATFS reply
 */
static int
nfs_xdr_statfsres(struct rpc_rqst *req, u32 *p, struct nfs_fsinfo *res)
{
	int	status;

	if ((status = ntohl(*p++)))
		return -nfs_stat_to_errno(status);
	res->tsize = ntohl(*p++);
	res->bsize = ntohl(*p++);
	res->blocks = ntohl(*p++);
	res->bfree = ntohl(*p++);
	res->bavail = ntohl(*p++);
	return 0;
}

/*
 * We need to translate between nfs status return values and
 * the local errno values which may not be the same.
 */
static struct {
	int stat;
	int errno;
} nfs_errtbl[] = {
	{ NFS_OK,		0		},
	{ NFSERR_PERM,		EPERM		},
	{ NFSERR_NOENT,		ENOENT		},
	{ NFSERR_IO,		errno_NFSERR_IO	},
	{ NFSERR_NXIO,		ENXIO		},
	{ NFSERR_EAGAIN,	EAGAIN		},
	{ NFSERR_ACCES,		EACCES		},
	{ NFSERR_EXIST,		EEXIST		},
	{ NFSERR_XDEV,		EXDEV		},
	{ NFSERR_NODEV,		ENODEV		},
	{ NFSERR_NOTDIR,	ENOTDIR		},
	{ NFSERR_ISDIR,		EISDIR		},
	{ NFSERR_INVAL,		EINVAL		},
	{ NFSERR_FBIG,		EFBIG		},
	{ NFSERR_NOSPC,		ENOSPC		},
	{ NFSERR_ROFS,		EROFS		},
	{ NFSERR_NAMETOOLONG,	ENAMETOOLONG	},
	{ NFSERR_NOTEMPTY,	ENOTEMPTY	},
	{ NFSERR_DQUOT,		EDQUOT		},
	{ NFSERR_STALE,		ESTALE		},
#ifdef EWFLUSH
	{ NFSERR_WFLUSH,	EWFLUSH		},
#endif
	{ -1,			EIO		}
};

static int
nfs_stat_to_errno(int stat)
{
	int i;

	for (i = 0; nfs_errtbl[i].stat != -1; i++) {
		if (nfs_errtbl[i].stat == stat)
			return nfs_errtbl[i].errno;
	}
	printk("nfs_stat_to_errno: bad nfs status return value: %d\n", stat);
	return nfs_errtbl[i].errno;
}

#ifndef MAX
# define MAX(a, b)	(((a) > (b))? (a) : (b))
#endif

#define PROC(proc, argtype, restype)	\
    { "nfs_" #proc,					\
      (kxdrproc_t) nfs_xdr_##argtype,			\
      (kxdrproc_t) nfs_xdr_##restype,			\
      MAX(NFS_##argtype##_sz,NFS_##restype##_sz) << 2	\
    }

static struct rpc_procinfo	nfs_procedures[18] = {
    PROC(null,		enc_void,	dec_void),
    PROC(getattr,	fhandle,	attrstat),
    PROC(setattr,	sattrargs,	attrstat),
    PROC(root,		enc_void,	dec_void),
    PROC(lookup,	diropargs,	diropres),
    PROC(readlink,	fhandle,	readlinkres),
    PROC(read,		readargs,	readres),
    PROC(writecache,	enc_void,	dec_void),
    PROC(write,		writeargs,	attrstat),
    PROC(create,	createargs,	diropres),
    PROC(remove,	diropargs,	stat),
    PROC(rename,	renameargs,	stat),
    PROC(link,		linkargs,	stat),
    PROC(symlink,	symlinkargs,	stat),
    PROC(mkdir,		createargs,	diropres),
    PROC(rmdir,		diropargs,	stat),
    PROC(readdir,	readdirargs,	readdirres),
    PROC(statfs,	fhandle,	statfsres),
};

static struct rpc_version	nfs_version2 = {
	2,
	sizeof(nfs_procedures)/sizeof(nfs_procedures[0]),
	nfs_procedures
};

static struct rpc_version *	nfs_version[] = {
	NULL,
	NULL,
	&nfs_version2
};

struct rpc_program	nfs_program = {
	"nfs",
	NFS_PROGRAM,
	sizeof(nfs_version) / sizeof(nfs_version[0]),
	nfs_version,
	&nfs_rpcstat,
};

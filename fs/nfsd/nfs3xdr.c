/*
 * linux/fs/nfsd/nfs3xdr.c
 *
 * XDR support for nfsd/protocol version 3.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/nfs3.h>

#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/xdr3.h>

#define NFSDDBG_FACILITY		NFSDDBG_XDR

u32	nfs_ok, nfserr_perm, nfserr_noent, nfserr_io, nfserr_nxio,
	nfserr_acces, nfserr_exist, nfserr_nodev, nfserr_notdir,
	nfserr_isdir, nfserr_fbig, nfserr_nospc, nfserr_rofs,
	nfserr_nametoolong, nfserr_dquot, nfserr_stale;

#ifdef NFSD_OPTIMIZE_SPACE
# define inline
#endif

/*
 * Mapping of S_IF* types to NFS file types
 */
static u32	nfs3_ftypes[] = {
	NF3NON,  NF3FIFO, NF3CHR, NF3BAD,
	NF3DIR,  NF3BAD,  NF3BLK, NF3BAD,
	NF3REG,  NF3BAD,  NF3LNK, NF3BAD,
	NF3SOCK, NF3BAD,  NF3LNK, NF3BAD,
};

/*
 * Initialization of NFS status variables
 */
void
nfs3xdr_init(void)
{
	static int	inited = 0;

	if (inited)
		return;

	nfs_ok = htonl(NFS_OK);
	nfserr_perm = htonl(NFSERR_PERM);
	nfserr_noent = htonl(NFSERR_NOENT);
	nfserr_io = htonl(NFSERR_IO);
	nfserr_nxio = htonl(NFSERR_NXIO);
	nfserr_acces = htonl(NFSERR_ACCES);
	nfserr_exist = htonl(NFSERR_EXIST);
	nfserr_nodev = htonl(NFSERR_NODEV);
	nfserr_notdir = htonl(NFSERR_NOTDIR);
	nfserr_isdir = htonl(NFSERR_ISDIR);
	nfserr_fbig = htonl(NFSERR_FBIG);
	nfserr_nospc = htonl(NFSERR_NOSPC);
	nfserr_rofs = htonl(NFSERR_ROFS);
	nfserr_nametoolong = htonl(NFSERR_NAMETOOLONG);
	nfserr_dquot = htonl(NFSERR_DQUOT);
	nfserr_stale = htonl(NFSERR_STALE);

	inited = 1;
}

/*
 * XDR functions for basic NFS types
 */
static inline u32 *
enc64(u32 *p, u64 val)
{
	*p++ = (val >> 32);
	*p++ = (val & 0xffffffff);
	return p;
}

static inline u32 *
dec64(u32 *p, u64 *valp)
{
	*valp  = ((u64) ntohl(*p++)) << 32;
	*valp |= ntohl(*p++);
	return p;
}

static inline u32 *
encode_time3(u32 *p, time_t secs)
{
	*p++ = htonl((u32) secs); *p++ = 0;
	return p;
}

static inline u32 *
decode_time3(u32 *p, time_t *secp)
{
	*secp = ntohl(*p++);
	return p + 1;
}

static inline u32 *
decode_fh(u32 *p, struct svc_fh *fhp)
{
	if (*p++ != sizeof(struct knfs_fh))
		return NULL;

	memcpy(&fhp->fh_handle, p, sizeof(struct knfs_fh));
	fhp->fh_inode  = NULL;
	fhp->fh_export = NULL;

	return p + (sizeof(struct knfs_fh) >> 2);
}

static inline u32 *
encode_fh(u32 *p, struct svc_fh *fhp)
{
	*p++ = htonl(sizeof(struct knfs_fh));
	memcpy(p, &fhp->fh_handle, sizeof(struct knfs_fh));
	return p + (sizeof(struct knfs_fh) >> 2);
}

/*
 * Decode a file name and make sure that the path contains
 * no slashes or null bytes.
 */
static inline u32 *
decode_filename(u32 *p, char **namp, int *lenp)
{
	char		*name;
	int		i;

	if ((p = xdr_decode_string(p, namp, lenp, NFS3_MAXNAMLEN)) != NULL) {
		for (i = 0, name = *namp; i < *lenp; i++, name++) {
			if (*name == '\0' || *name == '/')
				return NULL;
		}
		*name = '\0';
	}

	return p;
}

static inline u32 *
decode_pathname(u32 *p, char **namp, int *lenp)
{
	char		*name;
	int		i;

	if ((p = xdr_decode_string(p, namp, lenp, NFS3_MAXPATHLEN)) != NULL) {
		for (i = 0, name = *namp; i < *lenp; i++, name++) {
			if (*name == '\0')
				return NULL;
		}
		*name = '\0';
	}

	return p;
}

static inline u32 *
decode_sattr3(u32 *p, struct iattr *iap)
{
	u32	tmp;

	iap->ia_valid = 0;

	if (*p++) {
		iap->ia_valid |= ATTR_MODE;
		iap->ia_mode = ntohl(*p++);
	}
	if (*p++) {
		iap->ia_valid |= ATTR_UID;
		iap->ia_uid = ntohl(*p++);
	}
	if (*p++) {
		iap->ia_valid |= ATTR_GID;
		iap->ia_gid = ntohl(*p++);
	}
	if (*p++) {
		iap->ia_valid |= ATTR_SIZE;
		iap->ia_size = ntohl(*p++);
	}
	if ((tmp = *p++) == 1) {
		iap->ia_valid |= ATTR_ATIME;
	} else if (tmp == 2) {
		iap->ia_valid |= ATTR_ATIME | ATTR_ATIME_SET;
		iap->ia_atime = ntohl(*p++), p++;
	}
	if ((tmp = *p++) != 0) {
		iap->ia_valid |= ATTR_MTIME | ATTR_MTIME_SET;
	} else if (tmp == 2) {
		iap->ia_valid |= ATTR_MTIME;
		iap->ia_mtime = ntohl(*p++), p++;
	}
	return p;
}

static inline u32 *
encode_fattr3(struct svc_rqst *rqstp, u32 *p, struct inode *inode)
{
	if (!inode) {
		printk("nfsd: NULL inode in %s:%d", __FILE__, __LINE__);
		return NULL;
	}

	*p++ = htonl(nfs3_ftypes[(inode->i_mode & S_IFMT) >> 12]);
	*p++ = htonl((u32) inode->i_mode);
	*p++ = htonl((u32) inode->i_nlink);
	*p++ = htonl((u32) nfsd_ruid(rqstp, inode->i_uid));
	*p++ = htonl((u32) nfsd_rgid(rqstp, inode->i_gid));
	if (S_ISLNK(inode->i_mode) && inode->i_size > NFS3_MAXPATHLEN) {
		p = enc64(p, (u64) NFS3_MAXPATHLEN);
	} else {
		p = enc64(p, (u64) inode->i_size);
	}
	p = enc64(p, inode->i_blksize * inode->i_blocks);
	*p++ = htonl((u32) MAJOR(inode->i_rdev));
	*p++ = htonl((u32) MINOR(inode->i_rdev));
	p = enc64(p, (u64) inode->i_dev);
	p = enc64(p, (u64) inode->i_ino);
	p = encode_time3(p, inode->i_atime);
	p = encode_time3(p, inode->i_mtime);
	p = encode_time3(p, inode->i_ctime);

	return p;
}

/*
 * Encode post-operation attributes.
 * The inode may be NULL if the call failed because of a stale file
 * handle. In this case, no attributes are returned.
 */
static u32 *
encode_post_op_attr(struct svc_rqst *rqstp, u32 *p, struct inode *inode)
{
	if (inode == NULL) {
		*p++ = xdr_zero;
		return p;
	}
	return encode_fattr3(rqstp, p, inode);
}

/*
 * Enocde weak cache consistency data
 */
static u32 *
encode_wcc_data(struct svc_rqst *rqstp, u32 *p, struct svc_fh *fhp)
{
	struct inode	*inode = fhp->fh_inode;

	if (fhp->fh_post_version == inode->i_version) {
		*p++ = xdr_one;
		p = enc64(p, (u64) fhp->fh_pre_size);
		p = encode_time3(p, fhp->fh_pre_mtime);
		p = encode_time3(p, fhp->fh_pre_ctime);
	} else {
		*p++ = xdr_zero;
	}
	return encode_post_op_attr(rqstp, p, inode);
}

/*
 * Check buffer bounds after decoding arguments
 */
static inline int
xdr_argsize_check(struct svc_rqst *rqstp, u32 *p)
{
	struct svc_buf	*buf = &rqstp->rq_argbuf;

	return p - buf->base <= buf->buflen;
}

static inline int
xdr_ressize_check(struct svc_rqst *rqstp, u32 *p)
{
	struct svc_buf	*buf = &rqstp->rq_resbuf;

	buf->len = p - buf->base;
	dprintk("nfsd: ressize_check p %p base %p len %d\n",
			p, buf->base, buf->buflen);
	return (buf->len <= buf->buflen);
}

/*
 * XDR decode functions
 */
int
nfs3svc_decode_fhandle(struct svc_rqst *rqstp, u32 *p, struct svc_fh *fhp)
{
	if (!(p = decode_fh(p, fhp)))
		return 0;
	return xdr_argsize_check(rqstp, p);
}

int
nfs3svc_decode_sattrargs(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_sattrargs *args)
{
	if (!(p = decode_fh(p, &args->fh))
	 || !(p = decode_sattr3(p, &args->attrs))
	 || (*p++ && !(p = decode_time3(p, &args->guardtime))))
		return 0;

	return xdr_argsize_check(rqstp, p);
}

int
nfs3svc_decode_diropargs(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_diropargs *args)
{
	if (!(p = decode_fh(p, &args->fh))
	 || !(p = decode_filename(p, &args->name, &args->len)))
		return 0;

	return xdr_argsize_check(rqstp, p);
}

int
nfs3svc_decode_accessargs(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_accessargs *args)
{
	if (!(p = decode_fh(p, &args->fh)))
		return 0;
	args->access = ntohl(*p++);

	return xdr_argsize_check(rqstp, p);
}

int
nfs3svc_decode_readargs(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_readargs *args)
{
	if (!(p = decode_fh(p, &args->fh))
	 || !(p = dec64(p, &args->offset))
	 || !(p = dec64(p, &args->count)))
		return 0;

	return xdr_argsize_check(rqstp, p);
}

int
nfs3svc_decode_writeargs(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_writeargs *args)
{
	if (!(p = decode_fh(p, &args->fh))
	 || !(p = dec64(p, &args->offset))
	 || !(p = dec64(p, &args->count)))
		return 0;

	args->stable = ntohl(*p++);
	args->len = ntohl(*p++);
	args->data = (char *) p;
	p += (args->len + 3) >> 2;

	return xdr_argsize_check(rqstp, p);
}

int
nfs3svc_decode_createargs(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_createargs *args)
{
	if (!(p = decode_fh(p, &args->fh))
	 || !(p = decode_filename(p, &args->name, &args->len)))
		return 0;

	switch (args->createmode = ntohl(*p++)) {
	case 0: case 1:
		if (!(p = decode_sattr3(p, &args->attrs)))
			return 0;
		break;
	case 2:
		args->verf = p;
		p += 2;
		break;
	default:
		return 0;
	}

	return xdr_argsize_check(rqstp, p);
}
int
nfs3svc_decode_mkdirargs(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_createargs *args)
{
	if (!(p = decode_fh(p, &args->fh))
	 || !(p = decode_filename(p, &args->name, &args->len))
	 || !(p = decode_sattr3(p, &args->attrs)))
		return 0;

	return xdr_argsize_check(rqstp, p);
}

int
nfs3svc_decode_symlinkargs(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_symlinkargs *args)
{
	if (!(p = decode_fh(p, &args->ffh))
	 || !(p = decode_filename(p, &args->fname, &args->flen))
	 || !(p = decode_sattr3(p, &args->attrs))
	 || !(p = decode_pathname(p, &args->tname, &args->tlen)))
		return 0;

	return xdr_argsize_check(rqstp, p);
}

int
nfs3svc_decode_mknodargs(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_mknodargs *args)
{
	if (!(p = decode_fh(p, &args->fh))
	 || !(p = decode_filename(p, &args->name, &args->len)))
		return 0;

	args->ftype = ntohl(*p++);

	if (args->ftype == NF3BLK  || args->ftype == NF3CHR
	 || args->ftype == NF3SOCK || args->ftype == NF3FIFO) {
		if (!(p = decode_sattr3(p, &args->attrs)))
			return 0;
	}

	if (args->ftype == NF3BLK || args->ftype == NF3CHR) {
		args->major = ntohl(*p++);
		args->minor = ntohl(*p++);
	}

	return xdr_argsize_check(rqstp, p);
}

int
nfs3svc_decode_renameargs(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_renameargs *args)
{
	if (!(p = decode_fh(p, &args->ffh))
	 || !(p = decode_filename(p, &args->fname, &args->flen))
	 || !(p = decode_fh(p, &args->tfh))
	 || !(p = decode_filename(p, &args->tname, &args->tlen)))
		return 0;

	return xdr_argsize_check(rqstp, p);
}

int
nfs3svc_decode_linkargs(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_linkargs *args)
{
	if (!(p = decode_fh(p, &args->ffh))
	 || !(p = decode_fh(p, &args->tfh))
	 || !(p = decode_filename(p, &args->tname, &args->tlen)))
		return 0;

	return xdr_argsize_check(rqstp, p);
}

int
nfs3svc_decode_readdirargs(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_readdirargs *args)
{
	if (!(p = decode_fh(p, &args->fh)))
		return 0;
	args->cookie = ntohl(*p++);
	args->verf   = p; p += 2;
	args->count  = ntohl(*p++);

	return xdr_argsize_check(rqstp, p);
}

int
nfs3svc_decode_readdirplusargs(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_readdirargs *args)
{
	if (!(p = decode_fh(p, &args->fh)))
		return 0;
	args->cookie   = ntohl(*p++);
	args->verf     = p; p += 2;
	args->dircount = ntohl(*p++);
	args->count    = ntohl(*p++);

	return xdr_argsize_check(rqstp, p);
}

int
nfs3svc_decode_commitargs(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_commitargs *args)
{
	if (!(p = decode_fh(p, &args->fh))
	 || !(p = dec64(p, &args->offset)))
		return 0;
	args->count = ntohl(*p++);

	return xdr_argsize_check(rqstp, p);
}

/*
 * XDR encode functions
 */
/* GETATTR */
int
nfs3svc_encode_attrstat(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_attrstat *resp)
{
	if (!(p = encode_fattr3(rqstp, p, resp->fh.fh_inode)))
		return 0;
	return xdr_ressize_check(rqstp, p);
}

/* SETATTR, REMOVE, RMDIR */
int
nfs3svc_encode_wccstat(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_attrstat *resp)
{
	if (!(p = encode_wcc_data(rqstp, p, &resp->fh)))
		return 0;
	return xdr_ressize_check(rqstp, p);
}

/* LOOKUP */
int
nfs3svc_encode_lookupres(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_lookupres *resp)
{
	if (resp->status == 0) {
		p = encode_fh(p, &resp->fh);
		if (!(p = encode_fattr3(rqstp, p, resp->fh.fh_inode)))
			return 0;
	}
	p = encode_post_op_attr(rqstp, p, resp->dirfh.fh_inode);
	return xdr_ressize_check(rqstp, p);
}

/* ACCESS */
int
nfs3svc_encode_accessres(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_accessres *resp)
{
	p = encode_post_op_attr(rqstp, p, resp->fh.fh_inode);
	if (resp->status == 0)
		*p++ = htonl(resp->access);
	return xdr_ressize_check(rqstp, p);
}

/* READLINK */
int
nfs3svc_encode_readlinkres(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_readlinkres *resp)
{
	p = encode_post_op_attr(rqstp, p, resp->fh.fh_inode);
	if (resp->status == 0) {
		*p++ = htonl(resp->len);
		p += XDR_QUADLEN(resp->len);
	}
	return xdr_ressize_check(rqstp, p);
}

/* READ */
int
nfs3svc_encode_readres(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_readres *resp)
{
	p = encode_post_op_attr(rqstp, p, resp->fh.fh_inode);
	if (resp->status == 0) {
		*p++ = htonl(resp->count);
		*p++ = htonl(resp->eof);
		*p++ = htonl(resp->count);	/* xdr opaque count */
		p += XDR_QUADLEN(resp->count);
	}
	return xdr_ressize_check(rqstp, p);
}

/* WRITE */
int
nfs3svc_encode_writeres(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_writeres *resp)
{
	p = encode_wcc_data(rqstp, p, &resp->fh);
	if (resp->status == 0) {
		*p++ = htonl(resp->count);
		*p++ = htonl(resp->committed);
		*p++ = htonl(nfssvc_boot.tv_sec);
		*p++ = htonl(nfssvc_boot.tv_usec);
	}
	return xdr_ressize_check(rqstp, p);
}

/* CREATE, MKDIR, SYMLINK, MKNOD */
int
nfs3svc_encode_createres(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_createres *resp)
{
	if (resp->status == 0) {
		p = encode_fh(p, &resp->fh);
		p = encode_post_op_attr(rqstp, p, resp->fh.fh_inode);
	}
	p = encode_wcc_data(rqstp, p, &resp->dirfh);
	return xdr_ressize_check(rqstp, p);
}

/* RENAME */
int
nfs3svc_encode_renameres(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_renameres *resp)
{
	p = encode_wcc_data(rqstp, p, &resp->ffh);
	p = encode_wcc_data(rqstp, p, &resp->tfh);
	return xdr_ressize_check(rqstp, p);
}

/* LINK */
int
nfs3svc_encode_linkres(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_linkres *resp)
{
	p = encode_post_op_attr(rqstp, p, resp->fh.fh_inode);
	p = encode_wcc_data(rqstp, p, &resp->tfh);
	return xdr_ressize_check(rqstp, p);
}

/* READDIR */
int
nfs3svc_encode_readdirres(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_readdirres *resp)
{
	p = encode_post_op_attr(rqstp, p, resp->fh.fh_inode);
	if (resp->status == 0) {
		/* stupid readdir cookie */
		*p++ = ntohl(resp->fh.fh_inode->i_mtime);
		*p++ = xdr_zero;
		p = resp->list_end;
	}

	return xdr_ressize_check(rqstp, p);
}

#define NFS3_ENTRYPLUS_BAGGAGE	((1 + 20 + 1 + NFS3_FHSIZE) << 2)
int
nfs3svc_encode_entry(struct readdir_cd *cd, const char *name,
				int namlen, unsigned long offset, ino_t ino)
{
	u32		*p = cd->buffer;
	int		buflen, slen, elen;
	struct svc_fh	fh;

	if (offset > ~((u64) 0))
		return -EINVAL;
	if (cd->offset)
		*cd->offset = htonl(offset);

	/* For readdirplus, look up the inode */
	if (cd->plus && nfsd_lookup(cd->rqstp, cd->dirfh, name, namlen, &fh))
		return 0;

	/* truncate filename if too long */
	if (namlen > NFS3_MAXNAMLEN)
		namlen = NFS3_MAXNAMLEN;

	slen = XDR_QUADLEN(namlen);
	elen = slen + (cd->plus? NFS3_ENTRYPLUS_BAGGAGE : 0);
	if ((buflen = cd->buflen - elen - 4) < 0) {
		cd->eob = 1;
		if (cd->plus)
			fh_put(&fh);
		return -EINVAL;
	}
	*p++ = xdr_one;			/* mark entry present */
	*p++ = xdr_zero;		/* file id (64 bit) */
	*p++ = htonl((u32) ino);
	*p++ = htonl((u32) namlen);	/* name length & name */
	memcpy(p, name, namlen);
	p += slen;

	/* throw in readdirplus baggage */
	if (cd->plus) {
		p = encode_post_op_attr(cd->rqstp, p, fh.fh_inode);
		p = encode_fh(p, &fh);
		fh_put(&fh);
	}

	cd->offset = p;			/* remember pointer */
	p = enc64(p, ~(u64) 0);	/* offset of next entry */

	cd->buflen = buflen;
	cd->buffer = p;
	return 0;
}

/* FSSTAT */
int
nfs3svc_encode_statfsres(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_statfsres *resp)
{
	struct statfs	*s = &resp->stats;
	u64		bs = s->f_bsize;

	*p++ = xdr_zero;	/* no post_op_attr */

	if (resp->status == 0) {
		p = enc64(p, bs * s->f_blocks);	/* total bytes */
		p = enc64(p, bs * s->f_bfree);	/* free bytes */
		p = enc64(p, bs * s->f_bavail);	/* user available bytes */
		p = enc64(p, s->f_files);	/* total inodes */
		p = enc64(p, s->f_ffree);	/* free inodes */
		p = enc64(p, s->f_ffree);	/* user available inodes */
		*p++ = htonl(resp->invarsec);	/* mean unchanged time */
	}
	return xdr_ressize_check(rqstp, p);
}

/* FSINFO */
int
nfs3svc_encode_fsinfores(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_fsinfores *resp)
{
	*p++ = xdr_zero;	/* no post_op_attr */

	if (resp->status == 0) {
		*p++ = htonl(resp->f_rtmax);
		*p++ = htonl(resp->f_rtpref);
		*p++ = htonl(resp->f_rtmult);
		*p++ = htonl(resp->f_wtmax);
		*p++ = htonl(resp->f_wtpref);
		*p++ = htonl(resp->f_wtmult);
		*p++ = htonl(resp->f_dtpref);
		*p++ = htonl(resp->f_maxfilesize);
		*p++ = xdr_zero;
		*p++ = htonl(1000000000 / HZ);
		*p++ = htonl(resp->f_properties);
	}

	return xdr_ressize_check(rqstp, p);
}

/* PATHCONF */
int
nfs3svc_encode_pathconfres(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_pathconfres *resp)
{
	*p++ = xdr_zero;	/* no post_op_attr */

	if (resp->status == 0) {
		*p++ = htonl(resp->p_link_max);
		*p++ = htonl(resp->p_name_max);
		*p++ = xdr_one;	/* always reject long file names */
		*p++ = xdr_one;	/* chown restricted */
		*p++ = htonl(resp->p_case_insensitive);
		*p++ = htonl(resp->p_case_preserving);
	}

	return xdr_ressize_check(rqstp, p);
}

/* COMMIT */
int
nfs3svc_encode_commitres(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_commitres *resp)
{
	p = encode_wcc_data(rqstp, p, &resp->fh);
	/* Write verifier */
	if (resp->status == 0) {
		*p++ = htonl(nfssvc_boot.tv_sec);
		*p++ = htonl(nfssvc_boot.tv_usec);
	}
	return xdr_ressize_check(rqstp, p);
}

/*
 * XDR release functions
 */
int
nfs3svc_release_fhandle(struct svc_rqst *rqstp, u32 *p,
					struct nfsd_fhandle *resp)
{
	fh_put(&resp->fh);
	return 1;
}

int
nfs3svc_release_fhandle2(struct svc_rqst *rqstp, u32 *p,
					struct nfsd3_fhandle2 *resp)
{
	fh_put(&resp->fh1);
	fh_put(&resp->fh2);
	return 1;
}

/*
 *  fs/nfs/nfs4xdr.c
 *
 *  Client-side XDR for NFSv4.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *  Andy Adamson   <andros@umich.edu>
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/param.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/utsname.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/kdev_t.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_idmap.h>

#define NFSDBG_FACILITY		NFSDBG_XDR

/* Mapping from NFS error code to "errno" error code. */
#define errno_NFSERR_IO		EIO

extern int			nfs_stat_to_errno(int);

/* NFSv4 COMPOUND tags are only wanted for debugging purposes */
#ifdef DEBUG
#define NFS4_MAXTAGLEN		20
#else
#define NFS4_MAXTAGLEN		0
#endif

#define compound_encode_hdr_maxsz	3 + (NFS4_MAXTAGLEN >> 2)
#define compound_decode_hdr_maxsz	2 + (NFS4_MAXTAGLEN >> 2)
#define op_encode_hdr_maxsz	1
#define op_decode_hdr_maxsz	2
#define encode_putfh_maxsz	op_encode_hdr_maxsz + 1 + \
				(NFS4_FHSIZE >> 2)
#define decode_putfh_maxsz	op_decode_hdr_maxsz
#define encode_getfh_maxsz      op_encode_hdr_maxsz
#define decode_getfh_maxsz      op_decode_hdr_maxsz + 1 + \
                                (NFS4_FHSIZE >> 2)
#define encode_getattr_maxsz    op_encode_hdr_maxsz + 3
#define nfs4_fattr_bitmap_maxsz 26 + 2 * ((NFS4_MAXNAMLEN +1) >> 2)
#define decode_getattr_maxsz    op_decode_hdr_maxsz + 3 + \
                                nfs4_fattr_bitmap_maxsz
#define encode_savefh_maxsz     op_encode_hdr_maxsz
#define decode_savefh_maxsz     op_decode_hdr_maxsz
#define encode_restorefh_maxsz  op_encode_hdr_maxsz
#define decode_restorefh_maxsz  op_decode_hdr_maxsz
#define encode_read_getattr_maxsz	op_encode_hdr_maxsz + 2
#define decode_read_getattr_maxsz	op_decode_hdr_maxsz + 8
#define encode_pre_write_getattr_maxsz	op_encode_hdr_maxsz + 2
#define decode_pre_write_getattr_maxsz	op_decode_hdr_maxsz + 5
#define encode_post_write_getattr_maxsz	op_encode_hdr_maxsz + 2
#define decode_post_write_getattr_maxsz	op_decode_hdr_maxsz + 13

#define NFS4_enc_compound_sz	1024  /* XXX: large enough? */
#define NFS4_dec_compound_sz	1024  /* XXX: large enough? */
#define NFS4_enc_read_sz	compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_read_getattr_maxsz + \
				op_encode_hdr_maxsz + 7
#define NFS4_dec_read_sz	compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_read_getattr_maxsz + \
				op_decode_hdr_maxsz + 2
#define NFS4_enc_write_sz	compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_pre_write_getattr_maxsz + \
				op_encode_hdr_maxsz + 8 + \
				encode_post_write_getattr_maxsz
#define NFS4_dec_write_sz	compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_pre_write_getattr_maxsz + \
				op_decode_hdr_maxsz + 4 + \
				decode_post_write_getattr_maxsz
#define NFS4_enc_commit_sz	compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_pre_write_getattr_maxsz + \
				op_encode_hdr_maxsz + 3 + \
				encode_post_write_getattr_maxsz
#define NFS4_dec_commit_sz	compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_pre_write_getattr_maxsz + \
				op_decode_hdr_maxsz + 2 + \
				decode_post_write_getattr_maxsz
#define NFS4_enc_open_sz        compound_encode_hdr_maxsz + \
                                encode_putfh_maxsz + \
                                encode_savefh_maxsz + \
                                op_encode_hdr_maxsz + \
                                13 + 3 + 2 + 64 + \
                                encode_getattr_maxsz + \
                                encode_getfh_maxsz + \
                                encode_restorefh_maxsz + \
                                encode_getattr_maxsz
#define NFS4_dec_open_sz        compound_decode_hdr_maxsz + \
                                decode_putfh_maxsz + \
                                decode_savefh_maxsz + \
                                op_decode_hdr_maxsz + 4 + 5 + 2 + 3 + \
                                decode_getattr_maxsz + \
                                decode_getfh_maxsz + \
                                decode_restorefh_maxsz + \
                                decode_getattr_maxsz
#define NFS4_enc_open_confirm_sz      \
                                compound_encode_hdr_maxsz + \
                                encode_putfh_maxsz + \
                                op_encode_hdr_maxsz + 5
#define NFS4_dec_open_confirm_sz        compound_decode_hdr_maxsz + \
                                        decode_putfh_maxsz + \
                                        op_decode_hdr_maxsz + 4
#define NFS4_enc_close_sz       compound_encode_hdr_maxsz + \
                                encode_putfh_maxsz + \
                                op_encode_hdr_maxsz + 5
#define NFS4_dec_close_sz       compound_decode_hdr_maxsz + \
                                decode_putfh_maxsz + \
                                op_decode_hdr_maxsz + 4
#define NFS4_enc_setattr_sz     compound_encode_hdr_maxsz + \
                                encode_putfh_maxsz + \
                                op_encode_hdr_maxsz + 4 + \
                                nfs4_fattr_bitmap_maxsz + \
                                encode_getattr_maxsz
#define NFS4_dec_setattr_sz     compound_decode_hdr_maxsz + \
                                decode_putfh_maxsz + \
                                op_decode_hdr_maxsz + 3


static struct {
	unsigned int	mode;
	unsigned int	nfs2type;
} nfs_type2fmt[] = {
	{ 0,		NFNON	     },
	{ S_IFREG,	NFREG	     },
	{ S_IFDIR,	NFDIR	     },
	{ S_IFBLK,	NFBLK	     },
	{ S_IFCHR,	NFCHR	     },
	{ S_IFLNK,	NFLNK	     },
	{ S_IFSOCK,	NFSOCK	     },
	{ S_IFIFO,	NFFIFO	     },
	{ 0,		NFNON	     },
	{ 0,		NFNON	     },
};

struct compound_hdr {
	int32_t		status;
	uint32_t	nops;
	uint32_t	taglen;
	char *		tag;
};

/*
 * START OF "GENERIC" ENCODE ROUTINES.
 *   These may look a little ugly since they are imported from a "generic"
 * set of XDR encode/decode routines which are intended to be shared by
 * all of our NFSv4 implementations (OpenBSD, MacOS X...).
 *
 * If the pain of reading these is too great, it should be a straightforward
 * task to translate them into Linux-specific versions which are more
 * consistent with the style used in NFSv2/v3...
 */
#define WRITE32(n)               *p++ = htonl(n)
#define WRITE64(n)               do {				\
	*p++ = htonl((uint32_t)((n) >> 32));				\
	*p++ = htonl((uint32_t)(n));					\
} while (0)
#define WRITEMEM(ptr,nbytes)     do {				\
	p = xdr_writemem(p, ptr, nbytes);			\
} while (0)

#define RESERVE_SPACE(nbytes)	do {				\
	p = xdr_reserve_space(xdr, nbytes);			\
	if (!p) printk("RESERVE_SPACE(%d) failed in function %s\n", (int) (nbytes), __FUNCTION__); \
	BUG_ON(!p);						\
} while (0)

static inline
uint32_t *xdr_writemem(uint32_t *p, const void *ptr, int nbytes)
{
	int tmp = XDR_QUADLEN(nbytes);
	if (!tmp)
		return p;
	p[tmp-1] = 0;
	memcpy(p, ptr, nbytes);
	return p + tmp;
}

static int
encode_compound_hdr(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	uint32_t *p;

	dprintk("encode_compound: tag=%.*s\n", (int)hdr->taglen, hdr->tag);
	BUG_ON(hdr->taglen > NFS4_MAXTAGLEN);
	RESERVE_SPACE(12+XDR_QUADLEN(hdr->taglen));
	WRITE32(hdr->taglen);
	WRITEMEM(hdr->tag, hdr->taglen);
	WRITE32(NFS4_MINOR_VERSION);
	WRITE32(hdr->nops);
	return 0;
}

static int
encode_attrs(struct xdr_stream *xdr, struct iattr *iap,
    struct nfs_server *server)
{
	char owner_name[256];
	char owner_group[256];
	int owner_namelen = 0;
	int owner_grouplen = 0;
	uint32_t *p;
	uint32_t *q;
	int len;
	uint32_t bmval0 = 0;
	uint32_t bmval1 = 0;
	int status;

	/*
	 * We reserve enough space to write the entire attribute buffer at once.
	 * In the worst-case, this would be
	 *   12(bitmap) + 4(attrlen) + 8(size) + 4(mode) + 4(atime) + 4(mtime)
	 *          = 36 bytes, plus any contribution from variable-length fields
	 *            such as owner/group/acl's.
	 */
	len = 16;

	/* Sigh */
	if (iap->ia_valid & ATTR_SIZE)
		len += 8;
	if (iap->ia_valid & ATTR_MODE)
		len += 4;
	if (iap->ia_valid & ATTR_UID) {
		status = nfs_idmap_name(server, IDMAP_TYPE_USER,
		    iap->ia_uid, owner_name, &owner_namelen);
		if (status < 0) {
			printk(KERN_WARNING "nfs: couldn't resolve uid %d to string\n",
			       iap->ia_uid);
			/* XXX */
			strcpy(owner_name, "nobody");
			owner_namelen = sizeof("nobody") - 1;
			/* goto out; */
		}
		len += 4 + (XDR_QUADLEN(owner_namelen) << 2);
	}
	if (iap->ia_valid & ATTR_GID) {
		status = nfs_idmap_name(server, IDMAP_TYPE_GROUP,
		    iap->ia_gid, owner_group, &owner_grouplen);
		if (status < 0) {
			printk(KERN_WARNING "nfs4: couldn't resolve gid %d to string\n",
			       iap->ia_gid);
			strcpy(owner_group, "nobody");
			owner_grouplen = sizeof("nobody") - 1;
			/* goto out; */
		}
		len += 4 + (XDR_QUADLEN(owner_grouplen) << 2);
	}
	if (iap->ia_valid & ATTR_ATIME_SET)
		len += 16;
	else if (iap->ia_valid & ATTR_ATIME)
		len += 4;
	if (iap->ia_valid & ATTR_MTIME_SET)
		len += 16;
	else if (iap->ia_valid & ATTR_MTIME)
		len += 4;
	RESERVE_SPACE(len);

	/*
	 * We write the bitmap length now, but leave the bitmap and the attribute
	 * buffer length to be backfilled at the end of this routine.
	 */
	WRITE32(2);
	q = p;
	p += 3;

	if (iap->ia_valid & ATTR_SIZE) {
		bmval0 |= FATTR4_WORD0_SIZE;
		WRITE64(iap->ia_size);
	}
	if (iap->ia_valid & ATTR_MODE) {
		bmval1 |= FATTR4_WORD1_MODE;
		WRITE32(iap->ia_mode);
	}
	if (iap->ia_valid & ATTR_UID) {
		bmval1 |= FATTR4_WORD1_OWNER;
		WRITE32(owner_namelen);
		WRITEMEM(owner_name, owner_namelen);
	}
	if (iap->ia_valid & ATTR_GID) {
		bmval1 |= FATTR4_WORD1_OWNER_GROUP;
		WRITE32(owner_grouplen);
		WRITEMEM(owner_group, owner_grouplen);
	}
	if (iap->ia_valid & ATTR_ATIME_SET) {
		bmval1 |= FATTR4_WORD1_TIME_ACCESS_SET;
		WRITE32(NFS4_SET_TO_CLIENT_TIME);
		WRITE32(0);
		WRITE32(iap->ia_mtime.tv_sec);
		WRITE32(iap->ia_mtime.tv_nsec);
	}
	else if (iap->ia_valid & ATTR_ATIME) {
		bmval1 |= FATTR4_WORD1_TIME_ACCESS_SET;
		WRITE32(NFS4_SET_TO_SERVER_TIME);
	}
	if (iap->ia_valid & ATTR_MTIME_SET) {
		bmval1 |= FATTR4_WORD1_TIME_MODIFY_SET;
		WRITE32(NFS4_SET_TO_CLIENT_TIME);
		WRITE32(0);
		WRITE32(iap->ia_mtime.tv_sec);
		WRITE32(iap->ia_mtime.tv_nsec);
	}
	else if (iap->ia_valid & ATTR_MTIME) {
		bmval1 |= FATTR4_WORD1_TIME_MODIFY_SET;
		WRITE32(NFS4_SET_TO_SERVER_TIME);
	}
	
	/*
	 * Now we backfill the bitmap and the attribute buffer length.
	 */
	if (len != ((char *)p - (char *)q) + 4) {
		printk ("encode_attr: Attr length calculation error! %u != %Zu\n",
				len, ((char *)p - (char *)q) + 4);
		BUG();
	}
	len = (char *)p - (char *)q - 12;
	*q++ = htonl(bmval0);
	*q++ = htonl(bmval1);
	*q++ = htonl(len);

	status = 0;
/* out: */
	return status;
}

static int
encode_access(struct xdr_stream *xdr, struct nfs4_access *access)
{
	uint32_t *p;

	RESERVE_SPACE(8);
	WRITE32(OP_ACCESS);
	WRITE32(access->ac_req_access);
	
	return 0;
}

static int
encode_close(struct xdr_stream *xdr, struct nfs_closeargs *arg)
{
	uint32_t *p;

	RESERVE_SPACE(8+sizeof(arg->stateid.data));
	WRITE32(OP_CLOSE);
	WRITE32(arg->seqid);
	WRITEMEM(arg->stateid.data, sizeof(arg->stateid.data));
	
	return 0;
}

static int
encode_commit(struct xdr_stream *xdr, struct nfs_writeargs *args)
{
	uint32_t *p;
        
        RESERVE_SPACE(16);
        WRITE32(OP_COMMIT);
        WRITE64(args->offset);
        WRITE32(args->count);

        return 0;
}

static int
encode_create(struct xdr_stream *xdr, struct nfs4_create *create,
    struct nfs_server *server)
{
	uint32_t *p;
	
	RESERVE_SPACE(8);
	WRITE32(OP_CREATE);
	WRITE32(create->cr_ftype);

	switch (create->cr_ftype) {
	case NF4LNK:
		RESERVE_SPACE(4 + create->cr_textlen);
		WRITE32(create->cr_textlen);
		WRITEMEM(create->cr_text, create->cr_textlen);
		break;

	case NF4BLK: case NF4CHR:
		RESERVE_SPACE(8);
		WRITE32(create->cr_specdata1);
		WRITE32(create->cr_specdata2);
		break;

	default:
		break;
	}

	RESERVE_SPACE(4 + create->cr_namelen);
	WRITE32(create->cr_namelen);
	WRITEMEM(create->cr_name, create->cr_namelen);

	return encode_attrs(xdr, create->cr_attrs, server);
}

static int
encode_getattr_one(struct xdr_stream *xdr, uint32_t bitmap)
{
        uint32_t *p;

        RESERVE_SPACE(12);
        WRITE32(OP_GETATTR);
        WRITE32(1);
        WRITE32(bitmap);
        return 0;
}

static int
encode_getattr_two(struct xdr_stream *xdr, uint32_t bm0, uint32_t bm1)
{
        uint32_t *p;

        RESERVE_SPACE(16);
        WRITE32(OP_GETATTR);
        WRITE32(2);
        WRITE32(bm0);
        WRITE32(bm1);
        return 0;
}

static inline int
encode_getattr(struct xdr_stream *xdr, struct nfs4_getattr *getattr)
{
	return encode_getattr_two(xdr, getattr->gt_bmval[0],
					getattr->gt_bmval[1]);
}

/*
 * Request the change attribute in order to check attribute+cache consistency
 */
static inline int
encode_read_getattr(struct xdr_stream *xdr)
{
	return encode_getattr_two(xdr, FATTR4_WORD0_CHANGE,
			FATTR4_WORD1_TIME_ACCESS);
}

/*
 * Request the change attribute prior to doing a write operation
 */
static inline int
encode_pre_write_getattr(struct xdr_stream *xdr)
{
	/* Request the change attribute */
	return encode_getattr_one(xdr, FATTR4_WORD0_CHANGE);
}

/*
 * Request the change attribute, size, and [cm]time after a write operation
 */
static inline int
encode_post_write_getattr(struct xdr_stream *xdr)
{
	return encode_getattr_two(xdr, FATTR4_WORD0_CHANGE|FATTR4_WORD0_SIZE,
			FATTR4_WORD1_SPACE_USED |
			FATTR4_WORD1_TIME_METADATA |
			FATTR4_WORD1_TIME_MODIFY);
}

static int
encode_getfh(struct xdr_stream *xdr)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(OP_GETFH);

	return 0;
}

static int
encode_link(struct xdr_stream *xdr, struct nfs4_link *link)
{
	uint32_t *p;

	RESERVE_SPACE(8 + link->ln_namelen);
	WRITE32(OP_LINK);
	WRITE32(link->ln_namelen);
	WRITEMEM(link->ln_name, link->ln_namelen);
	
	return 0;
}

static int
encode_lookup(struct xdr_stream *xdr, struct nfs4_lookup *lookup)
{
	int len = lookup->lo_name->len;
	uint32_t *p;

	RESERVE_SPACE(8 + len);
	WRITE32(OP_LOOKUP);
	WRITE32(len);
	WRITEMEM(lookup->lo_name->name, len);

	return 0;
}

static int
encode_open(struct xdr_stream *xdr, struct nfs_openargs *arg)
{
	int status;
	uint32_t *p;

 /*
 * opcode 4, seqid 4, share_access 4, share_deny 4, clientid 8, ownerlen 4,
 * owner 4, opentype 4 = 36
 */
	RESERVE_SPACE(36);
	WRITE32(OP_OPEN);
	WRITE32(arg->seqid);
	switch (arg->share_access) {
		case FMODE_READ:
			WRITE32(NFS4_SHARE_ACCESS_READ);
			break;
		case FMODE_WRITE:
			WRITE32(NFS4_SHARE_ACCESS_WRITE);
			break;
		case FMODE_READ|FMODE_WRITE:
			WRITE32(NFS4_SHARE_ACCESS_BOTH);
			break;
		default:
			BUG();
	}
	WRITE32(0);                  /* for linux, share_deny = 0 always */
	WRITE64(arg->clientid);
	WRITE32(4);
	WRITE32(arg->id);
	WRITE32(arg->opentype);

	if (arg->opentype == NFS4_OPEN_CREATE) {
		if (arg->createmode == NFS4_CREATE_EXCLUSIVE) {
			RESERVE_SPACE(12);
			WRITE32(arg->createmode);
			WRITEMEM(arg->u.verifier.data, sizeof(arg->u.verifier.data));
		}
		else if (arg->u.attrs) {
			RESERVE_SPACE(4);
			WRITE32(arg->createmode);
			if ((status = encode_attrs(xdr, arg->u.attrs, arg->server)))
				return status;
		}
		else {
			RESERVE_SPACE(12);
			WRITE32(arg->createmode);
			WRITE32(0);
			WRITE32(0);
		}
	}

	RESERVE_SPACE(8 + arg->name->len);
	WRITE32(NFS4_OPEN_CLAIM_NULL);
	WRITE32(arg->name->len);
	WRITEMEM(arg->name->name, arg->name->len);

	return 0;
}

static int
encode_open_confirm(struct xdr_stream *xdr, struct nfs_open_confirmargs *arg)
{
	uint32_t *p;

	RESERVE_SPACE(8+sizeof(arg->stateid.data));
	WRITE32(OP_OPEN_CONFIRM);
	WRITEMEM(arg->stateid.data, sizeof(arg->stateid.data));
	WRITE32(arg->seqid);

	return 0;
}


static int
encode_putfh(struct xdr_stream *xdr, struct nfs_fh *fh)
{
	int len = fh->size;
	uint32_t *p;

	RESERVE_SPACE(8 + len);
	WRITE32(OP_PUTFH);
	WRITE32(len);
	WRITEMEM(fh->data, len);

	return 0;
}

static int
encode_putrootfh(struct xdr_stream *xdr)
{
        uint32_t *p;
        
        RESERVE_SPACE(4);
        WRITE32(OP_PUTROOTFH);

        return 0;
}

static int
encode_read(struct xdr_stream *xdr, struct nfs_readargs *args)
{
	uint32_t *p;

	RESERVE_SPACE(32);
	WRITE32(OP_READ);
	WRITEMEM(args->stateid.data, sizeof(args->stateid.data));
	WRITE64(args->offset);
	WRITE32(args->count);

	return 0;
}

static int
encode_readdir(struct xdr_stream *xdr, struct nfs4_readdir *readdir, struct rpc_rqst *req)
{
	struct rpc_auth *auth = req->rq_task->tk_auth;
	int replen;
	uint32_t *p;

	RESERVE_SPACE(32+sizeof(nfs4_verifier));
	WRITE32(OP_READDIR);
	WRITE64(readdir->rd_cookie);
	WRITEMEM(readdir->rd_req_verifier.data, sizeof(readdir->rd_req_verifier.data));
	WRITE32(readdir->rd_count >> 5);  /* meaningless "dircount" field */
	WRITE32(readdir->rd_count);
	WRITE32(2);
	WRITE32(readdir->rd_bmval[0]);
	WRITE32(readdir->rd_bmval[1]);

	/* set up reply iovec
	 *    toplevel_status + taglen + rescount + OP_PUTFH + status
	 *      + OP_READDIR + status + verifer(2)  = 9
	 */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + 9) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen, readdir->rd_pages,
			 readdir->rd_pgbase, readdir->rd_count);

	return 0;
}

static int
encode_readlink(struct xdr_stream *xdr, struct nfs4_readlink *readlink, struct rpc_rqst *req)
{
	struct rpc_auth *auth = req->rq_task->tk_auth;
	int replen;
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(OP_READLINK);

	/* set up reply iovec
	 *    toplevel_status + taglen + rescount + OP_PUTFH + status
	 *      + OP_READLINK + status  = 7
	 */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + 7) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen, readlink->rl_pages, 0, readlink->rl_count);
	
	return 0;
}

static int
encode_remove(struct xdr_stream *xdr, struct nfs4_remove *remove)
{
	uint32_t *p;

	RESERVE_SPACE(8 + remove->rm_namelen);
	WRITE32(OP_REMOVE);
	WRITE32(remove->rm_namelen);
	WRITEMEM(remove->rm_name, remove->rm_namelen);

	return 0;
}

static int
encode_rename(struct xdr_stream *xdr, struct nfs4_rename *rename)
{
	uint32_t *p;

	RESERVE_SPACE(8 + rename->rn_oldnamelen);
	WRITE32(OP_RENAME);
	WRITE32(rename->rn_oldnamelen);
	WRITEMEM(rename->rn_oldname, rename->rn_oldnamelen);
	
	RESERVE_SPACE(4 + rename->rn_newnamelen);
	WRITE32(rename->rn_newnamelen);
	WRITEMEM(rename->rn_newname, rename->rn_newnamelen);

	return 0;
}

static int
encode_renew(struct xdr_stream *xdr, struct nfs4_client *client_stateid)
{
	uint32_t *p;

	RESERVE_SPACE(12);
	WRITE32(OP_RENEW);
	WRITE64(client_stateid->cl_clientid);

	return 0;
}

static int
encode_restorefh(struct xdr_stream *xdr)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(OP_RESTOREFH);

	return 0;
}

static int
encode_savefh(struct xdr_stream *xdr)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(OP_SAVEFH);

	return 0;
}

static int
encode_setattr(struct xdr_stream *xdr, struct nfs_setattrargs *arg,
    struct nfs_server *server)
{
	int status;
	uint32_t *p;
	
        RESERVE_SPACE(4+sizeof(arg->stateid.data));
        WRITE32(OP_SETATTR);
	WRITEMEM(arg->stateid.data, sizeof(arg->stateid.data));

        if ((status = encode_attrs(xdr, arg->iap, server)))
		return status;

        return 0;
}

static int
encode_setclientid(struct xdr_stream *xdr, struct nfs4_setclientid *setclientid)
{
	uint32_t total_len;
	uint32_t len1, len2, len3;
	uint32_t *p;

	len1 = strlen(setclientid->sc_name);
	len2 = strlen(setclientid->sc_netid);
	len3 = strlen(setclientid->sc_uaddr);
	total_len = XDR_QUADLEN(len1) + XDR_QUADLEN(len2) + XDR_QUADLEN(len3);
	total_len = (total_len << 2) + 24 + sizeof(setclientid->sc_verifier.data);

	RESERVE_SPACE(total_len);
	WRITE32(OP_SETCLIENTID);
	WRITEMEM(setclientid->sc_verifier.data, sizeof(setclientid->sc_verifier.data));
	WRITE32(len1);
	WRITEMEM(setclientid->sc_name, len1);
	WRITE32(setclientid->sc_prog);
	WRITE32(len2);
	WRITEMEM(setclientid->sc_netid, len2);
	WRITE32(len3);
	WRITEMEM(setclientid->sc_uaddr, len3);
	WRITE32(setclientid->sc_cb_ident);

	return 0;
}

static int
encode_setclientid_confirm(struct xdr_stream *xdr, struct nfs4_client *client_state)
{
        uint32_t *p;

        RESERVE_SPACE(12 + sizeof(client_state->cl_confirm.data));
        WRITE32(OP_SETCLIENTID_CONFIRM);
        WRITE64(client_state->cl_clientid);
        WRITEMEM(client_state->cl_confirm.data, sizeof(client_state->cl_confirm.data));

        return 0;
}

static int
encode_write(struct xdr_stream *xdr, struct nfs_writeargs *args)
{
	uint32_t *p;

	RESERVE_SPACE(36);
	WRITE32(OP_WRITE);
	WRITEMEM(args->stateid.data, sizeof(args->stateid.data));
	WRITE64(args->offset);
	WRITE32(args->stable);
	WRITE32(args->count);

	xdr_write_pages(xdr, args->pages, args->pgbase, args->count);

	return 0;
}

/* FIXME: this sucks */
static int
encode_compound(struct xdr_stream *xdr, struct nfs4_compound *cp, struct rpc_rqst *req)
{
	struct compound_hdr hdr = {
		.taglen = cp->taglen,
		.tag	= cp->tag,
		.nops	= cp->req_nops,
	};
	int i, status = 0;

	encode_compound_hdr(xdr, &hdr);

	for (i = 0; i < cp->req_nops; i++) {
		switch (cp->ops[i].opnum) {
		case OP_ACCESS:
			status = encode_access(xdr, &cp->ops[i].u.access);
			break;
		case OP_CREATE:
			status = encode_create(xdr, &cp->ops[i].u.create, cp->server);
			break;
		case OP_GETATTR:
			status = encode_getattr(xdr, &cp->ops[i].u.getattr);
			break;
		case OP_GETFH:
			status = encode_getfh(xdr);
			break;
		case OP_LINK:
			status = encode_link(xdr, &cp->ops[i].u.link);
			break;
		case OP_LOOKUP:
			status = encode_lookup(xdr, &cp->ops[i].u.lookup);
			break;
		case OP_PUTFH:
			status = encode_putfh(xdr, cp->ops[i].u.putfh.pf_fhandle);
			break;
		case OP_PUTROOTFH:
			status = encode_putrootfh(xdr);
			break;
		case OP_READDIR:
			status = encode_readdir(xdr, &cp->ops[i].u.readdir, req);
			break;
		case OP_READLINK:
			status = encode_readlink(xdr, &cp->ops[i].u.readlink, req);
			break;
		case OP_REMOVE:
			status = encode_remove(xdr, &cp->ops[i].u.remove);
			break;
		case OP_RENAME:
			status = encode_rename(xdr, &cp->ops[i].u.rename);
			break;
		case OP_RENEW:
			status = encode_renew(xdr, cp->ops[i].u.renew);
			break;
		case OP_RESTOREFH:
			status = encode_restorefh(xdr);
			break;
		case OP_SAVEFH:
			status = encode_savefh(xdr);
			break;
		case OP_SETCLIENTID:
			status = encode_setclientid(xdr, &cp->ops[i].u.setclientid);
			break;
		case OP_SETCLIENTID_CONFIRM:
			status = encode_setclientid_confirm(xdr, cp->ops[i].u.setclientid_confirm);
			break;
		default:
			BUG();
		}
		if (status)
			return status;
	}
	
	return 0;
}
/*
 * END OF "GENERIC" ENCODE ROUTINES.
 */


/*
 * Encode COMPOUND argument
 */
static int
nfs4_xdr_enc_compound(struct rpc_rqst *req, uint32_t *p, struct nfs4_compound *cp)
{
	struct xdr_stream xdr;
	int status;
	
	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	status = encode_compound(&xdr, cp, req);
	cp->timestamp = jiffies;
	return status;
}
/*
 * Encode a CLOSE request
 */
static int
nfs4_xdr_enc_close(struct rpc_rqst *req, uint32_t *p, struct nfs_closeargs *args)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr = {
                .nops   = 2,
        };
        int status;

        xdr_init_encode(&xdr, &req->rq_snd_buf, p);
        encode_compound_hdr(&xdr, &hdr);
        status = encode_putfh(&xdr, args->fh);
        if(status)
                goto out;
        status = encode_close(&xdr, args);
out:
        return status;
}

/*
 * Encode an OPEN request
 */
static int
nfs4_xdr_enc_open(struct rpc_rqst *req, uint32_t *p, struct nfs_openargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 7,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if (status)
		goto out;
	status = encode_savefh(&xdr);
	if (status)
		goto out;
	status = encode_open(&xdr, args);
	if (status)
		goto out;
	status = encode_getattr(&xdr, args->f_getattr);
	if (status)
		goto out;
	status = encode_getfh(&xdr);
	if (status)
		goto out;
	status = encode_restorefh(&xdr);
	if (status)
		goto out;
	status = encode_getattr(&xdr, args->d_getattr);
out:
	return status;
}

/*
 * Encode an OPEN_CONFIRM request
 */
static int
nfs4_xdr_enc_open_confirm(struct rpc_rqst *req, uint32_t *p, struct nfs_open_confirmargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 2,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if(status)
		goto out;
	status = encode_open_confirm(&xdr, args);
out:
	return status;
}


/*
 * Encode a READ request
 */
static int
nfs4_xdr_enc_read(struct rpc_rqst *req, uint32_t *p, struct nfs_readargs *args)
{
	struct rpc_auth	*auth = req->rq_task->tk_auth;
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 3,
	};
	int replen, status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if (status)
		goto out;
	status = encode_read(&xdr, args);
	if (status)
		goto out;
	status = encode_read_getattr(&xdr);

	/* set up reply iovec
	 *    toplevel status + taglen=0 + rescount + OP_PUTFH + status
	 *       + OP_READ + status + eof + datalen = 9
	 */
	replen = (RPC_REPHDRSIZE + auth->au_rslack +
			NFS4_dec_read_sz - decode_read_getattr_maxsz) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen,
			 args->pages, args->pgbase, args->count);
out:
	return status;
}

/*
 * Encode an SETATTR request
 */
static int
nfs4_xdr_enc_setattr(struct rpc_rqst *req, uint32_t *p, struct nfs_setattrargs *args)

{
        struct xdr_stream xdr;
        struct compound_hdr hdr = {
                .nops   = 3,
        };
        int status;

        xdr_init_encode(&xdr, &req->rq_snd_buf, p);
        encode_compound_hdr(&xdr, &hdr);
        status = encode_putfh(&xdr, args->fh);
        if(status)
                goto out;
        status = encode_setattr(&xdr, args, args->server);
        if(status)
                goto out;
        status = encode_getattr(&xdr, args->attr);
out:
        return status;
}

/*
 * Encode a WRITE request
 */
static int
nfs4_xdr_enc_write(struct rpc_rqst *req, uint32_t *p, struct nfs_writeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 4,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if (status)
		goto out;
	status = encode_pre_write_getattr(&xdr);
	if (status)
		goto out;
	status = encode_write(&xdr, args);
	if (status)
		goto out;
	status = encode_post_write_getattr(&xdr);
out:
	return status;
}

/*
 *  a COMMIT request
 */
static int
nfs4_xdr_enc_commit(struct rpc_rqst *req, uint32_t *p, struct nfs_writeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 4,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if (status)
		goto out;
	status = encode_pre_write_getattr(&xdr);
	if (status)
		goto out;
	status = encode_commit(&xdr, args);
	if (status)
		goto out;
	status = encode_post_write_getattr(&xdr);
out:
	return status;
}

/*
 * START OF "GENERIC" DECODE ROUTINES.
 *   These may look a little ugly since they are imported from a "generic"
 * set of XDR encode/decode routines which are intended to be shared by
 * all of our NFSv4 implementations (OpenBSD, MacOS X...).
 *
 * If the pain of reading these is too great, it should be a straightforward
 * task to translate them into Linux-specific versions which are more
 * consistent with the style used in NFSv2/v3...
 */
#define DECODE_TAIL				\
	status = 0;				\
out:						\
	return status;				\
xdr_error:					\
	printk(KERN_NOTICE "xdr error! (%s:%d)\n", __FILE__, __LINE__); \
	status = -EIO;				\
	goto out

#define READ32(x)         (x) = ntohl(*p++)
#define READ64(x)         do {			\
	(x) = (u64)ntohl(*p++) << 32;		\
	(x) |= ntohl(*p++);			\
} while (0)
#define READTIME(x)       do {			\
	p++;					\
	(x.tv_sec) = ntohl(*p++);		\
	(x.tv_nsec) = ntohl(*p++);		\
} while (0)
#define COPYMEM(x,nbytes) do {			\
	memcpy((x), p, nbytes);			\
	p += XDR_QUADLEN(nbytes);		\
} while (0)

#define READ_BUF(nbytes)  do { \
	p = xdr_inline_decode(xdr, nbytes); \
	if (!p) { \
		printk(KERN_WARNING "%s: reply buffer overflowed in line %d.", \
			       	__FUNCTION__, __LINE__); \
		return -EIO; \
	} \
} while (0)

static int
decode_compound_hdr(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	uint32_t *p;

	READ_BUF(8);
	READ32(hdr->status);
	READ32(hdr->taglen);
	
	READ_BUF(hdr->taglen + 4);
	hdr->tag = (char *)p;
	p += XDR_QUADLEN(hdr->taglen);
	READ32(hdr->nops);
	return 0;
}

static int
decode_op_hdr(struct xdr_stream *xdr, enum nfs_opnum4 expected)
{
	uint32_t *p;
	uint32_t opnum;
	int32_t nfserr;

	READ_BUF(8);
	READ32(opnum);
	if (opnum != expected) {
		printk(KERN_NOTICE
				"nfs4_decode_op_hdr: Server returned operation"
			       	" %d but we issued a request for %d\n",
				opnum, expected);
		return -EIO;
	}
	READ32(nfserr);
	if (nfserr != NFS_OK)
		return -nfs_stat_to_errno(nfserr);
	return 0;
}

static int
decode_change_info(struct xdr_stream *xdr, struct nfs4_change_info *cinfo)
{
	uint32_t *p;

	READ_BUF(20);
	READ32(cinfo->atomic);
	READ64(cinfo->before);
	READ64(cinfo->after);
	return 0;
}

static int
decode_access(struct xdr_stream *xdr, struct nfs4_access *access)
{
	uint32_t *p;
	uint32_t supp, acc;
	int status;

	status = decode_op_hdr(xdr, OP_ACCESS);
	if (status)
		return status;
	READ_BUF(8);
	READ32(supp);
	READ32(acc);
	if ((supp & ~access->ac_req_access) || (acc & ~supp)) {
		printk(KERN_NOTICE "NFS: server returned bad bits in access call!\n");
		return -EIO;
	}
	*access->ac_resp_supported = supp;
	*access->ac_resp_access = acc;
	return 0;
}

static int
decode_close(struct xdr_stream *xdr, struct nfs_closeres *res)
{
	uint32_t *p;
	int status;

	status = decode_op_hdr(xdr, OP_CLOSE);
	if (status)
		return status;
	READ_BUF(sizeof(res->stateid.data));
	COPYMEM(res->stateid.data, sizeof(res->stateid.data));
	return 0;
}

static int
decode_commit(struct xdr_stream *xdr, struct nfs_writeres *res)
{
	uint32_t *p;
	int status;

	status = decode_op_hdr(xdr, OP_COMMIT);
	if (status)
		return status;
	READ_BUF(8);
	COPYMEM(res->verf->verifier, 8);
	return 0;
}

static int
decode_create(struct xdr_stream *xdr, struct nfs4_create *create)
{
	uint32_t *p;
	uint32_t bmlen;
	int status;

	status = decode_op_hdr(xdr, OP_CREATE);
	if (status)
		return status;
	if ((status = decode_change_info(xdr, create->cr_cinfo)))
		return status;
	READ_BUF(4);
	READ32(bmlen);
	READ_BUF(bmlen << 2);
	return 0;
}

extern uint32_t nfs4_fattr_bitmap[2];
extern uint32_t nfs4_fsinfo_bitmap[2];
extern uint32_t nfs4_fsstat_bitmap[2];
extern uint32_t nfs4_pathconf_bitmap[2];

static int
decode_getattr(struct xdr_stream *xdr, struct nfs4_getattr *getattr,
    struct nfs_server *server)
{
	struct nfs_fattr *nfp = getattr->gt_attrs;
	struct nfs_fsstat *fsstat = getattr->gt_fsstat;
	struct nfs_fsinfo *fsinfo = getattr->gt_fsinfo;
	struct nfs_pathconf *pathconf = getattr->gt_pathconf;
	uint32_t attrlen, dummy32, bmlen,
		 bmval0 = 0,
		 bmval1 = 0,
		 len = 0;
	uint32_t *p;
	unsigned int type;
	int fmode = 0;
	int status;
	
	status = decode_op_hdr(xdr, OP_GETATTR);
	if (status)
		return status;
        
        READ_BUF(4);
        READ32(bmlen);
        if (bmlen > 2)
                goto xdr_error;
	
        READ_BUF((bmlen << 2) + 4);
        if (bmlen > 0)
                READ32(bmval0);
        if (bmlen > 1)
                READ32(bmval1);
        READ32(attrlen);

	if ((bmval0 & ~getattr->gt_bmval[0]) ||
	    (bmval1 & ~getattr->gt_bmval[1])) {
		dprintk("read_attrs: server returned bad attributes!\n");
		goto xdr_error;
	}
	if (nfp) {
		nfp->bitmap[0] = bmval0;
		nfp->bitmap[1] = bmval1;
	}

	/*
	 * In case the server doesn't return some attributes,
	 * we initialize them here to some nominal values..
	 */
	if (nfp) {
		nfp->valid = NFS_ATTR_FATTR | NFS_ATTR_FATTR_V3 | NFS_ATTR_FATTR_V4;
		nfp->nlink = 1;
		nfp->timestamp = jiffies;
	}
	if (fsinfo) {
		fsinfo->rtmult = fsinfo->wtmult = 512;  /* ??? */
		fsinfo->lease_time = 60;
	}

        if (bmval0 & FATTR4_WORD0_TYPE) {
                READ_BUF(4);
                len += 4;
                READ32(type);
                if (type < NF4REG || type > NF4NAMEDATTR) {
                        dprintk("read_attrs: bad type %d\n", type);
                        goto xdr_error;
                }
		nfp->type = nfs_type2fmt[type].nfs2type;
		fmode = nfs_type2fmt[type].mode;
                dprintk("read_attrs: type=%d\n", (uint32_t)nfp->type);
        }
        if (bmval0 & FATTR4_WORD0_CHANGE) {
                READ_BUF(8);
                len += 8;
                READ64(nfp->change_attr);
                dprintk("read_attrs: changeid=%Ld\n", (long long)nfp->change_attr);
        }
        if (bmval0 & FATTR4_WORD0_SIZE) {
                READ_BUF(8);
                len += 8;
                READ64(nfp->size);
                dprintk("read_attrs: size=%Ld\n", (long long)nfp->size);
        }
        if (bmval0 & FATTR4_WORD0_FSID) {
                READ_BUF(16);
                len += 16;
                READ64(nfp->fsid_u.nfs4.major);
                READ64(nfp->fsid_u.nfs4.minor);
                dprintk("read_attrs: fsid=0x%Lx/0x%Lx\n",
			(long long)nfp->fsid_u.nfs4.major,
			(long long)nfp->fsid_u.nfs4.minor);
        }
        if (bmval0 & FATTR4_WORD0_LEASE_TIME) {
                READ_BUF(4);
                len += 4;
                READ32(fsinfo->lease_time);
                dprintk("read_attrs: lease_time=%d\n", fsinfo->lease_time);
        }
        if (bmval0 & FATTR4_WORD0_FILEID) {
                READ_BUF(8);
                len += 8;
                READ64(nfp->fileid);
                dprintk("read_attrs: fileid=%Ld\n", (long long) nfp->fileid);
        }
	if (bmval0 & FATTR4_WORD0_FILES_AVAIL) {
		READ_BUF(8);
		len += 8;
		READ64(fsstat->afiles);
		dprintk("read_attrs: files_avail=0x%Lx\n", (long long) fsstat->afiles);
	}
        if (bmval0 & FATTR4_WORD0_FILES_FREE) {
                READ_BUF(8);
                len += 8;
                READ64(fsstat->ffiles);
                dprintk("read_attrs: files_free=0x%Lx\n", (long long) fsstat->ffiles);
        }
        if (bmval0 & FATTR4_WORD0_FILES_TOTAL) {
                READ_BUF(8);
                len += 8;
                READ64(fsstat->tfiles);
                dprintk("read_attrs: files_tot=0x%Lx\n", (long long) fsstat->tfiles);
        }
        if (bmval0 & FATTR4_WORD0_MAXFILESIZE) {
                READ_BUF(8);
                len += 8;
                READ64(fsinfo->maxfilesize);
                dprintk("read_attrs: maxfilesize=0x%Lx\n", (long long) fsinfo->maxfilesize);
        }
	if (bmval0 & FATTR4_WORD0_MAXLINK) {
		READ_BUF(4);
		len += 4;
		READ32(pathconf->max_link);
		dprintk("read_attrs: maxlink=%d\n", pathconf->max_link);
	}
        if (bmval0 & FATTR4_WORD0_MAXNAME) {
                READ_BUF(4);
                len += 4;
                READ32(pathconf->max_namelen);
                dprintk("read_attrs: maxname=%d\n", pathconf->max_namelen);
        }
        if (bmval0 & FATTR4_WORD0_MAXREAD) {
                READ_BUF(8);
                len += 8;
                READ64(fsinfo->rtmax);
		fsinfo->rtpref = fsinfo->dtpref = fsinfo->rtmax;
                dprintk("read_attrs: maxread=%d\n", fsinfo->rtmax);
        }
        if (bmval0 & FATTR4_WORD0_MAXWRITE) {
                READ_BUF(8);
                len += 8;
                READ64(fsinfo->wtmax);
		fsinfo->wtpref = fsinfo->wtmax;
                dprintk("read_attrs: maxwrite=%d\n", fsinfo->wtmax);
        }
	
        if (bmval1 & FATTR4_WORD1_MODE) {
                READ_BUF(4);
                len += 4;
                READ32(dummy32);
		nfp->mode = (dummy32 & ~S_IFMT) | fmode;
                dprintk("read_attrs: mode=0%o\n", nfp->mode);
        }
        if (bmval1 & FATTR4_WORD1_NUMLINKS) {
                READ_BUF(4);
                len += 4;
                READ32(nfp->nlink);
                dprintk("read_attrs: nlinks=0%o\n", nfp->nlink);
        }
        if (bmval1 & FATTR4_WORD1_OWNER) {
                READ_BUF(4);
		len += 4;
		READ32(dummy32);    /* name length */
		if (dummy32 > XDR_MAX_NETOBJ) {
			dprintk("read_attrs: name too long!\n");
			goto xdr_error;
		}
		READ_BUF(dummy32);
		len += (XDR_QUADLEN(dummy32) << 2);
		if ((status = nfs_idmap_id(server, IDMAP_TYPE_USER,
			 (char *)p, len, &nfp->uid)) == -1) {
			dprintk("read_attrs: gss_get_num failed!\n");
			/* goto out; */
			nfp->uid = -2;
		}
		dprintk("read_attrs: uid=%d\n", (int)nfp->uid);
        }
        if (bmval1 & FATTR4_WORD1_OWNER_GROUP) {
                READ_BUF(4);
		len += 4;
		READ32(dummy32);
		if (dummy32 > XDR_MAX_NETOBJ) {
			dprintk("read_attrs: name too long!\n");
			goto xdr_error;
		}
		READ_BUF(dummy32);
		len += (XDR_QUADLEN(dummy32) << 2);
		if ((status = nfs_idmap_id(server, IDMAP_TYPE_GROUP,
			 (char *)p, len, &nfp->gid)) == -1) {
			dprintk("read_attrs: gss_get_num failed!\n");
			nfp->gid = -2;
			/* goto out; */
		}
		dprintk("read_attrs: gid=%d\n", (int)nfp->gid);
        }
        if (bmval1 & FATTR4_WORD1_RAWDEV) {
		uint32_t major, minor;

		READ_BUF(8);
		len += 8;
		READ32(major);
		READ32(minor);
		nfp->rdev = MKDEV(major, minor);
		if (MAJOR(nfp->rdev) != major || MINOR(nfp->rdev) != minor)
			nfp->rdev = 0;
		dprintk("read_attrs: rdev=%u:%u\n", major, minor);
        }
        if (bmval1 & FATTR4_WORD1_SPACE_AVAIL) {
                READ_BUF(8);
                len += 8;
                READ64(fsstat->abytes);
                dprintk("read_attrs: savail=0x%Lx\n", (long long) fsstat->abytes);
        }
	if (bmval1 & FATTR4_WORD1_SPACE_FREE) {
                READ_BUF(8);
                len += 8;
                READ64(fsstat->fbytes);
                dprintk("read_attrs: sfree=0x%Lx\n", (long long) fsstat->fbytes);
        }
        if (bmval1 & FATTR4_WORD1_SPACE_TOTAL) {
                READ_BUF(8);
                len += 8;
                READ64(fsstat->tbytes);
                dprintk("read_attrs: stotal=0x%Lx\n", (long long) fsstat->tbytes);
        }
        if (bmval1 & FATTR4_WORD1_SPACE_USED) {
                READ_BUF(8);
                len += 8;
                READ64(nfp->du.nfs3.used);
                dprintk("read_attrs: sused=0x%Lx\n", (long long) nfp->du.nfs3.used);
        }
        if (bmval1 & FATTR4_WORD1_TIME_ACCESS) {
                READ_BUF(12);
                len += 12;
                READTIME(nfp->atime);
                dprintk("read_attrs: atime=%ld\n", (long)nfp->atime.tv_sec);
        }
        if (bmval1 & FATTR4_WORD1_TIME_METADATA) {
                READ_BUF(12);
                len += 12;
                READTIME(nfp->ctime);
                dprintk("read_attrs: ctime=%ld\n", (long)nfp->ctime.tv_sec);
        }
        if (bmval1 & FATTR4_WORD1_TIME_MODIFY) {
                READ_BUF(12);
                len += 12;
                READTIME(nfp->mtime);
                dprintk("read_attrs: mtime=%ld\n", (long)nfp->mtime.tv_sec);
        }
        if (len != attrlen)
                goto xdr_error;
	
        DECODE_TAIL;
}

static int
decode_change_attr(struct xdr_stream *xdr, uint64_t *change_attr)
{
	uint32_t *p;
	uint32_t attrlen, bmlen, bmval = 0;
	int status;

	status = decode_op_hdr(xdr, OP_GETATTR);
	if (status)
		return status;
	READ_BUF(4);
	READ32(bmlen);
	if (bmlen < 1)
		return -EIO;
	READ_BUF(bmlen << 2);
	READ32(bmval);
	if (bmval != FATTR4_WORD0_CHANGE) {
		printk(KERN_NOTICE "decode_change_attr: server returned bad attribute bitmap 0x%x\n",
			(unsigned int)bmval);
		return -EIO;
	}
	READ_BUF(4);
	READ32(attrlen);
	READ_BUF(attrlen);
	if (attrlen < 8) {
		printk(KERN_NOTICE "decode_change_attr: server returned bad attribute length %u\n",
				(unsigned int)attrlen);
		return -EIO;
	}
	READ64(*change_attr);
	return 0;
}

static int
decode_read_getattr(struct xdr_stream *xdr, struct nfs_fattr *fattr)
{
	uint32_t *p;
	uint32_t attrlen, bmlen, bmval0 = 0, bmval1 = 0;
	int status;

	status = decode_op_hdr(xdr, OP_GETATTR);
	if (status)
		return status;
	READ_BUF(4);
	READ32(bmlen);
	if (bmlen < 1)
		return -EIO;
	READ_BUF(bmlen << 2);
	READ32(bmval0);
	if (bmval0 != FATTR4_WORD0_CHANGE)
		goto out_bad_bitmap;
	if (bmlen > 1) {
		READ32(bmval1);
		if (bmval1 & ~(FATTR4_WORD1_TIME_ACCESS))
			goto out_bad_bitmap;
	}
	READ_BUF(4);
	READ32(attrlen);
	READ_BUF(attrlen);
	if (attrlen < 16) {
		printk(KERN_NOTICE "decode_post_write_getattr: server returned bad attribute length %u\n",
				(unsigned int)attrlen);
		return -EIO;
	}
	READ64(fattr->change_attr);
	if (bmval1 & FATTR4_WORD1_TIME_ACCESS)
		READTIME(fattr->atime);
	fattr->bitmap[0] = bmval0;
	fattr->bitmap[1] = bmval1;
	return 0;
out_bad_bitmap:
	printk(KERN_NOTICE "decode_read_getattr: server returned bad attribute bitmap 0x%x/0x%x\n",
			(unsigned int)bmval0, (unsigned int)bmval1);
	return -EIO;
}

static int
decode_pre_write_getattr(struct xdr_stream *xdr, struct nfs_fattr *fattr)
{
	return decode_change_attr(xdr, &fattr->pre_change_attr);
}

static int
decode_post_write_getattr(struct xdr_stream *xdr, struct nfs_fattr *fattr)
{
	uint32_t *p;
	uint32_t attrlen, bmlen, bmval0 = 0, bmval1 = 0;
	int status;

	status = decode_op_hdr(xdr, OP_GETATTR);
	if (status)
		return status;
	READ_BUF(4);
	READ32(bmlen);
	if (bmlen < 1)
		return -EIO;
	READ_BUF(bmlen << 2);
	READ32(bmval0);
	if (bmval0 != (FATTR4_WORD0_CHANGE|FATTR4_WORD0_SIZE))
		goto out_bad_bitmap;
	if (bmlen > 1) {
		READ32(bmval1);
		if (bmval1 & ~(FATTR4_WORD1_SPACE_USED |
					FATTR4_WORD1_TIME_METADATA |
					FATTR4_WORD1_TIME_MODIFY))
			goto out_bad_bitmap;
	}
	READ_BUF(4);
	READ32(attrlen);
	READ_BUF(attrlen);
	if (attrlen < 16) {
		printk(KERN_NOTICE "decode_post_write_getattr: server returned bad attribute length %u\n",
				(unsigned int)attrlen);
		return -EIO;
	}
	READ64(fattr->change_attr);
	READ64(fattr->size);
	if (bmval1 & FATTR4_WORD1_SPACE_USED)
		READ64(fattr->du.nfs3.used);
	if (bmval1 & FATTR4_WORD1_TIME_METADATA)
		READTIME(fattr->ctime);
	if (bmval1 & FATTR4_WORD1_TIME_MODIFY)
		READTIME(fattr->mtime);
	fattr->bitmap[0] = bmval0;
	fattr->bitmap[1] = bmval1;
	return 0;
out_bad_bitmap:
	printk(KERN_NOTICE "decode_post_write_getattr: server returned bad attribute bitmap 0x%x/0x%x\n",
			(unsigned int)bmval0, (unsigned int)bmval1);
	return -EIO;
}


static int
decode_getfh(struct xdr_stream *xdr, struct nfs4_getfh *getfh)
{
	struct nfs_fh *fh = getfh->gf_fhandle;
	uint32_t *p;
	uint32_t len;
	int status;

	status = decode_op_hdr(xdr, OP_GETFH);
	if (status)
		return status;
	/* Zero handle first to allow comparisons */
	memset(fh, 0, sizeof(*fh));

	READ_BUF(4);
	READ32(len);
	if (len > NFS_MAXFHSIZE)
		return -EIO;
	fh->size = len;
	READ_BUF(len);
	COPYMEM(fh->data, len);
	return 0;
}

static int
decode_link(struct xdr_stream *xdr, struct nfs4_link *link)
{
	int status;
	
	status = decode_op_hdr(xdr, OP_LINK);
	if (status)
		return status;
	return decode_change_info(xdr, link->ln_cinfo);
}

static int
decode_lookup(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_LOOKUP);
}

static int
decode_open(struct xdr_stream *xdr, struct nfs_openres *res)
{
        uint32_t *p;
        uint32_t bmlen, delegation_type;
        int status;

        status = decode_op_hdr(xdr, OP_OPEN);
        if (status)
                return status;
        READ_BUF(sizeof(res->stateid.data));
        COPYMEM(res->stateid.data, sizeof(res->stateid.data));

        decode_change_info(xdr, res->cinfo);

        READ_BUF(8);
        READ32(res->rflags);
        READ32(bmlen);
        if (bmlen > 10)
                goto xdr_error;

        READ_BUF((bmlen << 2) + 4);
        p += bmlen;
        READ32(delegation_type);
        if (delegation_type != NFS4_OPEN_DELEGATE_NONE)
                goto xdr_error;

        DECODE_TAIL;
}

static int
decode_open_confirm(struct xdr_stream *xdr, struct nfs_open_confirmres *res)
{
        uint32_t *p;

        res->status = decode_op_hdr(xdr, OP_OPEN_CONFIRM);
        if (res->status)
                return res->status;
        READ_BUF(sizeof(res->stateid.data));
        COPYMEM(res->stateid.data, sizeof(res->stateid.data));
        return 0;
}


static int
decode_putfh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_PUTFH);
}

static int
decode_putrootfh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_PUTROOTFH);
}

static int
decode_read(struct xdr_stream *xdr, struct rpc_rqst *req, struct nfs_readres *res)
{
	struct iovec *iov = req->rq_rvec;
	uint32_t *p;
	uint32_t count, eof, recvd, hdrlen;
	int status;

	status = decode_op_hdr(xdr, OP_READ);
	if (status)
		return status;
	READ_BUF(8);
	READ32(eof);
	READ32(count);
	hdrlen = (u8 *) p - (u8 *) iov->iov_base;
	recvd = req->rq_received - hdrlen;
	if (count > recvd) {
		printk(KERN_WARNING "NFS: server cheating in read reply: "
				"count %u > recvd %u\n", count, recvd);
		count = recvd;
		eof = 0;
	}
	xdr_read_pages(xdr, count);
	res->eof = eof;
	res->count = count;
	return 0;
}

static int
decode_readdir(struct xdr_stream *xdr, struct rpc_rqst *req, struct nfs4_readdir *readdir)
{
	struct xdr_buf	*rcvbuf = &req->rq_rcv_buf;
	struct page	*page = *rcvbuf->pages;
	struct iovec	*iov = rcvbuf->head;
	unsigned int	nr, pglen = rcvbuf->page_len;
	uint32_t	*end, *entry, *p, *kaddr;
	uint32_t	len, attrlen, word;
	int 		i, hdrlen, recvd, status;

	status = decode_op_hdr(xdr, OP_READDIR);
	if (status)
		return status;
	READ_BUF(8);
	COPYMEM(readdir->rd_resp_verifier.data, 8);

	hdrlen = (char *) p - (char *) iov->iov_base;
	recvd = req->rq_received - hdrlen;
	if (pglen > recvd)
		pglen = recvd;
	xdr_read_pages(xdr, pglen);

	BUG_ON(pglen + readdir->rd_pgbase > PAGE_CACHE_SIZE);
	kaddr = p = (uint32_t *) kmap_atomic(page, KM_USER0);
	end = (uint32_t *) ((char *)p + pglen + readdir->rd_pgbase);
	entry = p;
	for (nr = 0; *p++; nr++) {
		if (p + 3 > end)
			goto short_pkt;
		p += 2;     /* cookie */
		len = ntohl(*p++);  /* filename length */
		if (len > NFS4_MAXNAMLEN) {
			printk(KERN_WARNING "NFS: giant filename in readdir (len 0x%x)\n", len);
			goto err_unmap;
		}
			
		p += XDR_QUADLEN(len);
		if (p + 1 > end)
			goto short_pkt;
		len = ntohl(*p++);  /* bitmap length */
		if (len > 10) {
			printk(KERN_WARNING "NFS: giant bitmap in readdir (len 0x%x)\n", len);
			goto err_unmap;
		}
		if (p + len + 1 > end)
			goto short_pkt;
		attrlen = 0;
		for (i = 0; i < len; i++) {
			word = ntohl(*p++);
			if (!word)
				continue;
			else if (i == 0 && word == FATTR4_WORD0_FILEID) {
				attrlen = 8;
				continue;
			}
			printk(KERN_WARNING "NFS: unexpected bitmap word in readdir (0x%x)\n", word);
			goto err_unmap;
		}
		if (ntohl(*p++) != attrlen) {
			printk(KERN_WARNING "NFS: unexpected attrlen in readdir\n");
			goto err_unmap;
		}
		p += XDR_QUADLEN(attrlen);
		if (p + 1 > end)
			goto short_pkt;
	}
	if (!nr && (entry[0] != 0 || entry[1] == 0))
		goto short_pkt;
out:	
	kunmap_atomic(kaddr, KM_USER0);
	return 0;
short_pkt:
	entry[0] = entry[1] = 0;
	/* truncate listing ? */
	if (!nr) {
		printk(KERN_NOTICE "NFS: readdir reply truncated!\n");
		entry[1] = 1;
	}
	goto out;
err_unmap:
	kunmap_atomic(kaddr, KM_USER0);
	return -errno_NFSERR_IO;
}

static int
decode_readlink(struct xdr_stream *xdr, struct rpc_rqst *req, struct nfs4_readlink *readlink)
{
	struct xdr_buf *rcvbuf = &req->rq_rcv_buf;
	struct iovec *iov = rcvbuf->head;
	uint32_t *strlen;
	unsigned int hdrlen, len;
	char *string;
	int status;

	status = decode_op_hdr(xdr, OP_READLINK);
	if (status)
		return status;

	hdrlen = (char *) xdr->p - (char *) iov->iov_base;
	if (iov->iov_len > hdrlen) {
		dprintk("NFS: READLINK header is short. iovec will be shifted.\n");
		xdr_shift_buf(rcvbuf, iov->iov_len - hdrlen);

	}
	/*
	 * The XDR encode routine has set things up so that
	 * the link text will be copied directly into the
	 * buffer.  We just have to do overflow-checking,
	 * and and null-terminate the text (the VFS expects
	 * null-termination).
	 */
	strlen = (uint32_t *) kmap_atomic(rcvbuf->pages[0], KM_USER0);
	len = ntohl(*strlen);
	if (len > PAGE_CACHE_SIZE - 5) {
		printk(KERN_WARNING "nfs: server returned giant symlink!\n");
		kunmap_atomic(strlen, KM_USER0);
		return -EIO;
	}
	*strlen = len;

	string = (char *)(strlen + 1);
	string[len] = '\0';
	kunmap_atomic(strlen, KM_USER0);
	return 0;
}

static int
decode_restorefh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_RESTOREFH);
}

static int
decode_remove(struct xdr_stream *xdr, struct nfs4_remove *remove)
{
	int status;

	status = decode_op_hdr(xdr, OP_REMOVE);
	if (status)
		goto out;
	status = decode_change_info(xdr, remove->rm_cinfo);
out:
	return status;
}

static int
decode_rename(struct xdr_stream *xdr, struct nfs4_rename *rename)
{
	int status;

	status = decode_op_hdr(xdr, OP_RENAME);
	if (status)
		goto out;
	if ((status = decode_change_info(xdr, rename->rn_src_cinfo)))
		goto out;
	if ((status = decode_change_info(xdr, rename->rn_dst_cinfo)))
		goto out;
out:
	return status;
}

static int
decode_renew(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_RENEW);
}

static int
decode_savefh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_SAVEFH);
}

static int
decode_setattr(struct xdr_stream *xdr, struct nfs_setattrres *res)
{
	uint32_t *p;
	uint32_t bmlen;
	int status;

        
	status = decode_op_hdr(xdr, OP_SETATTR);
	if (status)
		return status;
	READ_BUF(4);
	READ32(bmlen);
	READ_BUF(bmlen << 2);
	return 0;
}

static int
decode_setclientid(struct xdr_stream *xdr, struct nfs4_setclientid *setclientid)
{
	uint32_t *p;
	uint32_t opnum;
	int32_t nfserr;

	READ_BUF(8);
	READ32(opnum);
	if (opnum != OP_SETCLIENTID) {
		printk(KERN_NOTICE
				"nfs4_decode_setclientid: Server returned operation"
			       	" %d\n", opnum);
		return -EIO;
	}
	READ32(nfserr);
	if (nfserr == NFS_OK) {
		READ_BUF(8 + sizeof(setclientid->sc_state->cl_confirm.data));
		READ64(setclientid->sc_state->cl_clientid);
		COPYMEM(setclientid->sc_state->cl_confirm.data, sizeof(setclientid->sc_state->cl_confirm.data));
	} else if (nfserr == NFSERR_CLID_INUSE) {
		uint32_t len;

		/* skip netid string */
		READ_BUF(4);
		READ32(len);
		READ_BUF(len);

		/* skip uaddr string */
		READ_BUF(4);
		READ32(len);
		READ_BUF(len);
		return -EEXIST;
	} else
		return -nfs_stat_to_errno(nfserr);

	return 0;
}

static int
decode_setclientid_confirm(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_SETCLIENTID_CONFIRM);
}

static int
decode_write(struct xdr_stream *xdr, struct nfs_writeres *res)
{
	uint32_t *p;
	int status;

	status = decode_op_hdr(xdr, OP_WRITE);
	if (status)
		return status;

	READ_BUF(16);
	READ32(res->count);
	READ32(res->verf->committed);
	COPYMEM(res->verf->verifier, 8);
	return 0;
}

/* FIXME: this sucks */
static int
decode_compound(struct xdr_stream *xdr, struct nfs4_compound *cp, struct rpc_rqst *req)
{
	struct compound_hdr hdr;
	struct nfs4_op *op;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;

	cp->toplevel_status = hdr.status;

	/*
	 * We need this if our zero-copy I/O is going to work.  Rumor has
	 * it that the spec will soon mandate it...
	 */
	if (hdr.taglen != cp->taglen)
		dprintk("nfs4: non-conforming server returns tag length mismatch!\n");

	cp->resp_nops = hdr.nops;
	if (hdr.nops > cp->req_nops) {
		dprintk("nfs4: resp_nops > req_nops!\n");
		goto xdr_error;
	}

	op = &cp->ops[0];
	for (cp->nops = 0; cp->nops < cp->resp_nops; cp->nops++, op++) {
		switch (op->opnum) {
		case OP_ACCESS:
			status = decode_access(xdr, &op->u.access);
			break;
		case OP_CREATE:
			status = decode_create(xdr, &op->u.create);
			break;
		case OP_GETATTR:
			status = decode_getattr(xdr, &op->u.getattr, cp->server);
			break;
		case OP_GETFH:
			status = decode_getfh(xdr, &op->u.getfh);
			break;
		case OP_LINK:
			status = decode_link(xdr, &op->u.link);
			break;
		case OP_LOOKUP:
			status = decode_lookup(xdr);
			break;
		case OP_PUTFH:
			status = decode_putfh(xdr);
			break;
		case OP_PUTROOTFH:
			status = decode_putrootfh(xdr);
			break;
		case OP_READDIR:
			status = decode_readdir(xdr, req, &op->u.readdir);
			break;
		case OP_READLINK:
			status = decode_readlink(xdr, req, &op->u.readlink);
			break;
		case OP_RESTOREFH:
			status = decode_restorefh(xdr);
			break;
		case OP_REMOVE:
			status = decode_remove(xdr, &op->u.remove);
			break;
		case OP_RENAME:
			status = decode_rename(xdr, &op->u.rename);
			break;
		case OP_RENEW:
			status = decode_renew(xdr);
			break;
		case OP_SAVEFH:
			status = decode_savefh(xdr);
			break;
		case OP_SETCLIENTID:
			status = decode_setclientid(xdr, &op->u.setclientid);
			break;
		case OP_SETCLIENTID_CONFIRM:
			status = decode_setclientid_confirm(xdr);
			break;
		default:
			BUG();
			return -EIO;
		}
		if (status)
			break;
	}

	DECODE_TAIL;
}
/*
 * END OF "GENERIC" DECODE ROUTINES.
 */

/*
 * Decode COMPOUND response
 */
static int
nfs4_xdr_dec_compound(struct rpc_rqst *rqstp, uint32_t *p, struct nfs4_compound *cp)
{
	struct xdr_stream xdr;
	int status;
	
	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound(&xdr, cp, rqstp)))
		goto out;
	
	status = 0;
	if (cp->toplevel_status)
		status = -nfs_stat_to_errno(cp->toplevel_status);

out:
	return status;
}

/*
 * Decode CLOSE response
 */
static int
nfs4_xdr_dec_close(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_closeres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
        if (status)
                goto out;
        status = decode_putfh(&xdr);
        if (status)
                goto out;
        status = decode_close(&xdr, res);
out:
        return status;
}

/*
 * Decode OPEN response
 */
static int
nfs4_xdr_dec_open(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_openres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
	struct nfs4_getfh gfh	= {
		.gf_fhandle = &res->fh,
	};
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
        if (status)
                goto out;
        status = decode_putfh(&xdr);
        if (status)
                goto out;
        status = decode_savefh(&xdr);
        if (status)
                goto out;
        status = decode_open(&xdr, res);
        if (status)
                goto out;
        status = decode_getattr(&xdr, res->f_getattr, res->server);
        if (status)
                goto out;
        status = decode_getfh(&xdr, &gfh);
        if (status)
                goto out;
        status = decode_restorefh(&xdr);
        if (status)
                goto out;
        status = decode_getattr(&xdr, res->d_getattr, res->server);
        if (status)
                goto out;
out:
        return status;
}

/*
 * Decode OPEN_CONFIRM response
 */
static int
nfs4_xdr_dec_open_confirm(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_open_confirmres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
        if (status)
                goto out;
        status = decode_putfh(&xdr);
        if (status)
                goto out;
        status = decode_open_confirm(&xdr, res);
out:
        return status;
}

/*
 * Decode SETATTR response
 */
static int
nfs4_xdr_dec_setattr(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_setattrres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
        if (status)
                goto out;
        status = decode_putfh(&xdr);
        if (status)
                goto out;
        status = decode_setattr(&xdr, res);
        if (status)
                goto out;
        status = decode_getattr(&xdr, res->attr, res->server);
out:
        return status;
}


/*
 * Decode Read response
 */
static int
nfs4_xdr_dec_read(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_readres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_read(&xdr, rqstp, res);
	if (status)
		goto out;
	status = decode_read_getattr(&xdr, res->fattr);
	if (!status)
		status = -nfs_stat_to_errno(hdr.status);
	if (!status)
		status = res->count;
out:
	return status;
}

/*
 * Decode WRITE response
 */
static int
nfs4_xdr_dec_write(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_writeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_pre_write_getattr(&xdr, res->fattr);
	if (status)
		goto out;
	status = decode_write(&xdr, res);
	if (status)
		goto out;
	status = decode_post_write_getattr(&xdr, res->fattr);
	if (!status)
		status = -nfs_stat_to_errno(hdr.status);
	if (!status)
		status = res->count;
out:
	return status;
}

/*
 * Decode COMMIT response
 */
static int
nfs4_xdr_dec_commit(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_writeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_pre_write_getattr(&xdr, res->fattr);
	if (status)
		goto out;
	status = decode_commit(&xdr, res);
	if (status)
		goto out;
	status = decode_post_write_getattr(&xdr, res->fattr);
	if (!status)
		status = -nfs_stat_to_errno(hdr.status);
out:
	return status;
}

uint32_t *
nfs4_decode_dirent(uint32_t *p, struct nfs_entry *entry, int plus)
{
	uint32_t len;

	if (!*p++) {
		if (!*p)
			return ERR_PTR(-EAGAIN);
		entry->eof = 1;
		return ERR_PTR(-EBADCOOKIE);
	}

	entry->prev_cookie = entry->cookie;
	p = xdr_decode_hyper(p, &entry->cookie);
	entry->len = ntohl(*p++);
	entry->name = (const char *) p;
	p += XDR_QUADLEN(entry->len);

	/*
	 * In case the server doesn't return an inode number,
	 * we fake one here.  (We don't use inode number 0,
	 * since glibc seems to choke on it...)
	 */
	entry->ino = 1;

	len = ntohl(*p++);             /* bitmap length */
	p += len;
	len = ntohl(*p++);             /* attribute buffer length */
	if (len)
		p = xdr_decode_hyper(p, &entry->ino);

	entry->eof = !p[0] && p[1];
	return p;
}

#ifndef MAX
# define MAX(a, b)	(((a) > (b))? (a) : (b))
#endif

#define PROC(proc, argtype, restype)				\
[NFSPROC4_CLNT_##proc] = {					\
	.p_proc   = NFSPROC4_COMPOUND,				\
	.p_encode = (kxdrproc_t) nfs4_xdr_##argtype,		\
	.p_decode = (kxdrproc_t) nfs4_xdr_##restype,		\
	.p_bufsiz = MAX(NFS4_##argtype##_sz,NFS4_##restype##_sz) << 2,	\
    }

struct rpc_procinfo	nfs4_procedures[] = {
  PROC(COMPOUND,	enc_compound,	dec_compound),
  PROC(READ,		enc_read,	dec_read),
  PROC(WRITE,		enc_write,	dec_write),
  PROC(COMMIT,		enc_commit,	dec_commit),
  PROC(OPEN,		enc_open,	dec_open),
  PROC(OPEN_CONFIRM,	enc_open_confirm,	dec_open_confirm),
  PROC(CLOSE,		enc_close,	dec_close),
  PROC(SETATTR,		enc_setattr,	dec_setattr),
};

struct rpc_version		nfs_version4 = {
	.number			= 4,
	.nrprocs		= sizeof(nfs4_procedures)/sizeof(nfs4_procedures[0]),
	.procs			= nfs4_procedures
};

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */

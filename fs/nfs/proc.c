/*
 *  linux/fs/nfs/proc.c
 *
 *  Copyright (C) 1992, 1993, 1994  Rick Sladkey
 *
 *  OS-independent nfs remote procedure call functions
 *
 *  Tuned by Alan Cox <A.Cox@swansea.ac.uk> for >3K buffers
 *  so at last we can have decent(ish) throughput off a 
 *  Sun server.
 *
 *  Coding optimized and cleaned up by Florian La Roche.
 *  Note: Error returns are optimized for NFS_OK, which isn't translated via
 *  nfs_stat_to_errno(), but happens to be already the right return code.
 *
 *  FixMe: We ought to define a sensible small max size for
 *  things like getattr that are tiny packets and use the
 *  old get_free_page stuff with it.
 *
 *  Also, the code currently doesn't check the size of the packet, when
 *  it decodes the packet.
 *
 *  Feel free to fix it and mail me the diffs if it worries you.
 */

/*
 * Defining NFS_PROC_DEBUG causes a lookup of a file named
 * "xyzzy" to toggle debugging.  Just cd to an NFS-mounted
 * filesystem and type 'ls xyzzy' to turn on debugging.
 */

#if 0
#define NFS_PROC_DEBUG
#endif

#ifdef MODULE
#include <linux/module.h>
#endif

#include <linux/param.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/nfs_fs.h>
#include <linux/utsname.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/in.h>
#include <asm/segment.h>

#ifdef NFS_PROC_DEBUG

static int proc_debug = 0;
#define PRINTK(format, args...) \
	do {						\
		if (proc_debug)				\
			printk(format , ## args);	\
	} while (0)

#else /* !NFS_PROC_DEBUG */

#define PRINTK(format, args...) do ; while (0)

#endif /* !NFS_PROC_DEBUG */

/* Mapping from NFS error code to "errno" error code. */
#define errno_NFSERR_IO EIO

static int *nfs_rpc_header(int *p, int procedure, int ruid);
static int *nfs_rpc_verify(int *p);
static int nfs_stat_to_errno(int stat);

/*
 * Our memory allocation and release functions.
 */
 
#define NFS_SLACK_SPACE		1024	/* Total overkill */ 
/* !!! Be careful, this constant is now also used in sock.c...
   We should easily convert to not using it anymore for most cases... */

static inline int *nfs_rpc_alloc(int size)
{
	int *i;

	while (!(i = (int *)kmalloc(size+NFS_SLACK_SPACE,GFP_NFS))) {
		schedule();
	}
	return i;
}

static inline void nfs_rpc_free(int *p)
{
	kfree((void *)p);
}

/*
 * Here are a bunch of xdr encode/decode functions that convert
 * between machine dependent and xdr data formats.
 */

#define QUADLEN(len) (((len) + 3) >> 2)

static inline int *xdr_encode_fhandle(int *p, struct nfs_fh *fhandle)
{
	*((struct nfs_fh *) p) = *fhandle;
	return p + QUADLEN(sizeof(*fhandle));
}

static inline int *xdr_decode_fhandle(int *p, struct nfs_fh *fhandle)
{
	*fhandle = *((struct nfs_fh *) p);
	return p + QUADLEN(sizeof(*fhandle));
}

static inline int *xdr_encode_string(int *p, const char *string)
{
	int len = strlen(string);
	int quadlen = QUADLEN(len);

	p[quadlen] = 0;
	*p++ = htonl(len);
	memcpy(p, string, len);
	return p + quadlen;
}

static inline int *xdr_decode_string(int *p, char *string, unsigned int maxlen)
{
	unsigned int len = ntohl(*p++);
	if (len > maxlen)
		return NULL;
	memcpy(string, p, len);
	string[len] = '\0';
	return p + QUADLEN(len);
}

static inline int *xdr_decode_string2(int *p, char **string, unsigned int *len,
			unsigned int maxlen)
{
	*len = ntohl(*p++);
	if (*len > maxlen)
		return NULL;
	*string = (char *) p;
	return p + QUADLEN(*len);
}


static inline int *xdr_encode_data(int *p, char *data, int len)
{
	int quadlen = QUADLEN(len);
	
	p[quadlen] = 0;
	*p++ = htonl(len);
	memcpy_fromfs(p, data, len);
	return p + quadlen;
}

static inline int *xdr_decode_data(int *p, char *data, int *lenp, int maxlen,
			int fs)
{
	unsigned len = *lenp = ntohl(*p++);
	if (len > maxlen)
		return NULL;
	if (fs)
		memcpy_tofs(data, p, len);
	else
		memcpy(data, p, len);
	return p + QUADLEN(len);
}

static int *xdr_decode_fattr(int *p, struct nfs_fattr *fattr)
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

static int *xdr_encode_sattr(int *p, struct nfs_sattr *sattr)
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

static int *xdr_decode_entry(int *p, struct nfs_entry *entry)
{
	entry->fileid = ntohl(*p++);
	if (!(p = xdr_decode_string(p, entry->name, NFS_MAXNAMLEN)))
		return NULL;
	entry->cookie = ntohl(*p++);
	entry->eof = 0;
	return p;
}

static int *xdr_decode_fsinfo(int *p, struct nfs_fsinfo *res)
{
	res->tsize = ntohl(*p++);
	res->bsize = ntohl(*p++);
	res->blocks = ntohl(*p++);
	res->bfree = ntohl(*p++);
	res->bavail = ntohl(*p++);
	return p;
}

/*
 * One function for each procedure in the NFS protocol.
 */

int nfs_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle,
		     struct nfs_fattr *fattr)
{
	int *p, *p0;
	int status;
	int ruid = 0;

	PRINTK("NFS call  getattr\n");
	if (!(p0 = nfs_rpc_alloc(server->rsize)))
		return -EIO;
retry:
	p = nfs_rpc_header(p0, NFSPROC_GETATTR, ruid);
	p = xdr_encode_fhandle(p, fhandle);
	if ((status = nfs_rpc_call(server, p0, p, server->rsize)) < 0) {
		nfs_rpc_free(p0);
		return status;
	}
	if (!(p = nfs_rpc_verify(p0)))
		status = -errno_NFSERR_IO;
	else if ((status = ntohl(*p++)) == NFS_OK) {
		p = xdr_decode_fattr(p, fattr);
		PRINTK("NFS reply getattr\n");
		/* status = 0; */
	}
	else {
		if (!ruid && current->fsuid == 0 && current->uid != 0) {
			ruid = 1;
			goto retry;
		}
		PRINTK("NFS reply getattr failed = %d\n", status);
		status = -nfs_stat_to_errno(status);
	}
	nfs_rpc_free(p0);
	return status;
}

int nfs_proc_setattr(struct nfs_server *server, struct nfs_fh *fhandle,
		     struct nfs_sattr *sattr, struct nfs_fattr *fattr)
{
	int *p, *p0;
	int status;
	int ruid = 0;

	PRINTK("NFS call  setattr\n");
	if (!(p0 = nfs_rpc_alloc(server->wsize)))
		return -EIO;
retry:
	p = nfs_rpc_header(p0, NFSPROC_SETATTR, ruid);
	p = xdr_encode_fhandle(p, fhandle);
	p = xdr_encode_sattr(p, sattr);
	if ((status = nfs_rpc_call(server, p0, p, server->wsize)) < 0) {
		nfs_rpc_free(p0);
		return status;
	}
	if (!(p = nfs_rpc_verify(p0)))
		status = -errno_NFSERR_IO;
	else if ((status = ntohl(*p++)) == NFS_OK) {
		p = xdr_decode_fattr(p, fattr);
		PRINTK("NFS reply setattr\n");
		/* status = 0; */
	}
	else {
		if (!ruid && current->fsuid == 0 && current->uid != 0) {
			ruid = 1;
			goto retry;
		}
		PRINTK("NFS reply setattr failed = %d\n", status);
		status = -nfs_stat_to_errno(status);
	}
	nfs_rpc_free(p0);
	return status;
}

int nfs_proc_lookup(struct nfs_server *server, struct nfs_fh *dir, const char *name,
		    struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	int *p, *p0;
	int status;
	int ruid = 0;

	PRINTK("NFS call  lookup %s\n", name);
#ifdef NFS_PROC_DEBUG
	if (!strcmp(name, "xyzzy"))
		proc_debug = 1 - proc_debug;
#endif
	if (!(p0 = nfs_rpc_alloc(server->rsize)))
		return -EIO;
retry:
	p = nfs_rpc_header(p0, NFSPROC_LOOKUP, ruid);
	p = xdr_encode_fhandle(p, dir);
	p = xdr_encode_string(p, name);
	if ((status = nfs_rpc_call(server, p0, p, server->rsize)) < 0) {
		nfs_rpc_free(p0);
		return status;
	}
	if (!(p = nfs_rpc_verify(p0)))
		status = -errno_NFSERR_IO;
	else if ((status = ntohl(*p++)) == NFS_OK) {
		p = xdr_decode_fhandle(p, fhandle);
		p = xdr_decode_fattr(p, fattr);
		PRINTK("NFS reply lookup\n");
		/* status = 0; */
	}
	else {
		if (!ruid && current->fsuid == 0 && current->uid != 0) {
			ruid = 1;
			goto retry;
		}
		PRINTK("NFS reply lookup failed = %d\n", status);
		status = -nfs_stat_to_errno(status);
	}
	nfs_rpc_free(p0);
	return status;
}

int nfs_proc_readlink(struct nfs_server *server, struct nfs_fh *fhandle,
		int **p0, char **string, unsigned int *len, unsigned int maxlen)
{
	int *p;
	int status, ruid = 0;

	PRINTK("NFS call  readlink\n");
	if (!(*p0 = nfs_rpc_alloc(server->rsize)))
		return -EIO;
retry:
	p = nfs_rpc_header(*p0, NFSPROC_READLINK, ruid);
	p = xdr_encode_fhandle(p, fhandle);
	if ((status = nfs_rpc_call(server, *p0, p, server->rsize)) < 0)
		return status;
	if (!(p = nfs_rpc_verify(*p0)))
		status = -errno_NFSERR_IO;
	else if ((status = ntohl(*p++)) == NFS_OK) {
		if (!(p = xdr_decode_string2(p, string, len, maxlen))) {
			printk("nfs_proc_readlink: giant pathname\n");
			status = -errno_NFSERR_IO;
		}
		else	/* status = 0, */
			PRINTK("NFS reply readlink\n");
	}
	else {
		if (!ruid && current->fsuid == 0 && current->uid != 0) {
			ruid = 1;
			goto retry;
		}
		PRINTK("NFS reply readlink failed = %d\n", status);
		status = -nfs_stat_to_errno(status);
	}
	return status;
}

int nfs_proc_read(struct nfs_server *server, struct nfs_fh *fhandle,
	  int offset, int count, char *data, struct nfs_fattr *fattr, int fs)
{
	int *p, *p0;
	int status;
	int ruid = 0;
	int len;

	PRINTK("NFS call  read %d @ %d\n", count, offset);
	if (!(p0 = nfs_rpc_alloc(server->rsize)))
		return -EIO;
retry:
	p = nfs_rpc_header(p0, NFSPROC_READ, ruid);
	p = xdr_encode_fhandle(p, fhandle);
	*p++ = htonl(offset);
	*p++ = htonl(count);
	*p++ = htonl(count); /* traditional, could be any value */
	if ((status = nfs_rpc_call(server, p0, p, server->rsize)) < 0) {
		nfs_rpc_free(p0);
		return status;
	}
	if (!(p = nfs_rpc_verify(p0)))
		status = -errno_NFSERR_IO;
	else if ((status = ntohl(*p++)) == NFS_OK) {
		p = xdr_decode_fattr(p, fattr);
		if (!(p = xdr_decode_data(p, data, &len, count, fs))) {
			printk("nfs_proc_read: giant data size\n"); 
			status = -errno_NFSERR_IO;
		}
		else {
			status = len;
			PRINTK("NFS reply read %d\n", len);
		}
	}
	else {
		if (!ruid && current->fsuid == 0 && current->uid != 0) {
			ruid = 1;
			goto retry;
		}
		PRINTK("NFS reply read failed = %d\n", status);
		status = -nfs_stat_to_errno(status);
	}
	nfs_rpc_free(p0);
	return status;
}

int nfs_proc_write(struct nfs_server *server, struct nfs_fh *fhandle,
		   int offset, int count, char *data, struct nfs_fattr *fattr)
{
	int *p, *p0;
	int status;
	int ruid = 0;

	PRINTK("NFS call  write %d @ %d\n", count, offset);
	if (!(p0 = nfs_rpc_alloc(server->wsize)))
		return -EIO;
retry:
	p = nfs_rpc_header(p0, NFSPROC_WRITE, ruid);
	p = xdr_encode_fhandle(p, fhandle);
	*p++ = htonl(offset); /* traditional, could be any value */
	*p++ = htonl(offset);
	*p++ = htonl(count); /* traditional, could be any value */
	p = xdr_encode_data(p, data, count);
	if ((status = nfs_rpc_call(server, p0, p, server->wsize)) < 0) {
		nfs_rpc_free(p0);
		return status;
	}
	if (!(p = nfs_rpc_verify(p0)))
		status = -errno_NFSERR_IO;
	else if ((status = ntohl(*p++)) == NFS_OK) {
		p = xdr_decode_fattr(p, fattr);
		PRINTK("NFS reply write\n");
		/* status = 0; */
	}
	else {
		if (!ruid && current->fsuid == 0 && current->uid != 0) {
			ruid = 1;
			goto retry;
		}
		PRINTK("NFS reply write failed = %d\n", status);
		status = -nfs_stat_to_errno(status);
	}
	nfs_rpc_free(p0);
	return status;
}

int nfs_proc_create(struct nfs_server *server, struct nfs_fh *dir,
		    const char *name, struct nfs_sattr *sattr,
		    struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	int *p, *p0;
	int status;
	int ruid = 0;

	PRINTK("NFS call  create %s\n", name);
	if (!(p0 = nfs_rpc_alloc(server->wsize)))
		return -EIO;
retry:
	p = nfs_rpc_header(p0, NFSPROC_CREATE, ruid);
	p = xdr_encode_fhandle(p, dir);
	p = xdr_encode_string(p, name);
	p = xdr_encode_sattr(p, sattr);
	if ((status = nfs_rpc_call(server, p0, p, server->wsize)) < 0) {
		nfs_rpc_free(p0);
		return status;
	}
	if (!(p = nfs_rpc_verify(p0)))
		status = -errno_NFSERR_IO;
	else if ((status = ntohl(*p++)) == NFS_OK) {
		p = xdr_decode_fhandle(p, fhandle);
		p = xdr_decode_fattr(p, fattr);
		PRINTK("NFS reply create\n");
		/* status = 0; */
	}
	else {
		if (!ruid && current->fsuid == 0 && current->uid != 0) {
			ruid = 1;
			goto retry;
		}
		PRINTK("NFS reply create failed = %d\n", status);
		status = -nfs_stat_to_errno(status);
	}
	nfs_rpc_free(p0);
	return status;
}

int nfs_proc_remove(struct nfs_server *server, struct nfs_fh *dir, const char *name)
{
	int *p, *p0;
	int status;
	int ruid = 0;

	PRINTK("NFS call  remove %s\n", name);
	if (!(p0 = nfs_rpc_alloc(server->wsize)))
		return -EIO;
retry:
	p = nfs_rpc_header(p0, NFSPROC_REMOVE, ruid);
	p = xdr_encode_fhandle(p, dir);
	p = xdr_encode_string(p, name);
	if ((status = nfs_rpc_call(server, p0, p, server->wsize)) < 0) {
		nfs_rpc_free(p0);
		return status;
	}
	if (!(p = nfs_rpc_verify(p0)))
		status = -errno_NFSERR_IO;
	else if ((status = ntohl(*p++)) == NFS_OK) {
		PRINTK("NFS reply remove\n");
		/* status = 0; */
	}
	else {
		if (!ruid && current->fsuid == 0 && current->uid != 0) {
			ruid = 1;
			goto retry;
		}
		PRINTK("NFS reply remove failed = %d\n", status);
		status = -nfs_stat_to_errno(status);
	}
	nfs_rpc_free(p0);
	return status;
}

int nfs_proc_rename(struct nfs_server *server,
		    struct nfs_fh *old_dir, const char *old_name,
		    struct nfs_fh *new_dir, const char *new_name)
{
	int *p, *p0;
	int status;
	int ruid = 0;

	PRINTK("NFS call  rename %s -> %s\n", old_name, new_name);
	if (!(p0 = nfs_rpc_alloc(server->wsize)))
		return -EIO;
retry:
	p = nfs_rpc_header(p0, NFSPROC_RENAME, ruid);
	p = xdr_encode_fhandle(p, old_dir);
	p = xdr_encode_string(p, old_name);
	p = xdr_encode_fhandle(p, new_dir);
	p = xdr_encode_string(p, new_name);
	if ((status = nfs_rpc_call(server, p0, p, server->wsize)) < 0) {
		nfs_rpc_free(p0);
		return status;
	}
	if (!(p = nfs_rpc_verify(p0)))
		status = -errno_NFSERR_IO;
	else if ((status = ntohl(*p++)) == NFS_OK) {
		PRINTK("NFS reply rename\n");
		/* status = 0; */
	}
	else {
		if (!ruid && current->fsuid == 0 && current->uid != 0) {
			ruid = 1;
			goto retry;
		}
		PRINTK("NFS reply rename failed = %d\n", status);
		status = -nfs_stat_to_errno(status);
	}
	nfs_rpc_free(p0);
	return status;
}

int nfs_proc_link(struct nfs_server *server, struct nfs_fh *fhandle,
		  struct nfs_fh *dir, const char *name)
{
	int *p, *p0;
	int status;
	int ruid = 0;

	PRINTK("NFS call  link %s\n", name);
	if (!(p0 = nfs_rpc_alloc(server->wsize)))
		return -EIO;
retry:
	p = nfs_rpc_header(p0, NFSPROC_LINK, ruid);
	p = xdr_encode_fhandle(p, fhandle);
	p = xdr_encode_fhandle(p, dir);
	p = xdr_encode_string(p, name);
	if ((status = nfs_rpc_call(server, p0, p, server->wsize)) < 0) {
		nfs_rpc_free(p0);
		return status;
	}
	if (!(p = nfs_rpc_verify(p0)))
		status = -errno_NFSERR_IO;
	else if ((status = ntohl(*p++)) == NFS_OK) {
		PRINTK("NFS reply link\n");
		/* status = 0; */
	}
	else {
		if (!ruid && current->fsuid == 0 && current->uid != 0) {
			ruid = 1;
			goto retry;
		}
		PRINTK("NFS reply link failed = %d\n", status);
		status = -nfs_stat_to_errno(status);
	}
	nfs_rpc_free(p0);
	return status;
}

int nfs_proc_symlink(struct nfs_server *server, struct nfs_fh *dir,
		     const char *name, const char *path, struct nfs_sattr *sattr)
{
	int *p, *p0;
	int status;
	int ruid = 0;

	PRINTK("NFS call  symlink %s -> %s\n", name, path);
	if (!(p0 = nfs_rpc_alloc(server->wsize)))
		return -EIO;
retry:
	p = nfs_rpc_header(p0, NFSPROC_SYMLINK, ruid);
	p = xdr_encode_fhandle(p, dir);
	p = xdr_encode_string(p, name);
	p = xdr_encode_string(p, path);
	p = xdr_encode_sattr(p, sattr);
	if ((status = nfs_rpc_call(server, p0, p, server->wsize)) < 0) {
		nfs_rpc_free(p0);
		return status;
	}
	if (!(p = nfs_rpc_verify(p0)))
		status = -errno_NFSERR_IO;
	else if ((status = ntohl(*p++)) == NFS_OK) {
		PRINTK("NFS reply symlink\n");
		/* status = 0; */
	}
	else {
		if (!ruid && current->fsuid == 0 && current->uid != 0) {
			ruid = 1;
			goto retry;
		}
		PRINTK("NFS reply symlink failed = %d\n", status);
		status = -nfs_stat_to_errno(status);
	}
	nfs_rpc_free(p0);
	return status;
}

int nfs_proc_mkdir(struct nfs_server *server, struct nfs_fh *dir,
		   const char *name, struct nfs_sattr *sattr,
		   struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	int *p, *p0;
	int status;
	int ruid = 0;

	PRINTK("NFS call  mkdir %s\n", name);
	if (!(p0 = nfs_rpc_alloc(server->wsize)))
		return -EIO;
retry:
	p = nfs_rpc_header(p0, NFSPROC_MKDIR, ruid);
	p = xdr_encode_fhandle(p, dir);
	p = xdr_encode_string(p, name);
	p = xdr_encode_sattr(p, sattr);
	if ((status = nfs_rpc_call(server, p0, p, server->wsize)) < 0) {
		nfs_rpc_free(p0);
		return status;
	}
	if (!(p = nfs_rpc_verify(p0)))
		status = -errno_NFSERR_IO;
	else if ((status = ntohl(*p++)) == NFS_OK) {
		p = xdr_decode_fhandle(p, fhandle);
		p = xdr_decode_fattr(p, fattr);
		PRINTK("NFS reply mkdir\n");
		/* status = 0; */
	}
	else {
		if (!ruid && current->fsuid == 0 && current->uid != 0) {
			ruid = 1;
			goto retry;
		}
		PRINTK("NFS reply mkdir failed = %d\n", status);
		status = -nfs_stat_to_errno(status);
	}
	nfs_rpc_free(p0);
	return status;
}

int nfs_proc_rmdir(struct nfs_server *server, struct nfs_fh *dir, const char *name)
{
	int *p, *p0;
	int status;
	int ruid = 0;

	PRINTK("NFS call  rmdir %s\n", name);
	if (!(p0 = nfs_rpc_alloc(server->wsize)))
		return -EIO;
retry:
	p = nfs_rpc_header(p0, NFSPROC_RMDIR, ruid);
	p = xdr_encode_fhandle(p, dir);
	p = xdr_encode_string(p, name);
	if ((status = nfs_rpc_call(server, p0, p, server->wsize)) < 0) {
		nfs_rpc_free(p0);
		return status;
	}
	if (!(p = nfs_rpc_verify(p0)))
		status = -errno_NFSERR_IO;
	else if ((status = ntohl(*p++)) == NFS_OK) {
		PRINTK("NFS reply rmdir\n");
		/* status = 0; */
	}
	else {
		if (!ruid && current->fsuid == 0 && current->uid != 0) {
			ruid = 1;
			goto retry;
		}
		PRINTK("NFS reply rmdir failed = %d\n", status);
		status = -nfs_stat_to_errno(status);
	}
	nfs_rpc_free(p0);
	return status;
}

int nfs_proc_readdir(struct nfs_server *server, struct nfs_fh *fhandle,
		     int cookie, int count, struct nfs_entry *entry)
{
	int *p, *p0;
	int status;
	int ruid = 0;
	int i;
	int size;
	int eof;

	PRINTK("NFS call  readdir %d @ %d\n", count, cookie);
	size = server->rsize;
	if (!(p0 = nfs_rpc_alloc(server->rsize)))
		return -EIO;
retry:
	p = nfs_rpc_header(p0, NFSPROC_READDIR, ruid);
	p = xdr_encode_fhandle(p, fhandle);
	*p++ = htonl(cookie);
	*p++ = htonl(size);
	if ((status = nfs_rpc_call(server, p0, p, server->rsize)) < 0) {
		nfs_rpc_free(p0);
		return status;
	}
	if (!(p = nfs_rpc_verify(p0)))
		status = -errno_NFSERR_IO;
	else if ((status = ntohl(*p++)) == NFS_OK) {
		for (i = 0; i < count && *p++; i++) {
			if (!(p = xdr_decode_entry(p, entry++)))
				break;
		}
		if (!p) {
			printk("nfs_proc_readdir: giant filename\n");
			status = -errno_NFSERR_IO;
		}
		else {
			eof = (i == count && !*p++ && *p++)
			      || (i < count && *p++);
			if (eof && i)
				entry[-1].eof = 1;
			PRINTK("NFS reply readdir %d %s\n", i,
			       eof ? "eof" : "");
			status = i;
		}
	}
	else {
		if (!ruid && current->fsuid == 0 && current->uid != 0) {
			ruid = 1;
			goto retry;
		}
		PRINTK("NFS reply readdir failed = %d\n", status);
		status = -nfs_stat_to_errno(status);
	}
	nfs_rpc_free(p0);
	return status;
}

int nfs_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle,
		    struct nfs_fsinfo *res)
{
	int *p, *p0;
	int status;
	int ruid = 0;

	PRINTK("NFS call  statfs\n");
	if (!(p0 = nfs_rpc_alloc(server->rsize)))
		return -EIO;
retry:
	p = nfs_rpc_header(p0, NFSPROC_STATFS, ruid);
	p = xdr_encode_fhandle(p, fhandle);
	if ((status = nfs_rpc_call(server, p0, p, server->rsize)) < 0) {
		nfs_rpc_free(p0);
		return status;
	}
	if (!(p = nfs_rpc_verify(p0)))
		status = -errno_NFSERR_IO;
	else if ((status = ntohl(*p++)) == NFS_OK) {
		p = xdr_decode_fsinfo(p, res);
		PRINTK("NFS reply statfs\n");
		/* status = 0; */
	}
	else {
		if (!ruid && current->fsuid == 0 && current->uid != 0) {
			ruid = 1;
			goto retry;
		}
		PRINTK("NFS reply statfs failed = %d\n", status);
		status = -nfs_stat_to_errno(status);
	}
	nfs_rpc_free(p0);
	return status;
}

/*
 * Here are a few RPC-assist functions.
 */

static int *nfs_rpc_header(int *p, int procedure, int ruid)
{
	int *p1, *p2;
	int i;
	static int xid = 0;
	unsigned char *sys = (unsigned char *) system_utsname.nodename;

	if (xid == 0) {
		xid = CURRENT_TIME;
		xid ^= (sys[3]<<24) | (sys[2]<<16) | (sys[1]<<8) | sys[0];
	}
	*p++ = htonl(++xid);
	*p++ = htonl(RPC_CALL);
	*p++ = htonl(RPC_VERSION);
	*p++ = htonl(NFS_PROGRAM);
	*p++ = htonl(NFS_VERSION);
	*p++ = htonl(procedure);
	*p++ = htonl(RPC_AUTH_UNIX);
	p1 = p++;
	*p++ = htonl(CURRENT_TIME); /* traditional, could be anything */
	p = xdr_encode_string(p, (char *) sys);
	*p++ = htonl(ruid ? current->uid : current->fsuid);
	*p++ = htonl(current->egid);
	p2 = p++;
	for (i = 0; i < 16 && i < NGROUPS && current->groups[i] != NOGROUP; i++)
		*p++ = htonl(current->groups[i]);
	*p2 = htonl(i);
	*p1 = htonl((p - (p1 + 1)) << 2);
	*p++ = htonl(RPC_AUTH_NULL);
	*p++ = htonl(0);
	return p;
}

static int *nfs_rpc_verify(int *p)
{
	unsigned int n;

	p++;
	if ((n = ntohl(*p++)) != RPC_REPLY) {
		printk("nfs_rpc_verify: not an RPC reply: %d\n", n);
		return NULL;
	}
	if ((n = ntohl(*p++)) != RPC_MSG_ACCEPTED) {
		printk("nfs_rpc_verify: RPC call rejected: %d\n", n);
		return NULL;
	}
	switch (n = ntohl(*p++)) {
	case RPC_AUTH_NULL: case RPC_AUTH_UNIX: case RPC_AUTH_SHORT:
		break;
	default:
		printk("nfs_rpc_verify: bad RPC authentication type: %d\n", n);
		return NULL;
	}
	if ((n = ntohl(*p++)) > 400) {
		printk("nfs_rpc_verify: giant auth size\n");
		return NULL;
	}
	p += QUADLEN(n);
	if ((n = ntohl(*p++)) != RPC_SUCCESS) {
		printk("nfs_rpc_verify: RPC call failed: %d\n", n);
		return NULL;
	}
	return p;
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

static int nfs_stat_to_errno(int stat)
{
	int i;

	for (i = 0; nfs_errtbl[i].stat != -1; i++) {
		if (nfs_errtbl[i].stat == stat)
			return nfs_errtbl[i].errno;
	}
	printk("nfs_stat_to_errno: bad nfs status return value: %d\n", stat);
	return nfs_errtbl[i].errno;
}


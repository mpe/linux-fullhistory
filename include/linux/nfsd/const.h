/*
 * include/linux/nfsd/nfsconst.h
 *
 * Various constants related to NFS.
 *
 * Copyright (C) 1995 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef __NFSCONST_H__
#define __NFSCONST_H__

#include <linux/limits.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/dirent.h>
#include <linux/fs.h>
#include <linux/nfs.h>

/*
 * Maximum protocol version supported by knfsd
 */
#define NFSSVC_MAXVERS		3

/*
 * Maximum blocksize supported by daemon currently at 8K
 */
#define NFSSVC_MAXBLKSIZE	8192

#define NFS2_MAXPATHLEN		1024
#define NFS2_MAXNAMLEN		255
#define NFS2_FHSIZE		NFS_FHSIZE
#define NFS2_COOKIESIZE		4

#define NFS3_MAXPATHLEN		PATH_MAX
#define NFS3_MAXNAMLEN		NAME_MAX
#define NFS3_FHSIZE		NFS_FHSIZE
#define NFS3_COOKIEVERFSIZE	8
#define NFS3_CREATEVERFSIZE	8
#define NFS3_WRITEVERFSIZE	8

#ifdef __KERNEL__

#ifndef NFS_SUPER_MAGIC
# define NFS_SUPER_MAGIC	0x6969
#endif

/*
 * NFS stats. The good thing with these values is that NFSv3 errors are
 * a superset of NFSv2 errors (with the exception of NFSERR_WFLUSH which
 * no-one uses anyway), so we can happily mix code as long as we make sure
 * no NFSv3 errors are returned to NFSv2 clients.
 */
#define NFS_OK			0		/* v2 v3 */
#define NFSERR_PERM		1		/* v2 v3 */
#define NFSERR_NOENT		2		/* v2 v3 */
#define NFSERR_IO		5		/* v2 v3 */
#define NFSERR_NXIO		6		/* v2 v3 */
#define NFSERR_ACCES		13		/* v2 v3 */
#define NFSERR_EXIST		17		/* v2 v3 */
#define NFSERR_XDEV		18		/*    v3 */
#define NFSERR_NODEV		19		/* v2 v3 */
#define NFSERR_NOTDIR		20		/* v2 v3 */
#define NFSERR_ISDIR		21		/* v2 v3 */
#define NFSERR_INVAL		22		/*    v3 */
#define NFSERR_FBIG		27		/* v2 v3 */
#define NFSERR_NOSPC		28		/* v2 v3 */
#define NFSERR_ROFS		30		/* v2 v3 */
#define NFSERR_MLINK		31		/*    v3 */
#define NFSERR_NAMETOOLONG	63		/* v2 v3 */
#define NFSERR_NOTEMPTY		66		/* v2 v3 */
#define NFSERR_DQUOT		69		/* v2 v3 */
#define NFSERR_STALE		70		/* v2 v3 */
#define NFSERR_REMOTE		71		/*    v3 */
#define NFSERR_WFLUSH		99		/* v2    */
#define NFSERR_BADHANDLE	10001		/*    v3 */
#define NFSERR_NOT_SYNC		10002		/*    v3 */
#define NFSERR_BAD_COOKIE	10003		/*    v3 */
#define NFSERR_NOTSUPP		10004		/*    v3 */
#define NFSERR_TOOSMALL		10005		/*    v3 */
#define NFSERR_SERVERFAULT	10006		/*    v3 */
#define NFSERR_BADTYPE		10007		/*    v3 */
#define NFSERR_JUKEBOX		10008		/*    v3 */

#endif /* __KERNEL__ */

#endif /* __NFSCONST_H__ */

/*
 * include/linux/nfsd/const.h
 *
 * Various constants related to NFS.
 *
 * Copyright (C) 1995-1997 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_NFSD_CONST_H
#define _LINUX_NFSD_CONST_H

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
#define NFS3_FHSIZE		64
#define NFS3_COOKIEVERFSIZE	8
#define NFS3_CREATEVERFSIZE	8
#define NFS3_WRITEVERFSIZE	8

#ifdef __KERNEL__

#ifndef NFS_SUPER_MAGIC
# define NFS_SUPER_MAGIC	0x6969
#endif

#endif /* __KERNEL__ */

#endif /* _LINUX_NFSD_CONST_H */

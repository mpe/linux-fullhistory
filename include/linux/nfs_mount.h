#ifndef _LINUX_NFS_MOUNT_H
#define _LINUX_NFS_MOUNT_H

/*
 *  linux/include/linux/nfs_mount.h
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  structure passed from user-space to kernel-space during an nfs mount
 */

/*
 * WARNING!  Do not delete or change the order of these fields.  If
 * a new field is required then add it to the end.  The version field
 * tracks which fields are present.  This will ensure some measure of
 * mount-to-kernel version compatibility.  Some of these aren't used yet
 * but here they are anyway.
 */

#define NFS_NFS_PROGRAM		100003	/* nfsd program number */
#define NFS_NFS_VERSION		2	/* nfsd version */
#define NFS_NFS_PORT		2049	/* portnumber on server for nfsd */

#define NFS_MOUNT_PROGRAM	100005	/* mountd program number */
#define NFS_MOUNT_VERSION	1	/* mountd version */
#define NFS_MOUNT_PROC		1	/* mount process id */
#define NFS_MOUNT_PORT		627	/* portnumber on server for mountd */

#define NFS_PMAP_PROGRAM	100000	/* portmap program number */
#define NFS_PMAP_VERSION	2	/* portmap version */
#define NFS_PMAP_PROC		3	/* portmap getport id */
#define NFS_PMAP_PORT		111	/* portnumber on server for portmap */

struct nfs_mount_data {
	int version;			/* 1 */
	int fd;				/* 1 */
	struct nfs_fh root;		/* 1 */
	int flags;			/* 1 */
	int rsize;			/* 1 */
	int wsize;			/* 1 */
	int timeo;			/* 1 */
	int retrans;			/* 1 */
	int acregmin;			/* 1 */
	int acregmax;			/* 1 */
	int acdirmin;			/* 1 */
	int acdirmax;			/* 1 */
	struct sockaddr_in addr;	/* 1 */
	char hostname[256];		/* 1 */
};

/* bits in the flags field */

#define NFS_MOUNT_SOFT		0x0001	/* 1 */
#define NFS_MOUNT_INTR		0x0002	/* 1 */
#define NFS_MOUNT_SECURE	0x0004	/* 1 */
#define NFS_MOUNT_POSIX		0x0008	/* 1 */
#define NFS_MOUNT_NOCTO		0x0010	/* 1 */
#define NFS_MOUNT_NOAC		0x0020	/* 1 */
 
#endif

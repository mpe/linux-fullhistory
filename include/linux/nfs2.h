/*
 * NFS protocol definitions
 *
 * This file contains constants for Version 2 of the protocol.
 */
#ifndef _LINUX_NFS2_H
#define _LINUX_NFS2_H

#define NFS2_PORT	2049
#define NFS2_MAXDATA	8192
#define NFS2_MAXPATHLEN	1024
#define NFS2_MAXNAMLEN	255
#define NFS2_MAXGROUPS	16
#define NFS2_FHSIZE	32
#define NFS2_COOKIESIZE	4
#define NFS2_FIFO_DEV	(-1)
#define NFS2MODE_FMT	0170000
#define NFS2MODE_DIR	0040000
#define NFS2MODE_CHR	0020000
#define NFS2MODE_BLK	0060000
#define NFS2MODE_REG	0100000
#define NFS2MODE_LNK	0120000
#define NFS2MODE_SOCK	0140000
#define NFS2MODE_FIFO	0010000
	
#endif

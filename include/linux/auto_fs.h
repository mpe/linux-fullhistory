/* -*- linux-c -*- ------------------------------------------------------- *
 *   
 * linux/include/linux/auto_fs.h
 *
 *   Copyright 1997 Transmeta Corporation - All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */


#ifndef _LINUX_AUTO_FS_H
#define _LINUX_AUTO_FS_H

#include <linux/version.h>
#include <linux/fs.h>
#include <linux/limits.h>
#include <linux/ioctl.h>
#include <asm/types.h>

#define AUTOFS_PROTO_VERSION 3

enum autofs_packet_type {
	autofs_ptype_missing,	/* Missing entry (mount request) */
	autofs_ptype_expire,	/* Expire entry (umount request) */
};

struct autofs_packet_hdr {
	int proto_version;	      /* Protocol version */
	enum autofs_packet_type type; /* Type of packet */
};

struct autofs_packet_missing {
	struct autofs_packet_hdr hdr;
        unsigned long wait_queue_token;
	int len;
	char name[NAME_MAX+1];
};	

struct autofs_packet_expire {
	struct autofs_packet_hdr hdr;
	int len;
	char name[NAME_MAX+1];
};

#define AUTOFS_IOC_READY      _IO(0x93,0x60)
#define AUTOFS_IOC_FAIL       _IO(0x93,0x61)
#define AUTOFS_IOC_CATATONIC  _IO(0x93,0x62)
#define AUTOFS_IOC_PROTOVER   _IOR(0x93,0x63,int)
#define AUTOFS_IOC_SETTIMEOUT _IOWR(0x93,0x64,unsigned long)
#define AUTOFS_IOC_EXPIRE     _IOR(0x93,0x65,struct autofs_packet_expire)

#ifdef __KERNEL__

/* Init function */
int init_autofs_fs(void);

#endif /* __KERNEL__ */

#endif /* _LINUX_AUTO_FS_H */

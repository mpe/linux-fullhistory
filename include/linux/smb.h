/*
 *  smb.h
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *
 */

#ifndef _LINUX_SMB_H
#define _LINUX_SMB_H

#define SMB_PORT 139
#define SMB_MAXNAMELEN 255
#define SMB_MAXPATHLEN 1024

#define SMB_DEF_MAX_XMIT 32768

/* Allocate max. 1 page */
#define TRANS2_MAX_TRANSFER (4096-17)

#include <asm/types.h>
#ifdef __KERNEL__
typedef u8  byte;
typedef u16 word;
typedef u32 dword;
#else
typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned long dword;
#endif

/*
 * Set/Get values in SMB-byte order
 */
#define ARCH i386
#if (ARCH == i386)
#define BVAL(p,off)      (*((byte  *)(((void *)p)+off)))
#define WVAL(p,off)      (*((word  *)(((void *)p)+off)))
#define DVAL(p,off)      (*((dword *)(((void *)p)+off)))
#define BSET(p,off,new)  (*((byte  *)(((void *)p)+off))=(new))
#define WSET(p,off,new)  (*((word  *)(((void *)p)+off))=(new))
#define DSET(p,off,new)  (*((dword *)(((void *)p)+off))=(new))

/* where to find the base of the SMB packet proper */
#define smb_base(buf) ((byte *)(((byte *)(buf))+4))

#else
#error "Currently only on 386, sorry"
#endif


#define LANMAN1
#define LANMAN2
#define NT1

enum smb_protocol { 
	PROTOCOL_NONE, 
	PROTOCOL_CORE, 
	PROTOCOL_COREPLUS, 
	PROTOCOL_LANMAN1, 
	PROTOCOL_LANMAN2, 
	PROTOCOL_NT1 
};

enum smb_case_hndl {
	CASE_DEFAULT,
	CASE_LOWER,
	CASE_UPPER
};

#ifdef __KERNEL__

enum smb_conn_state {
        CONN_VALID,             /* everything's fine */
        CONN_INVALID,           /* Something went wrong, but did not
                                   try to reconnect yet. */
        CONN_RETRIED            /* Tried a reconnection, but was refused */
};

struct smb_dskattr {
        word total;
        word allocblocks;
        word blocksize;
        word free;
};

/*
 * Contains all relevant data on a SMB networked file.
 */
struct smb_dirent {
        int             opened; /* is it open on the fileserver? */
	word            fileid;	/* What id to handle a file with? */
	word            attr;	/* Attribute fields, DOS value */

        time_t atime, mtime,    /* Times, as seen by the server, normalized */
               ctime;           /* to UTC. The ugly conversion happens in */
                                /* proc.c */

	unsigned long   size;	/* File size. */
	unsigned short  access;	/* Access bits. */
        unsigned long   f_pos;	/* File position. (For readdir.) */
	char*           path;   /* Complete path, MS-DOS notation, with '\' */
	int             len;	/* Namelength. */
};

#endif  /* __KERNEL__ */
#endif  /* _LINUX_SMB_H */

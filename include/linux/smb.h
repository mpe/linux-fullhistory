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

/* The following macros have been taken directly from Samba. Thanks,
   Andrew! */

#undef CAREFUL_ALIGNMENT

/* we know that the 386 can handle misalignment and has the "right" 
   byteorder */
#if defined(__i386__)
#define CAREFUL_ALIGNMENT 0
#endif

#ifndef CAREFUL_ALIGNMENT
#define CAREFUL_ALIGNMENT 1
#endif

#define BVAL(buf,pos) (((u8 *)(buf))[pos])
#define PVAL(buf,pos) ((unsigned)BVAL(buf,pos))
#define BSET(buf,pos,val) (BVAL(buf,pos) = (val))


#if CAREFUL_ALIGNMENT
#define WVAL(buf,pos) (PVAL(buf,pos)|PVAL(buf,(pos)+1)<<8)
#define DVAL(buf,pos) (WVAL(buf,pos)|WVAL(buf,(pos)+2)<<16)

#define SSVALX(buf,pos,val) (BVAL(buf,pos)=(val)&0xFF,BVAL(buf,pos+1)=(val)>>8)
#define SIVALX(buf,pos,val) (SSVALX(buf,pos,val&0xFFFF),SSVALX(buf,pos+2,val>>16))
#define WSET(buf,pos,val) { word __val = (val); \
	SSVALX((buf),(pos),((word)(__val))); }
#define DSET(buf,pos,val) { dword __val = (val); \
	SIVALX((buf),(pos),((dword)(__val))); }
#else
/* this handles things for architectures like the 386 that can handle
   alignment errors */
/*
   WARNING: This section is dependent on the length of word and dword
   being correct 
*/
#define WVAL(buf,pos) (*(word *)((char *)(buf) + (pos)))
#define DVAL(buf,pos) (*(dword *)((char *)(buf) + (pos)))
#define WSET(buf,pos,val) WVAL(buf,pos)=((word)(val))
#define DSET(buf,pos,val) DVAL(buf,pos)=((dword)(val))
#endif


/* where to find the base of the SMB packet proper */
#define smb_base(buf) ((byte *)(((byte *)(buf))+4))

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

	unsigned long	f_ino;
	umode_t		f_mode;
	nlink_t		f_nlink;
	uid_t		f_uid;
	gid_t		f_gid;
	kdev_t		f_rdev;
	off_t		f_size;
	time_t		f_atime;
	time_t		f_mtime;
	time_t		f_ctime;
	unsigned long	f_blksize;
	unsigned long	f_blocks;
	
        int             opened; /* is it open on the fileserver? */
	word            fileid;	/* What id to handle a file with? */
	word            attr;	/* Attribute fields, DOS value */

	unsigned short  access;	/* Access bits. */
        unsigned long   f_pos;	/* File position. (For readdir.) */
	unsigned char   name[SMB_MAXNAMELEN+1];
	int             len;	/* namelength */
};

#endif  /* __KERNEL__ */
#endif  /* _LINUX_SMB_H */

/*
 *  include/linux/nfs4.h
 *
 *  NFSv4 protocol definitions.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *  Andy Adamson   <andros@umich.edu>
 */

#ifndef _LINUX_NFS4_H
#define _LINUX_NFS4_H

#define NFS4_VERIFIER_SIZE	8
#define NFS4_FHSIZE		128
#define NFS4_MAXNAMLEN		NAME_MAX

#define NFS4_ACCESS_READ        0x0001
#define NFS4_ACCESS_LOOKUP      0x0002
#define NFS4_ACCESS_MODIFY      0x0004
#define NFS4_ACCESS_EXTEND      0x0008
#define NFS4_ACCESS_DELETE      0x0010
#define NFS4_ACCESS_EXECUTE     0x0020

#define NFS4_FH_PERISTENT		0x0000
#define NFS4_FH_NOEXPIRE_WITH_OPEN	0x0001
#define NFS4_FH_VOLATILE_ANY		0x0002
#define NFS4_FH_VOL_MIGRATION		0x0004
#define NFS4_FH_VOL_RENAME		0x0008

#define NFS4_OPEN_RESULT_CONFIRM 0x0002

#define NFS4_SHARE_ACCESS_READ	0x0001
#define NFS4_SHARE_ACCESS_WRITE	0x0002
#define NFS4_SHARE_ACCESS_BOTH	0x0003
#define NFS4_SHARE_DENY_READ	0x0001
#define NFS4_SHARE_DENY_WRITE	0x0002

#define NFS4_SET_TO_SERVER_TIME	0
#define NFS4_SET_TO_CLIENT_TIME	1

#define NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE 0
#define NFS4_ACE_ACCESS_DENIED_ACE_TYPE  1
#define NFS4_ACE_SYSTEM_AUDIT_ACE_TYPE   2
#define NFS4_ACE_SYSTEM_ALARM_ACE_TYPE   3

typedef struct { char data[NFS4_VERIFIER_SIZE]; } nfs4_verifier;
typedef struct { char data[16]; } nfs4_stateid;

enum nfs_opnum4 {
	OP_ACCESS = 3,
	OP_CLOSE = 4,
	OP_COMMIT = 5,
	OP_CREATE = 6,
	OP_DELEGPURGE = 7,
	OP_DELEGRETURN = 8,
	OP_GETATTR = 9,
	OP_GETFH = 10,
	OP_LINK = 11,
	OP_LOCK = 12,
	OP_LOCKT = 13,
	OP_LOCKU = 14,
	OP_LOOKUP = 15,
	OP_LOOKUPP = 16,
	OP_NVERIFY = 17,
	OP_OPEN = 18,
	OP_OPENATTR = 19,
	OP_OPEN_CONFIRM = 20,
	OP_OPEN_DOWNGRADE = 21,
	OP_PUTFH = 22,
	OP_PUTPUBFH = 23,
	OP_PUTROOTFH = 24,
	OP_READ = 25,
	OP_READDIR = 26,
	OP_READLINK = 27,
	OP_REMOVE = 28,
	OP_RENAME = 29,
	OP_RENEW = 30,
	OP_RESTOREFH = 31,
	OP_SAVEFH = 32,
	OP_SECINFO = 33,
	OP_SETATTR = 34,
	OP_SETCLIENTID = 35,
	OP_SETCLIENTID_CONFIRM = 36,
	OP_VERIFY = 37,
	OP_WRITE = 38,
};

/*
 * Note: NF4BAD is not actually part of the protocol; it is just used
 * internally by nfsd.
 */
enum nfs_ftype4 {
	NF4BAD		= 0,
        NF4REG          = 1,    /* Regular File */
        NF4DIR          = 2,    /* Directory */
        NF4BLK          = 3,    /* Special File - block device */
        NF4CHR          = 4,    /* Special File - character device */
        NF4LNK          = 5,    /* Symbolic Link */
        NF4SOCK         = 6,    /* Special File - socket */
        NF4FIFO         = 7,    /* Special File - fifo */
        NF4ATTRDIR      = 8,    /* Attribute Directory */
        NF4NAMEDATTR    = 9     /* Named Attribute */
};

enum open_claim_type4 {
	NFS4_OPEN_CLAIM_NULL = 0,
	NFS4_OPEN_CLAIM_PREVIOUS = 1,
	NFS4_OPEN_CLAIM_DELEGATE_CUR = 2,
	NFS4_OPEN_CLAIM_DELEGATE_PREV = 3
};

enum opentype4 {
	NFS4_OPEN_NOCREATE = 0,
	NFS4_OPEN_CREATE = 1
};

enum createmode4 {
	NFS4_CREATE_UNCHECKED = 0,
	NFS4_CREATE_GUARDED = 1,
	NFS4_CREATE_EXCLUSIVE = 2
};

enum limit_by4 {
	NFS4_LIMIT_SIZE = 1,
	NFS4_LIMIT_BLOCKS = 2
};

enum open_delegation_type4 {
	NFS4_OPEN_DELEGATE_NONE = 0,
	NFS4_OPEN_DELEGATE_READ = 1,
	NFS4_OPEN_DELEGATE_WRITE = 2
};

enum lock_type4 {
	NFS4_UNLOCK_LT = 0,
	NFS4_READ_LT = 1,
	NFS4_WRITE_LT = 2,
	NFS4_READW_LT = 3,
	NFS4_WRITEW_LT = 4
};


/* Mandatory Attributes */
#define FATTR4_WORD0_SUPPORTED_ATTRS    (1)
#define FATTR4_WORD0_TYPE               (1 << 1)
#define FATTR4_WORD0_FH_EXPIRE_TYPE     (1 << 2)
#define FATTR4_WORD0_CHANGE             (1 << 3)
#define FATTR4_WORD0_SIZE               (1 << 4)
#define FATTR4_WORD0_LINK_SUPPORT       (1 << 5)
#define FATTR4_WORD0_SYMLINK_SUPPORT    (1 << 6)
#define FATTR4_WORD0_NAMED_ATTR         (1 << 7)
#define FATTR4_WORD0_FSID               (1 << 8)
#define FATTR4_WORD0_UNIQUE_HANDLES     (1 << 9)
#define FATTR4_WORD0_LEASE_TIME         (1 << 10)
#define FATTR4_WORD0_RDATTR_ERROR       (1 << 11)

/* Recommended Attributes */
#define FATTR4_WORD0_ACL                (1 << 12)
#define FATTR4_WORD0_ACLSUPPORT         (1 << 13)
#define FATTR4_WORD0_ARCHIVE            (1 << 14)
#define FATTR4_WORD0_CANSETTIME         (1 << 15)
#define FATTR4_WORD0_CASE_INSENSITIVE   (1 << 16)
#define FATTR4_WORD0_CASE_PRESERVING    (1 << 17)
#define FATTR4_WORD0_CHOWN_RESTRICTED   (1 << 18)
#define FATTR4_WORD0_FILEHANDLE         (1 << 19)
#define FATTR4_WORD0_FILEID             (1 << 20)
#define FATTR4_WORD0_FILES_AVAIL        (1 << 21)
#define FATTR4_WORD0_FILES_FREE         (1 << 22)
#define FATTR4_WORD0_FILES_TOTAL        (1 << 23)
#define FATTR4_WORD0_FS_LOCATIONS       (1 << 24)
#define FATTR4_WORD0_HIDDEN             (1 << 25)
#define FATTR4_WORD0_HOMOGENEOUS        (1 << 26)
#define FATTR4_WORD0_MAXFILESIZE        (1 << 27)
#define FATTR4_WORD0_MAXLINK            (1 << 28)
#define FATTR4_WORD0_MAXNAME            (1 << 29)
#define FATTR4_WORD0_MAXREAD            (1 << 30)
#define FATTR4_WORD0_MAXWRITE           (1 << 31)
#define FATTR4_WORD1_MIMETYPE           (1)
#define FATTR4_WORD1_MODE               (1 << 1)
#define FATTR4_WORD1_NO_TRUNC           (1 << 2)
#define FATTR4_WORD1_NUMLINKS           (1 << 3)
#define FATTR4_WORD1_OWNER              (1 << 4)
#define FATTR4_WORD1_OWNER_GROUP        (1 << 5)
#define FATTR4_WORD1_QUOTA_HARD         (1 << 6)
#define FATTR4_WORD1_QUOTA_SOFT         (1 << 7)
#define FATTR4_WORD1_QUOTA_USED         (1 << 8)
#define FATTR4_WORD1_RAWDEV             (1 << 9)
#define FATTR4_WORD1_SPACE_AVAIL        (1 << 10)
#define FATTR4_WORD1_SPACE_FREE         (1 << 11)
#define FATTR4_WORD1_SPACE_TOTAL        (1 << 12)
#define FATTR4_WORD1_SPACE_USED         (1 << 13)
#define FATTR4_WORD1_SYSTEM             (1 << 14)
#define FATTR4_WORD1_TIME_ACCESS        (1 << 15)
#define FATTR4_WORD1_TIME_ACCESS_SET    (1 << 16)
#define FATTR4_WORD1_TIME_BACKUP        (1 << 17)
#define FATTR4_WORD1_TIME_CREATE        (1 << 18)
#define FATTR4_WORD1_TIME_DELTA         (1 << 19)
#define FATTR4_WORD1_TIME_METADATA      (1 << 20)
#define FATTR4_WORD1_TIME_MODIFY        (1 << 21)
#define FATTR4_WORD1_TIME_MODIFY_SET    (1 << 22)

#define NFSPROC4_NULL 0
#define NFSPROC4_COMPOUND 1
#define NFS4_MINOR_VERSION 0
#define NFS4_DEBUG 1

#ifdef __KERNEL__

/* Index of predefined Linux client operations */

enum {
	NFSPROC4_CLNT_NULL = 0,		/* Unused */
	NFSPROC4_CLNT_COMPOUND,		/* Soon to be unused */
	NFSPROC4_CLNT_READ,
	NFSPROC4_CLNT_WRITE,
	NFSPROC4_CLNT_COMMIT,
	NFSPROC4_CLNT_OPEN,
	NFSPROC4_CLNT_OPEN_CONFIRM,
	NFSPROC4_CLNT_CLOSE,
	NFSPROC4_CLNT_SETATTR,
};

#endif
#endif

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */

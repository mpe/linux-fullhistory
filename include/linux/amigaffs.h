#ifndef AMIGAFFS_H
#define AMIGAFFS_H

#include <asm/byteorder.h>
#include <linux/types.h>

/* Ugly macros make the code more pretty. */

#define GET_END_PTR(st,p,sz)		 ((st *)((char *)(p)+((sz)-sizeof(st))))
#define AFFS_GET_HASHENTRY(data,hashkey) htonl(((struct dir_front *)data)->hashtable[hashkey])
#define AFFS_BLOCK(data,ino,blk)	 ((struct file_front *)data)->blocks[AFFS_I2HSIZE(ino)-1-blk]

#define FILE_END(p,i)	GET_END_PTR(struct file_end,p,AFFS_I2BSIZE(i))
#define ROOT_END(p,i)	GET_END_PTR(struct root_end,p,AFFS_I2BSIZE(i))
#define DIR_END(p,i)	GET_END_PTR(struct dir_end,p,AFFS_I2BSIZE(i))
#define LINK_END(p,i)	GET_END_PTR(struct hlink_end,p,AFFS_I2BSIZE(i))
#define ROOT_END_S(p,s)	GET_END_PTR(struct root_end,p,(s)->s_blocksize)

/* Only for easier debugging if need be */
#define affs_bread	bread
#define affs_brelse	brelse

#ifdef __LITTLE_ENDIAN
#define BO_EXBITS	0x18UL
#elif defined(__BIG_ENDIAN)
#define BO_EXBITS	0x00UL
#else
#error Endianness must be known for affs to work.
#endif

/* The following constants will be checked against the values read native */

#define FS_OFS		0x444F5300
#define FS_FFS		0x444F5301
#define FS_INTLOFS	0x444F5302
#define FS_INTLFFS	0x444F5303
#define FS_DCOFS	0x444F5304
#define FS_DCFFS	0x444F5305
#define MUFS_FS		0x6d754653   /* 'muFS' */
#define MUFS_OFS	0x6d754600   /* 'muF\0' */
#define MUFS_FFS	0x6d754601   /* 'muF\1' */
#define MUFS_INTLOFS	0x6d754602   /* 'muF\2' */
#define MUFS_INTLFFS	0x6d754603   /* 'muF\3' */
#define MUFS_DCOFS	0x6d754604   /* 'muF\4' */
#define MUFS_DCFFS	0x6d754605   /* 'muF\5' */

typedef __u32	ULONG;
typedef __u16	UWORD;
typedef __u8	UBYTE;

typedef __s32	LONG;
typedef __s16	WORD;
typedef __s8	BYTE;

struct DateStamp
{
  ULONG ds_Days;
  ULONG ds_Minute;
  ULONG ds_Tick;
};

#define T_SHORT		2
#define T_LIST		16
#define T_DATA		8

#define ST_LINKFILE	-4
#define ST_FILE		-3
#define ST_ROOT		1
#define ST_USERDIR	2
#define ST_SOFTLINK	3
#define ST_LINKDIR	4

struct root_front
{
  LONG primary_type;
  ULONG spare1[2];
  ULONG hash_size;
  ULONG spare2;
  ULONG checksum;
  ULONG hashtable[0];
};

struct root_end
{
  LONG bm_flag;
  ULONG bm_keys[25];
  ULONG bm_extend;
  struct DateStamp dir_altered;
  UBYTE disk_name[40];
  struct DateStamp disk_altered;
  struct DateStamp disk_made;
  ULONG spare1[3];
  LONG secondary_type;
};

struct dir_front
{
  LONG primary_type;
  ULONG own_key;
  ULONG spare1[3];
  ULONG checksum;
  ULONG hashtable[0];
};

struct dir_end
{
  ULONG spare1;
  UWORD owner_uid;
  UWORD owner_gid;
  ULONG protect;
  ULONG spare2;
  UBYTE comment[92];
  struct DateStamp created;
  UBYTE dir_name[32];
  ULONG spare3[2];
  ULONG link_chain;
  ULONG spare4[5];
  ULONG hash_chain;
  ULONG parent;
  ULONG spare5;
  LONG secondary_type;
};

struct file_front
{
  LONG primary_type;
  ULONG own_key;
  ULONG block_count;
  ULONG unknown1;
  ULONG first_data;
  ULONG checksum;
  ULONG blocks[0];
};

struct file_end
{
  ULONG spare1;
  UWORD owner_uid;
  UWORD owner_gid;
  ULONG protect;
  ULONG byte_size;
  UBYTE comment[92];
  struct DateStamp created;
  UBYTE file_name[32];
  ULONG spare2;
  ULONG original;	/* not really in file_end */
  ULONG link_chain;
  ULONG spare3[5];
  ULONG hash_chain;
  ULONG parent;
  ULONG extension;
  LONG secondary_type;
};

struct hlink_front
{
  LONG primary_type;
  ULONG own_key;
  ULONG spare1[3];
  ULONG checksum;
};

struct hlink_end
{
  ULONG spare1;
  UWORD owner_uid;
  UWORD owner_gid;
  ULONG protect;
  UBYTE comment[92];
  struct DateStamp created;
  UBYTE link_name[32];
  ULONG spare2;
  ULONG original;
  ULONG link_chain;
  ULONG spare3[5];
  ULONG hash_chain;
  ULONG parent;
  ULONG spare4;
  LONG secondary_type;
};

struct slink_front
{
  LONG primary_type;
  ULONG own_key;
  ULONG spare1[3];
  ULONG checksum;
  UBYTE	symname[288];	/* depends on block size */
};

/* Permission bits */

#define FIBF_OTR_READ		0x8000
#define FIBF_OTR_WRITE		0x4000
#define FIBF_OTR_EXECUTE	0x2000
#define FIBF_OTR_DELETE		0x1000
#define FIBF_GRP_READ		0x0800
#define FIBF_GRP_WRITE		0x0400
#define FIBF_GRP_EXECUTE	0x0200
#define FIBF_GRP_DELETE		0x0100

#define FIBF_SCRIPT		0x0040
#define FIBF_PURE		0x0020		/* no use under linux */
#define FIBF_ARCHIVE		0x0010		/* never set, always cleared on write */
#define FIBF_READ		0x0008		/* 0 means allowed */
#define FIBF_WRITE		0x0004		/* 0 means allowed */
#define FIBF_EXECUTE		0x0002		/* 0 means allowed, ignored under linux */
#define FIBF_DELETE		0x0001		/* 0 means allowed */

#define FIBF_OWNER		0x000F		/* Bits pertaining to owner */

#define AFFS_UMAYWRITE(prot)	(((prot) & (FIBF_WRITE|FIBF_DELETE)) == (FIBF_WRITE|FIBF_DELETE))
#define AFFS_UMAYREAD(prot)	((prot) & FIBF_READ)
#define AFFS_UMAYEXECUTE(prot)	(((prot) & (FIBF_SCRIPT|FIBF_READ)) == (FIBF_SCRIPT|FIBF_READ))
#define AFFS_GMAYWRITE(prot)	(((prot)&(FIBF_GRP_WRITE|FIBF_GRP_DELETE))==\
							(FIBF_GRP_WRITE|FIBF_GRP_DELETE))
#define AFFS_GMAYREAD(prot)	((prot) & FIBF_GRP_READ)
#define AFFS_GMAYEXECUTE(prot)	(((prot)&(FIBF_SCRIPT|FIBF_GRP_READ))==(FIBF_SCRIPT|FIBF_GRP_READ))
#define AFFS_OMAYWRITE(prot)	(((prot)&(FIBF_OTR_WRITE|FIBF_OTR_DELETE))==\
							(FIBF_OTR_WRITE|FIBF_OTR_DELETE))
#define AFFS_OMAYREAD(prot)	((prot) & FIBF_OTR_READ)
#define AFFS_OMAYEXECUTE(prot)	(((prot)&(FIBF_SCRIPT|FIBF_OTR_READ))==(FIBF_SCRIPT|FIBF_OTR_READ))

#endif

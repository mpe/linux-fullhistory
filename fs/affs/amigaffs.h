#ifndef AMIGAFFS_H
#define AMIGAFFS_H

/* Ugly macros to make the code pretty. */

#define GET_END_PTR(st,p,sz) ((st *)((char *)(p)+((sz)-sizeof(st))))

#define MIDBLOCK_LONGS(sz) ((sz - sizeof (struct dir_front) \
			     - sizeof (struct dir_end)) / 4)

static __inline__ unsigned long
swap_long (unsigned long x)
{
#ifdef __i386__   /* bad check... should be endian check */
  unsigned char *px;

  px = (unsigned char *) &x;
  return (  (px[3] <<  0)
	  | (px[2] <<  8)
	  | (px[1] << 16)
	  | (px[0] << 24));
#else
  return x;
#endif
}

typedef unsigned long	ULONG;
typedef unsigned short	UWORD;
typedef unsigned char	UBYTE;

typedef long		LONG;
typedef short		WORD;
typedef char		BYTE;

struct DateStamp
{
  ULONG ds_Days;
  ULONG ds_Minute;
  ULONG ds_Tick;
};

#define T_SHORT 	2
#define T_LIST		16
#define T_DATA		8

#define ST_FILE 	-3
#define ST_USERDIR	2
#define ST_ROOT 	1
#define ST_SOFTLINK	3
#define ST_LINKFILE	-4
#define ST_LINKDIR	4

#define PROT_ARCHIVE        (1<<4)
#define PROT_READ           (1<<3)
#define PROT_WRITE          (1<<2)
#define PROT_EXECUTE        (1<<1)
#define PROT_DELETE         (1<<0)

#define PROT_OTR_READ       (1<<15)
#define PROT_OTR_WRITE      (1<<14)
#define PROT_OTR_EXECUTE    (1<<13)
#define PROT_OTR_DELETE     (1<<12)

#define PROT_GRP_READ       (1<<11)
#define PROT_GRP_WRITE      (1<<10)
#define PROT_GRP_EXECUTE    (1<<9)
#define PROT_GRP_DELETE     (1<<8)

struct ffs_root_front
{
  LONG primary_type;
  ULONG spare1[2];
  ULONG hash_size;
  ULONG spare2;
  ULONG checksum;
};

struct ffs_root_end
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
  ULONG hash_table[0];
};

struct dir_end
{
  ULONG spare1;
  UWORD uid;
  UWORD gid;
  ULONG protect;
  ULONG spare2;
  UBYTE comment[92];
  struct DateStamp created;
  UBYTE dir_name[64];
  ULONG hash_chain;
  ULONG parent;
  ULONG spare3;
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
  UWORD uid;
  UWORD gid;
  ULONG protect;
  ULONG byte_size;
  UBYTE comment[92];
  struct DateStamp created;
  UBYTE file_name[64];
  ULONG hash_chain;
  ULONG parent;
  ULONG extension;
  LONG secondary_type;
};

struct data_front
{
  LONG primary_type;
  ULONG header;
  ULONG seq_num;
  ULONG data_size;
  ULONG next_data;
  ULONG checksum;
};

struct symlink_front
{
  LONG primary_type;
  ULONG own_key;
  LONG unused[3];
  ULONG checksum;
  UBYTE symname[0];
};

struct symlink_end
{
  ULONG spare1;
  UWORD uid;
  UWORD gid;
  ULONG protect;
  ULONG spare2;
  UBYTE comment[92];
  struct DateStamp created;
  UBYTE link_name[64];
  ULONG hash_chain;
  ULONG parent;
  ULONG spare3;
  LONG secondary_type;
};

struct hardlink_front
{
  LONG primary_type;
  ULONG own_key;
  LONG unused[3];
  ULONG checksum;
};

struct hardlink_end
{
  ULONG spare1;
  UWORD uid;
  UWORD gid;
  ULONG protect;
  ULONG spare2;
  UBYTE comment[92];
  struct DateStamp created;
  UBYTE link_name[32];
  ULONG spare3;
  ULONG original;
  ULONG link_chain;
  ULONG spare4[5];
  ULONG hash_chain;
  ULONG parent;
  ULONG spare5;
  LONG secondary_type;
};

#endif

/*
 * udf_fs_sb.h
 * 
 * This include file is for the Linux kernel/module.
 *
 * CONTACTS
 *	E-mail regarding any portion of the Linux UDF file system should be
 *	directed to the development team mailing list (run by majordomo):
 *		linux_udf@hootie.lvld.hp.com
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 */

#if !defined(_LINUX_UDF_FS_SB_H)
#define _LINUX_UDF_FS_SB_H

#pragma pack(1)

#define UDF_MAX_BLOCK_LOADED	8

#define UDF_TYPE1_MAP15			0x1511U
#define UDF_VIRTUAL_MAP15		0x1512U
#define UDF_VIRTUAL_MAP20		0x2012U
#define UDF_SPARABLE_MAP15		0x1522U

struct udf_sparing_data
{
	__u32	  s_spar_loc;
	__u16	  s_spar_plen;
};

struct udf_virtual_data
{
	__u32	  s_num_entries;
	__u16	  s_start_offset;
};

struct udf_part_map
{
	__u32	s_uspace_bitmap;
	__u32	s_partition_root;
	__u32	s_partition_len;
	__u16	s_partition_type;
	__u16	s_partition_num;
	union
	{
		struct udf_sparing_data s_sparing;
		struct udf_virtual_data s_virtual;
	} s_type_specific;
	__u16	s_volumeseqnum;
};

#pragma pack()

struct udf_sb_info
{
	struct udf_part_map *s_partmaps;
	__u8  s_volident[32];

	/* Overall info */
	__u16 s_partitions;
	__u16 s_partition;

	/* Sector headers */
	__u32 s_session;
	__u32 s_anchor[4];
	__u32 s_lastblock;

	struct buffer_head *s_lvidbh;

	lb_addr s_location;

	__u16 s_loaded_block_bitmaps;
	__u32 s_block_bitmap_number[UDF_MAX_BLOCK_LOADED];
	struct buffer_head *s_block_bitmap[UDF_MAX_BLOCK_LOADED];

	/* Default permissions */
	mode_t s_umask;
	gid_t s_gid;
	uid_t s_uid;

	/* Root Info */
	time_t s_recordtime;

	/* Fileset Info */
	__u16 s_serialnum;

	/* Character Mapping Info */
	struct nls_table *s_nls_iocharset;
	__u8 s_utf8;

	/* Miscellaneous flags */
	__u32 s_flags;

	/* VAT inode */
	struct inode    *s_vat;

#if LINUX_VERSION_CODE < 0x020206
	int s_rename_lock;
	struct wait_queue * s_rename_wait;
#endif
};

#endif /* !defined(_LINUX_UDF_FS_SB_H) */

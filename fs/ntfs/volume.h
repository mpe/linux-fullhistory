/*
 * volume.h - Defines for volume structures in NTFS Linux kernel driver. Part
 *	      of the Linux-NTFS project.
 *
 * Copyright (c) 2001,2002 Anton Altaparmakov.
 * Copyright (C) 2002 Richard Russon.
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be 
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty 
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS 
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_NTFS_VOLUME_H
#define _LINUX_NTFS_VOLUME_H

#include "types.h"

/*
 * Defined bits for the flags field in the ntfs_volume structure.
 */
typedef enum {
	NV_ShowSystemFiles,	/* 1: Return system files in ntfs_readdir(). */
	NV_CaseSensitive,	/* 1: Treat file names as case sensitive and
				      create filenames in the POSIX namespace.
				      Otherwise be case insensitive and create
				      file names in WIN32 namespace. */
} ntfs_volume_flags;

#define NVolShowSystemFiles(n_vol)	test_bit(NV_ShowSystemFiles,	\
							&(n_vol)->flags)
#define NVolSetShowSystemFiles(n_vol)	set_bit(NV_ShowSystemFiles,	\
							&(n_vol)->flags)
#define NVolClearShowSystemFiles(n_vol)	clear_bit(NV_ShowSystemFiles,	\
							&(n_vol)->flags)

#define NVolCaseSensitive(n_vol)	test_bit(NV_CaseSensitive,	\
							&(n_vol)->flags)
#define NVolSetCaseSensitive(n_vol)	set_bit(NV_CaseSensitive,	\
							&(n_vol)->flags)
#define NVolClearCaseSensitive(n_vol)	clear_bit(NV_CaseSensitive,	\
							&(n_vol)->flags)

/*
 * The NTFS in memory super block structure.
 */
typedef struct {
	/*
	 * FIXME: Reorder to have commonly used together element within the
	 * same cache line, aiming at a cache line size of 32 bytes. Aim for
	 * 64 bytes for less commonly used together elements. Put most commonly
	 * used elements to front of structure. Obviously do this only when the
	 * structure has stabilized... (AIA)
	 */
	/* Device specifics. */
	struct super_block *sb;		/* Pointer back to the super_block,
					   so we don't have to get the offset
					   every time. */
	LCN nr_blocks;			/* Number of NTFS_BLOCK_SIZE bytes
					   sized blocks on the device. */
	/* Configuration provided by user at mount time. */
	unsigned long flags;		/* Miscellaneous flags, see above. */
	uid_t uid;			/* uid that files will be mounted as. */
	gid_t gid;			/* gid that files will be mounted as. */
	mode_t fmask;			/* The mask for file permissions. */
	mode_t dmask;			/* The mask for directory
					   permissions. */
	u8 mft_zone_multiplier;		/* Initial mft zone multiplier. */
	u8 on_errors;			/* What to do on file system errors. */
	/* NTFS bootsector provided information. */
	u16 sector_size;		/* in bytes */
	u8 sector_size_bits;		/* log2(sector_size) */
	u32 cluster_size;		/* in bytes */
	u32 cluster_size_mask;		/* cluster_size - 1 */
	u8 cluster_size_bits;		/* log2(cluster_size) */
	u32 mft_record_size;		/* in bytes */
	u32 mft_record_size_mask;	/* mft_record_size - 1 */
	u8 mft_record_size_bits;	/* log2(mft_record_size) */
	u32 index_record_size;		/* in bytes */
	u32 index_record_size_mask;	/* index_record_size - 1 */
	u8 index_record_size_bits;	/* log2(index_record_size) */
	union {
		LCN nr_clusters;	/* Volume size in clusters. */
		LCN nr_lcn_bits;	/* Number of bits in lcn bitmap. */
	} SN(vcl);
	LCN mft_lcn;			/* Cluster location of mft data. */
	LCN mftmirr_lcn;		/* Cluster location of copy of mft. */
	u64 serial_no;			/* The volume serial number. */
	/* Mount specific NTFS information. */
	u32 upcase_len;			/* Number of entries in upcase[]. */
	uchar_t *upcase;		/* The upcase table. */
	LCN mft_zone_start;		/* First cluster of the mft zone. */
	LCN mft_zone_end;		/* First cluster beyond the mft zone. */
	struct inode *mft_ino;		/* The VFS inode of $MFT. */
	struct rw_semaphore mftbmp_lock; /* Lock for serializing accesses to the
					    mft record bitmap ($MFT/$BITMAP). */
	union {
		unsigned long nr_mft_records; /* Number of mft records. */
		unsigned long nr_mft_bits; /* Number of bits in mft bitmap. */
	} SN(vmm);
	struct address_space mftbmp_mapping; /* Page cache for $MFT/$BITMAP. */
	run_list mftbmp_rl;		/* Run list for $MFT/$BITMAP. */
	s64 mftbmp_size;		/* Data size of $MFT/$BITMAP. */
	s64 mftbmp_initialized_size;	/* Initialized size of $MFT/$BITMAP. */
	s64 mftbmp_allocated_size;	/* Allocated size of $MFT/$BITMAP. */
	struct inode *mftmirr_ino;	/* The VFS inode of $MFTMirr. */
	struct inode *lcnbmp_ino;	/* The VFS inode of $Bitmap. */
	struct rw_semaphore lcnbmp_lock; /* Lock for serializing accesses to the
					    cluster bitmap ($Bitmap/$DATA). */
	struct inode *vol_ino;		/* The VFS inode of $Volume. */
	unsigned long vol_flags;	/* Volume flags (VOLUME_*). */
	u8 major_ver;			/* Ntfs major version of volume. */
	u8 minor_ver;			/* Ntfs minor version of volume. */
	struct inode *root_ino;		/* The VFS inode of the root
					   directory. */
	struct inode *secure_ino;	/* The VFS inode of $Secure (NTFS3.0+
					   only, otherwise NULL). */
	struct nls_table *nls_map;
} ntfs_volume;

#define _VCL(X)  SC(vcl,X)
#define _VMM(X)  SC(vmm,X)

#endif /* _LINUX_NTFS_VOLUME_H */


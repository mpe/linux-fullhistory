/*
 *  linux/include/linux/ufs_fs_sb.h
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * $Id: ufs_fs_sb.h,v 1.6 1996/06/01 15:31:08 ecd Exp $
 *
 */

#ifndef __LINUX_UFS_FS_SB_H
#define __LINUX_UFS_FS_SB_H

#include <linux/ufs_fs.h>

struct ufs_sb_info {
	struct ufs_superblock * s_raw_sb;
	__u32	s_flags; /* internal flags for UFS code */
	__u32	s_ncg;	 /* used in ufs_read_inode */
	__u32	s_ipg;	 /* used in ufs_read_inode */
	__u32	s_fpg;
	__u32	s_fsize;
	__u32	s_fshift;
	__u32	s_fmask;
	__u32	s_bsize;
	__u32	s_bmask;
	__u32	s_bshift;
	__u32	s_iblkno;
	__u32	s_dblkno;
	__u32	s_cgoffset;
	__u32	s_cgmask;
	__u32	s_inopb;
	__u32	s_lshift;
	__u32	s_lmask;
	__u32	s_fsfrag;
};

#endif /* __LINUX_UFS_FS_SB_H */

/*
 *  linux/include/linux/ufs_fs_sb.h
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * $Id: ufs_fs_sb.h,v 1.1 1996/04/21 14:45:15 davem Exp $
 *
 */

#ifndef __LINUX_UFS_FS_SB_H
#define __LINUX_UFS_FS_SB_H


struct ufs_sb_info {
	struct ufs_superblock * s_raw_sb;
	__u32	s_flags; /* internal flags for UFS code */
	__u32	s_ncg;	 /* used in ufs_read_inode */
	__u32	s_ipg;	 /* used in ufs_read_inode */
	__u32	s_fpg;
	__u32	s_fsize;
	__u32	s_bsize;
	__u32	s_iblkno;
	__u32	s_dblkno;
	__u32	s_cgoffset;
	__u32	s_cgmask;
	__u32	s_inopb;
	__u32	s_fsfrag;
};

#endif /* __LINUX_UFS_FS_SB_H */

/*
 * Local Variables: ***
 * c-indent-level: 8 ***
 * c-continued-statement-offset: 8 ***
 * c-brace-offset: -8 ***
 * c-argdecl-indent: 0 ***
 * c-label-offset: -8 ***
 * End: ***
 */

/*
 *  linux/include/linux/ufs_fs_i.h
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * $Id: ufs_fs_i.h,v 1.2 1996/05/03 04:02:25 davem Exp $
 *
 */

#ifndef _LINUX_UFS_FS_I_H
#define _LINUX_UFS_FS_I_H

struct ufs_inode_info {
	__u32	i_data[15];
	__u64	i_size;
	__u32	i_flags;
	__u32	i_gen;
	__u32	i_shadow;
	__u32	i_uid;
	__u32	i_gid;
	__u32	i_oeftflag;
};

#endif /* _LINUX_UFS_FS_I_H */

/*
 *  linux/include/linux/ufs_fs_i.h
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * $Id: ufs_fs_i.h,v 1.1 1996/04/21 14:45:13 davem Exp $
 *
 */

#ifndef _LINUX_UFS_FS_I_H
#define _LINUX_UFS_FS_I_H

#include <linux/ufs_fs.h>

struct ufs_inode_info {
	__u64	ui_size;
	__u32	ui_flags;
	__u32	ui_gen;
	__u32	ui_shadow;
	__u32	ui_uid;
	__u32	ui_gid;
	__u32	ui_oeftflag;
	__u32	ui_db[UFS_NDADDR];
	__u32	ui_ib[UFS_NINDIR];
};

#endif /* _LINUX_UFS_FS_I_H */

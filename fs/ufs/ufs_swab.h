/*
 *  linux/fs/ufs/ufs_swab.h
 *
 * Copyright (C) 1997 
 * Francois-Rene Rideau <rideau@ens.fr>
 *
 */

#ifndef _UFS_SWAB_H
#define _UFS_SWAB_H


/*
 * Notes:
 * (1) HERE WE ASSUME EITHER BIG OR LITTLE ENDIAN UFSes
 *    in case there are ufs implementations that have strange bytesexes,
 *    you'll need to modify code here as well as in ufs_super.c and ufs_fs.h
 *    to support them.
 * (2) for a read/write ufs driver, we should distinguish
 *    between byteswapping for read or write accesses!
 */

#include <linux/ufs_fs.h>
#include <asm/byteorder.h>

/*
 * These are only valid inside ufs routines,
 * after bytesex has been initialized to sb->u.ufs_sb.s_flags&UFS_BYTESEX
 */
#define SWAB16(x) ufs_swab16(bytesex,x)
#define SWAB32(x) ufs_swab32(bytesex,x)
#define SWAB64(x) ufs_swab64(bytesex,x)

extern __inline__ __const__ __u16 ufs_swab16(__u32 bytesex, __u16 x) {
	if (bytesex == UFS_LITTLE_ENDIAN) {
		return le16_to_cpu(x);
	} else {
		return be16_to_cpu(x);
	}
}
extern __inline__ __const__ __u32 ufs_swab32(__u32 bytesex, __u32 x) {
	if (bytesex == UFS_LITTLE_ENDIAN) {
		return le32_to_cpu(x);
	} else {
		return be32_to_cpu(x);
	}
}
extern __inline__ __const__ __u64 ufs_swab64(__u32 bytesex, __u64 x) {
	if (bytesex == UFS_LITTLE_ENDIAN) {
		return le64_to_cpu(x);
	} else {
		return be64_to_cpu(x);
	}
}

extern void ufs_superblock_le_to_cpus(struct ufs_superblock * usb);
extern void ufs_superblock_be_to_cpus(struct ufs_superblock * usb);

#endif /* _UFS_SWAB_H */

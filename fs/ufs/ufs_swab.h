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
 *    naming should then be UFS16_TO_CPU and suches.
 *
 * 4.4BSD (FreeBSD) support added on February 1st 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk> partially based
 * on code by Martin von Loewis <martin@mira.isdn.cs.tu-berlin.de>.
 */

#include <linux/ufs_fs.h>
#include <asm/byteorder.h>

/*
 * These are only valid inside ufs routines, after a variable named flags
 * has been made visible in current scope and properly initialized:
	__u32 flags = sb->u.ufs_sb.s_flags ;
 */
#define SWAB16(x) ufs_swab16(flags,x)
#define SWAB32(x) ufs_swab32(flags,x)
#define SWAB64(x) ufs_swab64(flags,x)

extern __inline__ __const__ __u16 ufs_swab16(__u32 flags, __u16 x) {
	if ((flags&UFS_BYTESEX) == UFS_LITTLE_ENDIAN) {
		return le16_to_cpu(x);
	} else {
		return be16_to_cpu(x);
	}
}
extern __inline__ __const__ __u32 ufs_swab32(__u32 flags, __u32 x) {
	if ((flags&UFS_BYTESEX) == UFS_LITTLE_ENDIAN) {
		return le32_to_cpu(x);
	} else {
		return be32_to_cpu(x);
	}
}
extern __inline__ __const__ __u64 ufs_swab64(__u32 flags, __u64 x) {
	if ((flags&UFS_BYTESEX) == UFS_LITTLE_ENDIAN) {
		return le64_to_cpu(x);
	} else {
		return be64_to_cpu(x);
	}
}


/*
 * These are for in-core superblock normalization.
 * It might or not be a bad idea once we go to a read/write driver,
 * as all critical info should be copied to the sb info structure anyway.
 * So better replace them with a static inline function
 * ufs_superblock_to_sb_info() in ufs_super.c
 */
extern void ufs_superblock_le_to_cpus(struct ufs_superblock * usb);
extern void ufs_superblock_be_to_cpus(struct ufs_superblock * usb);


/*
 * These also implicitly depend on variable flags...
 * NAMLEN(foo) is already normalized to local format, so don't SWAB16() it!
 */

#define NAMLEN(direct) ufs_namlen(flags,direct)
extern __inline__ __u16 ufs_namlen(__u32 flags, struct ufs_direct * direct) {
	if ( (flags&UFS_DE_MASK) == UFS_DE_OLD) {
        	return SWAB16(direct->d_u.d_namlen);
	} else /* UFS_DE_44BSD */ {
        	return direct->d_u.d_44.d_namlen;
	}
}

/* Here is how the uid is computed:
   if the file system is 4.2BSD, get it from oldids.
   if it has sun extension and oldids is USEEFT, get it from ui_sun.
   if it is 4.4 or Hurd, get it from ui_44 (which is the same as ui_hurd).
   depends on implicit variable flags being initialized from
	__u32 flags = sb->u.ufs_sb.s_flags;
*/
#define UFS_UID(ino)	ufs_uid(flags,ino)
#define UFS_GID(ino)	ufs_gid(flags,ino)

extern __inline__ __u32 ufs_uid(__u32 flags,struct ufs_inode * ino) {
	switch(flags&UFS_UID_MASK) {
		case UFS_UID_EFT:
			return SWAB32(ino->ui_u3.ui_sun.ui_uid) ;
		case UFS_UID_44BSD:
                  	return SWAB32(ino->ui_u3.ui_44.ui_uid) ;
		case UFS_UID_OLD:
		default:
                	return SWAB16(ino->ui_u1.oldids.suid) ;
	}
}
extern __inline__ __u32 ufs_gid(__u32 flags,struct ufs_inode * ino) {
	switch(flags&UFS_UID_MASK) {
		case UFS_UID_EFT:
			return SWAB32(ino->ui_u3.ui_sun.ui_gid) ;
		case UFS_UID_44BSD:
                  	return SWAB32(ino->ui_u3.ui_44.ui_gid) ;
		case UFS_UID_OLD:
		default:
                	return SWAB16(ino->ui_u1.oldids.sgid) ;
	}
}

#endif /* _UFS_SWAB_H */

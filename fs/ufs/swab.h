/*
 *  linux/fs/ufs/ufs_swab.h
 *
 * Copyright (C) 1997 Francois-Rene Rideau <rideau@ens.fr>
 * Copyright (C) 1998 Jakub Jelinek <jj@ultra.linux.cz>
 */

#ifndef _UFS_SWAB_H
#define _UFS_SWAB_H

/*
 * Notes:
 *    HERE WE ASSUME EITHER BIG OR LITTLE ENDIAN UFSes
 *    in case there are ufs implementations that have strange bytesexes,
 *    you'll need to modify code here as well as in ufs_super.c and ufs_fs.h
 *    to support them.
 */

#include <linux/ufs_fs.h>
#include <asm/byteorder.h>

/*
 * These are only valid inside ufs routines,
 * after swab has been initialized to sb->u.ufs_sb.s_swab
 */
#define SWAB16(x) ufs_swab16(swab,x)
#define SWAB32(x) ufs_swab32(swab,x)
#define SWAB64(x) ufs_swab64(swab,x)

/*
 * We often use swabing, when we want to increment/decrement some value, so these
 * macros might become handy and increase readability. (Daniel)
 */
#define INC_SWAB16(x)	x=ufs_swab16_add(swab,x,1)
#define INC_SWAB32(x)	x=ufs_swab32_add(swab,x,1)
#define INC_SWAB64(x)	x=ufs_swab64_add(swab,x,1)
#define DEC_SWAB16(x)	x=ufs_swab16_add(swab,x,-1)
#define DEC_SWAB32(x)	x=ufs_swab32_add(swab,x,-1)
#define DEC_SWAB64(x)	x=ufs_swab64_add(swab,x,-1)
#define ADD_SWAB16(x,y)	x=ufs_swab16_add(swab,x,y)
#define ADD_SWAB32(x,y)	x=ufs_swab32_add(swab,x,y)
#define ADD_SWAB64(x,y)	x=ufs_swab64_add(swab,x,y)
#define SUB_SWAB16(x,y)	x=ufs_swab16_add(swab,x,-(y))
#define SUB_SWAB32(x,y)	x=ufs_swab32_add(swab,x,-(y))
#define SUB_SWAB64(x,y)	x=ufs_swab64_add(swab,x,-(y))

#ifndef __PDP_ENDIAN
extern __inline__ __const__ __u16 ufs_swab16(unsigned swab, __u16 x) {
	if (swab)
		return swab16(x);
	else
		return x;
}
extern __inline__ __const__ __u32 ufs_swab32(unsigned swab, __u32 x) {
	if (swab)
		return swab32(x);
	else
		return x;
}
extern __inline__ __const__ __u64 ufs_swab64(unsigned swab, __u64 x) {
	if (swab)
		return swab64(x);
	else
		return x;
}
extern __inline__ __const__ __u16 ufs_swab16_add(unsigned swab, __u16 x, __u16 y) {
	if (swab)
		return swab16(swab16(x)+y);
	else
		return x + y;
}
extern __inline__ __const__ __u32 ufs_swab32_add(unsigned swab, __u32 x, __u32 y) {
	if (swab)
		return swab32(swab32(x)+y);
	else
		return x + y;
}
extern __inline__ __const__ __u64 ufs_swab64_add(unsigned swab, __u64 x, __u64 y) {
	if (swab)
		return swab64(swab64(x)+y);
	else
		return x + y;
}
#else /* __PDP_ENDIAN */
extern __inline__ __const__ __u16 ufs_swab16(unsigned swab, __u16 x) {
	if (swab & UFS_LITTLE_ENDIAN)
		return le16_to_cpu(x);
	else
		return be16_to_cpu(x);
}
extern __inline__ __const__ __u32 ufs_swab32(unsigned swab, __u32 x) {
	if (swab & UFS_LITTLE_ENDIAN)
		return le32_to_cpu(x);
	else
		return be32_to_cpu(x);
}
extern __inline__ __const__ __u64 ufs_swab64(unsigned swab, __u64 x) {
	if (swab & UFS_LITTLE_ENDIAN)
		return le64_to_cpu(x);
	else
		return be64_to_cpu(x);
}
extern __inline__ __const__ __u16 ufs_swab16_add(unsigned swab, __u16 x, __u16 y) {
	return ufs_swab16(swab, ufs_swab16(swab, x) + y);
}
extern __inline__ __const__ __u32 ufs_swab32_add(unsigned swab, __u32 x, __u32 y) {
	return ufs_swab32(swab, ufs_swab32(swab, x) + y);
}
extern __inline__ __const__ __u64 ufs_swab64_add(unsigned swab, __u64 x, __u64 y) {
	return ufs_swab64(swab, ufs_swab64(swab, x) + y);
}
#endif /* __PDP_ENDIAN */

#endif /* _UFS_SWAB_H */

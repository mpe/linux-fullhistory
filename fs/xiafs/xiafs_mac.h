/*
 *  linux/fs/xiafs/xiafs_mac.h
 *
 *  Copyright (C) Q. Frank Xia, 1993.
 */

extern char internal_error_message[];
#define INTERN_ERR		internal_error_message, __FILE__, __LINE__
#define WHERE_ERR		__FILE__, __LINE__

#define XIAFS_ZSHIFT(sp)		((sp)->u.xiafs_sb.s_zone_shift)
#define XIAFS_ZSIZE(sp)		(BLOCK_SIZE << XIAFS_ZSHIFT(sp))
#define XIAFS_ZSIZE_BITS(sp)	(BLOCK_SIZE_BITS + XIAFS_ZSHIFT(sp))
#define XIAFS_ADDRS_PER_Z(sp)   	(BLOCK_SIZE >> (2 - XIAFS_ZSHIFT(sp)))
#define XIAFS_ADDRS_PER_Z_BITS(sp) 	(BLOCK_SIZE_BITS - 2 + XIAFS_ZSHIFT(sp))
#define XIAFS_BITS_PER_Z(sp)	(BLOCK_SIZE  << (3 + XIAFS_ZSHIFT(sp)))
#define XIAFS_BITS_PER_Z_BITS(sp)	(BLOCK_SIZE_BITS + 3 + XIAFS_ZSHIFT(sp))
#define XIAFS_INODES_PER_Z(sp)	(_XIAFS_INODES_PER_BLOCK << XIAFS_ZSHIFT(sp))



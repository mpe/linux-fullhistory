#ifndef __CRAMFS_H
#define __CRAMFS_H

#define CRAMFS_MAGIC		0x28cd3d45	/* some random number */
#define CRAMFS_SIGNATURE	"Compressed ROMFS"

/*
 * Reasonably terse representation of the inode
 * data.. When the mode of the inode indicates
 * a special device node, the "offset" bits will
 * encode i_rdev. In other cases, "offset" points
 * to the ROM image for the actual file data
 * (whether that data be directory or compressed
 * file data depends on the inode type again)
 */
struct cramfs_inode {
	u32 mode:16, uid:16;
	u32 size:24, gid:8;
	u32 namelen:6, offset:26;
};

/*
 * Superblock information at the beginning of the FS.
 */
struct cramfs_super {
	u32 magic;		/* 0x28cd3d45 - random number */
	u32 size;		/* > offset, < 2**26 */
	u32 flags;		/* 0 */
	u32 future;		/* 0 */
	u8 signature[16];	/* "Compressed ROMFS" */
	u8 fsid[16];		/* random number */
	u8 name[16];		/* user-defined name */
	struct cramfs_inode root;	/* Root inode data */
};

/* Uncompression interfaces to the underlying zlib */
int cramfs_uncompress_block(void *dst, int dstlen, void *src, int srclen);
int cramfs_uncompress_init(void);
int cramfs_uncompress_exit(void);

#endif

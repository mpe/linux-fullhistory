#ifndef _AFFS_FS_SB
#define _AFFS_FS_SB

/*
 * super-block data in memory
 *
 * Block numbers are for FFS-sized (normally 512 bytes) blocks.
 *
 */

/* Mount options */

struct affs_options {
	int offset;
	int size;
	int root;
	int nocase:1;			/* Ignore case in filenames. */
	int conv_links:1;		/* convert pathnames symlinks point to */
};

struct affs_sb_info {
	int s_partition_offset;         /* Offset to start in blocks. */
	int s_partition_size;		/* Partition size in blocks. */
	int s_root_block;		/* Absolute FFS root block number. */
	int s_block_size;		/* Block size in bytes. */
	char s_volume_name[42];
	struct affs_options s_options;
};

#endif








#ifndef _AFFS_FS_I
#define _AFFS_FS_I

#define EXT_CACHE_SIZE	12
#define MAX_PREALLOC	8	/* MUST be a power of 2 */

/*
 * affs fs inode data in memory
 */
struct affs_inode_info {
	__u32	i_protect;		/* unused attribute bits */
	__s32	i_parent;		/* parent ino */
	__s32	i_original;		/* if != 0, this is the key of the original */
	__s32	i_ext[EXT_CACHE_SIZE];	/* extension block numbers */
	__s32	i_data[MAX_PREALLOC];	/* preallocated blocks */
	int	i_lastblock;		/* last allocated block */
	short	i_max_ext;		/* last known extension block */
	short	i_pa_cnt;		/* number of preallocated blocks */
	short	i_pa_next;		/* Index of next block in i_data[] */
	short	i_pa_last;		/* Index of next free slot in i_data[] */
	short	i_zone;			/* write zone */
	unsigned char i_hlink;		/* This is a fake */
	unsigned char i_pad;
};

#endif

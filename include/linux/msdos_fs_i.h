#ifndef _MSDOS_FS_I
#define _MSDOS_FS_I

#include <linux/fs.h>

/*
 * MS-DOS file system inode data in memory
 */

struct msdos_inode_info {
	struct list_head cache_lru;
	int nr_caches;

	loff_t mmu_private;
	int i_start;	/* first cluster or 0 */
	int i_logstart;	/* logical first cluster */
	int i_attrs;	/* unused attribute bits */
	int i_ctime_ms;	/* unused change time in milliseconds */
	loff_t i_pos;	/* on-disk position of directory entry or 0 */
	struct hlist_node i_fat_hash;	/* hash by i_location */
	struct inode vfs_inode;
};

#endif

#ifndef _NFS_FS_I
#define _NFS_FS_I

#include <linux/nfs.h>
#include <linux/pipe_fs_i.h>

/*
 * nfs fs inode data in memory
 */
struct nfs_inode_info {
	struct pipe_inode_info pipeinfo;
	struct nfs_fh fhandle;
	/*
	 * read_cache_jiffies is when we started read-caching this inode,
	 * and read_cache_mtime is the mtime of the inode at that time.
	 *
	 * We need to invalidate the cache for this inode if
	 *
	 *	jiffies - read_cache_jiffies > 30*HZ
	 * AND
	 *	mtime != read_cache_mtime
	 */
	unsigned long read_cache_jiffies;
	unsigned long read_cache_mtime;
	/*
	 * This is to support the clandestine rename on unlink.
	 * Instead of the directory inode, we might as well keep its
	 * NFS FH, but that requires a kmalloc.
	 */
	struct inode *silly_rename_dir;
};

#endif

/*
 *	include/linux/bfs_fs_i.h
 *	Copyright (C) 1999 Tigran Aivazian <tigran@ocston.org>
 */

#ifndef _LINUX_BFS_FS_I
#define _LINUX_BFS_FS_I

/*
 * BFS file system in-core inode info
 */
struct bfs_inode_info {
	__u32	i_dsk_ino;	/* inode number from the disk, can be 0 */
	__u32	i_sblock;
	__u32	i_eblock;
};

#endif	/* _LINUX_BFS_FS_I */

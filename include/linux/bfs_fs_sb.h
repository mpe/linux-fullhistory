/*
 *	include/linux/bfs_fs_sb.h
 *	Copyright (C) 1999 Tigran Aivazian <tigran@ocston.org>
 */

#ifndef _LINUX_BFS_FS_SB
#define _LINUX_BFS_FS_SB

/*
 * BFS block map entry, an array of these is kept in bfs_sb_info.
 */
 struct bfs_bmap {
 	unsigned long start, end;
 };

/*
 * BFS file system in-core superblock info
 */
struct bfs_sb_info {
	unsigned long si_blocks;
	unsigned long si_freeb;
	unsigned long si_freei;
	unsigned long si_lf_ioff;
	unsigned long si_lf_sblk;
	unsigned long si_lf_eblk;
	unsigned long si_lasti;
	struct bfs_bmap * si_bmap;
	char * si_imap;
	struct buffer_head * si_sbh;		/* buffer header w/superblock */
	struct bfs_super_block * si_bfs_sb;	/* superblock in si_sbh->b_data */
};

#endif	/* _LINUX_BFS_FS_SB */

/*
 *	include/linux/bfs_fs_sb.h
 *	Copyright (C) 1999 Tigran Aivazian <tigran@ocston.org>
 */

#ifndef _LINUX_BFS_FS_SB
#define _LINUX_BFS_FS_SB

/*
 * BFS file system in-core superblock info
 */
struct bfs_sb_info {
	__u32 si_blocks;
	__u32 si_freeb;
	__u32 si_freei;
	__u32 si_lf_ioff;
	__u32 si_lf_sblk;
	__u32 si_lf_eblk;
	__u32 si_lasti;
	__u32 si_imap_len;
	__u8 *si_imap;
	struct buffer_head * si_sbh;		/* buffer header w/superblock */
	struct bfs_super_block * si_bfs_sb;	/* superblock in si_sbh->b_data */
};

#endif	/* _LINUX_BFS_FS_SB */

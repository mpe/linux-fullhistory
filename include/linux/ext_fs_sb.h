#ifndef _EXT_FS_SB
#define _EXT_FS_SB

/*
 * extended-fs super-block data in memory (same as minix: has to change)
 */
struct ext_sb_info {
			unsigned long s_ninodes;
			unsigned long s_nzones;
			unsigned long s_imap_blocks;
			unsigned long s_zmap_blocks;
			unsigned long s_firstdatazone;
			unsigned long s_log_zone_size;
			unsigned long s_max_size;
			struct buffer_head * s_imap[8];
			struct buffer_head * s_zmap[8];
};

#endif

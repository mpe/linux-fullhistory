#ifndef _EXT2_FS_I
#define _EXT2_FS_I

/*
 * second extended file system inode data in memory
 */
struct ext2_inode_info {
	unsigned long i_flags;
	unsigned short i_faddr;
	unsigned char i_frag;
	unsigned char i_fsize;
	unsigned long i_file_acl;
	unsigned long i_dir_acl;
	unsigned long i_dtime;
	unsigned long i_version;
	unsigned long i_block_group;
	unsigned long i_data[15];
};

#endif

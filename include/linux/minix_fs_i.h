#ifndef _MINIX_FS_I
#define _MINIX_FS_I

/*
 * minix fs inode data in memory
 */
struct minix_inode_info {
	__u16 i_data[16];
};

#endif

/*
 *  Name                         : qnx4_fs_i.h
 *  Author                       : Richard Frowijn
 *  Function                     : qnx4 inode definitions
 *  Version                      : 1.0
 *  Last modified                : 25-05-1998
 * 
 *  History                      : 23-03-1998 created
 * 
 */
#ifndef _QNX4_FS_I
#define _QNX4_FS_I

#include <linux/qnxtypes.h>

/*
 * qnx4 fs inode entry 
 */
struct qnx4_inode_info {
	char i_reserved[16];	/* 16 */
	qnx4_off_t i_size;		/*  4 */
	qnx4_xtnt_t i_first_xtnt;	/*  8 */
	long i_xblk;		/*  4 */
	time_t i_ftime;		/*  4 */
	time_t i_mtime;		/*  4 */
	time_t i_atime;		/*  4 */
	time_t i_ctime;		/*  4 */
	qnx4_nxtnt_t i_num_xtnts;	/*  2 */
	mode_t i_mode;		/*  2 */
	qnx4_muid_t i_uid;		/*  2 */
	qnx4_mgid_t i_gid;		/*  2 */
	qnx4_nlink_t i_nlink;	/*  2 */
	char i_zero[4];		/*  4 */
	qnx4_ftype_t i_type;	/*  1 */
	unsigned char i_status;	/*  1 */
};

#endif

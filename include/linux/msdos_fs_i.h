#ifndef _MSDOS_FS_I
#define _MSDOS_FS_I

#ifndef _LINUX_PIPE_FS_I_H
#include <linux/pipe_fs_i.h>
#endif

/*
 * MS-DOS file system inode data in memory
 */

struct msdos_inode_info {
	/*
		UMSDOS manage special file and fifo as normal empty
		msdos file. fifo inode processing conflict with msdos
		processing. So I insert the pipe_inode_info so the
		information does not overlap. This increases the size of
		the msdos_inode_info, but the clear winner here is
		the ext2_inode_info. So it does not change anything to
		the total size of a struct inode.

		I have not put it conditional. With the advent of loadable
		file system drivers, it would be very easy to compile
		a MsDOS FS driver unaware of UMSDOS and then later to
		load a (then incompatible) UMSDOS FS driver.
	*/
	struct pipe_inode_info reserved;
	int i_start;	/* first cluster or 0 */
	int i_attrs;	/* unused attribute bits */
	int i_busy;	/* file is either deleted but still open, or
			   inconsistent (mkdir) */
	struct inode *i_depend; /* pointer to inode that depends on the
				   current inode */
	struct inode *i_old;	/* pointer to the old inode this inode
				   depends on */
	int i_binary;	/* file contains non-text data */
};

#endif

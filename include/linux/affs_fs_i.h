#ifndef _AFFS_FS_I
#define _AFFS_FS_I

/*
 * affs fs inode data in memory
 */
struct affs_inode_info {
	int i_protect;	/* unused attribute bits */
	int i_parent;   /* parent ino */
};

#endif

/*
 *  smb_fs_i.h
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *
 */

#ifndef _LINUX_SMB_FS_I
#define _LINUX_SMB_FS_I

#ifdef __KERNEL__
#include <linux/smb.h>

enum smb_inode_state {
        SMB_INODE_VALID = 19,	/* Inode currently in use */
        SMB_INODE_LOOKED_UP,	/* directly before iget */
        SMB_INODE_CACHED,	/* in a path to an inode which is in use */
        SMB_INODE_INVALID
};

/*
 * smb fs inode data (in memory only)
 */
struct smb_inode_info {
        enum smb_inode_state state;
        int nused;              /* for directories:
                                   number of references in memory */
        struct smb_inode_info *dir;
        struct smb_inode_info *next, *prev;
        struct smb_dirent finfo;
};

#endif
#endif

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

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 8
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -8
 * c-argdecl-indent: 8
 * c-label-offset: -8
 * c-continued-statement-offset: 8
 * c-continued-brace-offset: 0
 * End:
 */

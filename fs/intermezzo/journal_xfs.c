
/*
 *  * Intermezzo. (C) 1998 Peter J. Braam
 *   */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#ifdef CONFIG_FS_XFS
#include <linux/xfs_fs.h>
#endif
#include <linux/intermezzo_fs.h>
#include <linux/intermezzo_upcall.h>
#include <linux/intermezzo_psdev.h>
#include <linux/intermezzo_kml.h>
#include <linux/intermezzo_journal.h>

#if 0

/* XFS has journalling, but these functions do nothing yet... */

static unsigned long presto_xfs_freespace(struct presto_file_set *fset,
                                         struct super_block *sb)
{

#if 0
        vfs_t *vfsp = LINVFS_GET_VFS(sb);
        struct statvfs_t stat; 
        bhv_desc_t *bdp;
        unsigned long avail; 
        int rc;

        VFS_STATVFS(vfsp, &stat, NULL, rc);
        avail = statp.f_bfree;

        return sbp->sb_fdblocks;; 
#endif
        return 0x0fffffff;
}


/* start the filesystem journal operations */
static void *
presto_xfs_trans_start(struct presto_file_set *fset,
		       struct inode *inode, int op)
{
	int xfs_op;
	/* do a free blocks check as in journal_ext3? does anything protect
	 * the space in that case or can it disappear out from under us
	 * anyway? */
	
/* copied from xfs_trans.h, skipping header maze for now */
#define XFS_TRANS_SETATTR_NOT_SIZE      1
#define XFS_TRANS_SETATTR_SIZE          2
#define XFS_TRANS_INACTIVE              3
#define XFS_TRANS_CREATE                4
#define XFS_TRANS_CREATE_TRUNC          5
#define XFS_TRANS_TRUNCATE_FILE         6
#define XFS_TRANS_REMOVE                7
#define XFS_TRANS_LINK                  8
#define XFS_TRANS_RENAME                9
#define XFS_TRANS_MKDIR                 10
#define XFS_TRANS_RMDIR                 11
#define XFS_TRANS_SYMLINK               12

	/* map the op onto the values for XFS so it can do reservation. if
	 * we don't have enough info to differentiate between e.g. setattr
	 * with or without size, what do we do? will it adjust? */
	switch (op) {
	case PRESTO_OP_SETATTR:
		/* or XFS_TRANS_SETATTR_NOT_SIZE? */
	        xfs_op = XFS_TRANS_SETATTR_SIZE;
		break;
	case PRESTO_OP_CREATE:
		/* or CREATE_TRUNC? */
		xfs_op = XFS_TRANS_CREATE;
		break;
	case PRESTO_OP_LINK:
		xfs_op = XFS_TRANS_LINK;
		break;
	case PRESTO_OP_UNLINK:
		xfs_op = XFS_TRANS_REMOVE;
		break;
	case PRESTO_OP_SYMLINK:
		xfs_op = XFS_TRANS_SYMLINK;
		break;
	case PRESTO_OP_MKDIR:
		xfs_op = XFS_TRANS_MKDIR;
		break;
	case PRESTO_OP_RMDIR:
		xfs_op = XFS_TRANS_RMDIR;
		break;
	case PRESTO_OP_MKNOD:
		/* XXX can't find an analog for mknod? */
		xfs_op = XFS_TRANS_CREATE;
		break;
	case PRESTO_OP_RENAME:
		xfs_op = XFS_TRANS_RENAME;
		break;
	default:
		CDEBUG(D_JOURNAL, "invalid operation %d for journal\n", op);
		return NULL;
	}

	return xfs_trans_start(inode, xfs_op);
}

static void presto_xfs_trans_commit(struct presto_file_set *fset, void *handle)
{
	/* assert (handle == current->j_handle) */
	xfs_trans_stop(handle);
}

void presto_xfs_journal_file_data(struct inode *inode)
{
        return; 
}

struct journal_ops presto_xfs_journal_ops = {
        tr_avail: presto_xfs_freespace,
        tr_start:  presto_xfs_trans_start,
        tr_commit: presto_xfs_trans_commit,
        tr_journal_data: presto_xfs_journal_file_data
};

#endif /* CONFIG_XFS_FS */



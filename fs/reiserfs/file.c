/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */


#ifdef __KERNEL__

#include <linux/sched.h>
#include <linux/reiserfs_fs.h>
#include <linux/smp_lock.h>

#else

#include "nokernel.h"

#endif

/*
** We pack the tails of files on file close, not at the time they are written.
** This implies an unnecessary copy of the tail and an unnecessary indirect item
** insertion/balancing, for files that are written in one write.
** It avoids unnecessary tail packings (balances) for files that are written in
** multiple writes and are small enough to have tails.
** 
** file_release is called by the VFS layer when the file is closed.  If
** this is the last open file descriptor, and the file
** small enough to have a tail, and the tail is currently in an
** unformatted node, the tail is converted back into a direct item.
** 
** We use reiserfs_truncate_file to pack the tail, since it already has
** all the conditions coded.  
*/
static int reiserfs_file_release (struct inode * inode, struct file * filp)
{

    struct reiserfs_transaction_handle th ;
    int windex ;

    if (!S_ISREG (inode->i_mode))
	BUG ();

    /* fast out for when nothing needs to be done */
    if ((atomic_read(&inode->i_count) > 1 ||
         !inode->u.reiserfs_i.i_pack_on_close || 
         !tail_has_to_be_packed(inode))       && 
	inode->u.reiserfs_i.i_prealloc_count <= 0) {
	return 0;
    }    
    
    lock_kernel() ;
    down (&inode->i_sem); 
    journal_begin(&th, inode->i_sb, JOURNAL_PER_BALANCE_CNT * 3) ;

#ifdef REISERFS_PREALLOCATE
    reiserfs_discard_prealloc (&th, inode);
#endif
    journal_end(&th, inode->i_sb, JOURNAL_PER_BALANCE_CNT * 3) ;

    if (atomic_read(&inode->i_count) <= 1 &&
	inode->u.reiserfs_i.i_pack_on_close &&
        tail_has_to_be_packed (inode)) {
	/* if regular file is released by last holder and it has been
	   appended (we append by unformatted node only) or its direct
	   item(s) had to be converted, then it may have to be
	   indirect2direct converted */
	windex = push_journal_writer("file_release") ;
	reiserfs_truncate_file(inode, 0) ;
	pop_journal_writer(windex) ;
    }
    up (&inode->i_sem); 
    unlock_kernel() ;
    return 0;
}

static void reiserfs_vfs_truncate_file(struct inode *inode) {
    reiserfs_truncate_file(inode, 1) ;
}

/* Sync a reiserfs file. */
static int reiserfs_sync_file(
			      struct file   * p_s_filp,
			      struct dentry * p_s_dentry,
			      int datasync
			      ) {
  struct inode * p_s_inode = p_s_dentry->d_inode;
  struct reiserfs_transaction_handle th ;
  int n_err = 0;
  int windex ;
  int jbegin_count = 1 ;

  lock_kernel() ;

  if (!S_ISREG(p_s_inode->i_mode))
      BUG ();

  n_err = fsync_inode_buffers(p_s_inode) ;
  /* commit the current transaction to flush any metadata
  ** changes.  sys_fsync takes care of flushing the dirty pages for us
  */
  journal_begin(&th, p_s_inode->i_sb, jbegin_count) ;
  windex = push_journal_writer("sync_file") ;
  reiserfs_update_sd(&th, p_s_inode);
  pop_journal_writer(windex) ;
  journal_end_sync(&th, p_s_inode->i_sb,jbegin_count) ;
  unlock_kernel() ;
  return ( n_err < 0 ) ? -EIO : 0;
}


struct file_operations reiserfs_file_operations = {
    read:	generic_file_read,
    write:	generic_file_write,
    ioctl:	reiserfs_ioctl,
    mmap:	generic_file_mmap,
    release:	reiserfs_file_release,
    fsync:	reiserfs_sync_file,
};


struct  inode_operations reiserfs_file_inode_operations = {
    truncate:	reiserfs_vfs_truncate_file,
};



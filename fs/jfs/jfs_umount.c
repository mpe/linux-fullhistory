/*
 *   Copyright (c) International Business Machines Corp., 2000-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/*
 *	jfs_umount.c
 *
 * note: file system in transition to aggregate/fileset:
 * (ref. jfs_mount.c)
 *
 * file system unmount is interpreted as mount of the single/only 
 * fileset in the aggregate and, if unmount of the last fileset, 
 * as unmount of the aggerate;
 */

#include <linux/fs.h>
#include "jfs_incore.h"
#include "jfs_filsys.h"
#include "jfs_superblock.h"
#include "jfs_dmap.h"
#include "jfs_imap.h"
#include "jfs_metapage.h"
#include "jfs_debug.h"

/*
 * NAME:	jfs_umount(vfsp, flags, crp)
 *
 * FUNCTION:	vfs_umount()
 *
 * PARAMETERS:	vfsp	- virtual file system pointer
 *		flags	- unmount for shutdown
 *		crp	- credential
 *
 * RETURN :	EBUSY	- device has open files
 */
int jfs_umount(struct super_block *sb)
{
	int rc = 0;
	log_t *log;
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	struct inode *ipbmap = sbi->ipbmap;
	struct inode *ipimap = sbi->ipimap;
	struct inode *ipaimap = sbi->ipaimap;
	struct inode *ipaimap2 = sbi->ipaimap2;

	jFYI(1, ("\n	UnMount JFS: sb:0x%p\n", sb));

	/*
	 *      update superblock and close log 
	 *
	 * if mounted read-write and log based recovery was enabled
	 */
	if ((log = sbi->log))
		/*
		 * Wait for outstanding transactions to be written to log: 
		 */
		lmLogWait(log);

	/*
	 * close fileset inode allocation map (aka fileset inode)
	 */
	jEVENT(0, ("jfs_umount: close ipimap:0x%p\n", ipimap));
	diUnmount(ipimap, 0);

	diFreeSpecial(ipimap);
	sbi->ipimap = NULL;

	/*
	 * close secondary aggregate inode allocation map
	 */
	ipaimap2 = sbi->ipaimap2;
	if (ipaimap2) {
		jEVENT(0, ("jfs_umount: close ipaimap2:0x%p\n", ipaimap2));
		diUnmount(ipaimap2, 0);
		diFreeSpecial(ipaimap2);
		sbi->ipaimap2 = NULL;
	}

	/*
	 * close aggregate inode allocation map
	 */
	ipaimap = sbi->ipaimap;
	jEVENT(0, ("jfs_umount: close ipaimap:0x%p\n", ipaimap));
	diUnmount(ipaimap, 0);
	diFreeSpecial(ipaimap);
	sbi->ipaimap = NULL;

	/*
	 * close aggregate block allocation map
	 */
	jEVENT(0, ("jfs_umount: close ipbmap:%p\n", ipbmap));
	dbUnmount(ipbmap, 0);

	diFreeSpecial(ipbmap);
	sbi->ipimap = NULL;

	/*
	 * Make sure all metadata makes it to disk before we mark
	 * the superblock as clean
	 */
	filemap_fdatawait(sbi->direct_inode->i_mapping);
	filemap_fdatawrite(sbi->direct_inode->i_mapping);
	filemap_fdatawait(sbi->direct_inode->i_mapping);

	/*
	 * ensure all file system file pages are propagated to their
	 * home blocks on disk (and their in-memory buffer pages are 
	 * invalidated) BEFORE updating file system superblock state
	 * (to signify file system is unmounted cleanly, and thus in 
	 * consistent state) and log superblock active file system 
	 * list (to signify skip logredo()).
	 */
	if (log) {		/* log = NULL if read-only mount */
		rc = updateSuper(sb, FM_CLEAN);

		/*
		 * close log: 
		 *
		 * remove file system from log active file system list.
		 */
		rc = lmLogClose(sb, log);
	}
	jFYI(0, ("	UnMount JFS Complete: %d\n", rc));
	return rc;
}


int jfs_umount_rw(struct super_block *sb)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	log_t *log = sbi->log;

	if (!log)
		return 0;

	/*
	 * close log: 
	 *
	 * remove file system from log active file system list.
	 */
	lmLogWait(log);

	/*
	 * Make sure all metadata makes it to disk
	 */
	dbSync(sbi->ipbmap);
	diSync(sbi->ipimap);
	filemap_fdatawait(sbi->direct_inode->i_mapping);
	filemap_fdatawrite(sbi->direct_inode->i_mapping);
	filemap_fdatawait(sbi->direct_inode->i_mapping);

	updateSuper(sb, FM_CLEAN);
	sbi->log = NULL;

	return lmLogClose(sb, log);
}

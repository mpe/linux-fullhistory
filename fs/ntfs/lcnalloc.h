/*
 * lcnalloc.h - Exports for NTFS kernel cluster (de)allocation.  Part of the
 *		Linux-NTFS project.
 *
 * Copyright (c) 2004 Anton Altaparmakov
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_NTFS_LCNALLOC_H
#define _LINUX_NTFS_LCNALLOC_H

#ifdef NTFS_RW

#include <linux/fs.h>

#include "types.h"
#include "volume.h"

typedef enum {
	FIRST_ZONE	= 0,	/* For sanity checking. */
	MFT_ZONE	= 0,	/* Allocate from $MFT zone. */
	DATA_ZONE	= 1,	/* Allocate from $DATA zone. */
	LAST_ZONE	= 1,	/* For sanity checking. */
} NTFS_CLUSTER_ALLOCATION_ZONES;

extern runlist_element *ntfs_cluster_alloc(ntfs_volume *vol,
		const VCN start_vcn, const s64 count, const LCN start_lcn,
		const NTFS_CLUSTER_ALLOCATION_ZONES zone);

extern s64 __ntfs_cluster_free(struct inode *vi, const VCN start_vcn,
		s64 count, const BOOL is_rollback);

/**
 * ntfs_cluster_free - free clusters on an ntfs volume
 * @vi:		vfs inode whose runlist describes the clusters to free
 * @start_vcn:	vcn in the runlist of @vi at which to start freeing clusters
 * @count:	number of clusters to free or -1 for all clusters
 *
 * Free @count clusters starting at the cluster @start_vcn in the runlist
 * described by the vfs inode @vi.
 *
 * If @count is -1, all clusters from @start_vcn to the end of the runlist are
 * deallocated.  Thus, to completely free all clusters in a runlist, use
 * @start_vcn = 0 and @count = -1.
 *
 * Note, ntfs_cluster_free() does not modify the runlist at all, so the caller
 * has to deal with it later.
 *
 * Return the number of deallocated clusters (not counting sparse ones) on
 * success and -errno on error.
 *
 * Locking: - The runlist described by @vi must be unlocked on entry and is
 *	      unlocked on return.
 *	    - This function takes the runlist lock of @vi for reading and
 *	      sometimes for writing and sometimes modifies the runlist.
 *	    - The volume lcn bitmap must be unlocked on entry and is unlocked
 *	      on return.
 *	    - This function takes the volume lcn bitmap lock for writing and
 *	      modifies the bitmap contents.
 */
static inline s64 ntfs_cluster_free(struct inode *vi, const VCN start_vcn,
		s64 count)
{
	return __ntfs_cluster_free(vi, start_vcn, count, FALSE);
}

#endif /* NTFS_RW */

#endif /* defined _LINUX_NTFS_LCNALLOC_H */

/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

/*
 * This file contains globals needed by XFS that were normally defined
 * somewhere else in IRIX.
 */

#include <xfs.h>

uint64_t	xfs_panic_mask;		/* set to cause more panics */
unsigned long	xfs_physmem;

/*
 * Used to serialize atomicIncWithWrap.
 */
spinlock_t Atomic_spin = SPIN_LOCK_UNLOCKED;

/*
 * Global system credential structure.
 */
cred_t sys_cred_val, *sys_cred = &sys_cred_val;

/*
 * The global quota manager. There is only one of these for the entire
 * system, _not_ one per file system. XQM keeps track of the overall
 * quota functionality, including maintaining the freelist and hash
 * tables of dquots.
 */
struct xfs_qm	*xfs_Gqm;
mutex_t		xfs_Gqm_lock;

/* Export XFS symbols used by xfsidbg */
EXPORT_SYMBOL(xfs_Gqm);
EXPORT_SYMBOL(xfs_next_bit);
EXPORT_SYMBOL(xfs_contig_bits);
EXPORT_SYMBOL(xfs_bmbt_get_all);


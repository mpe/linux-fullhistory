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
#ifndef __XFS_DMAPI_H__
#define __XFS_DMAPI_H__

#ifdef CONFIG_XFS_DMAPI

#include <dmapi/dmapi.h>
#include <dmapi/dmapi_kern.h>

/*	Values used to define the on-disk version of dm_attrname_t. All
 *	on-disk attribute names start with the 8-byte string "SGI_DMI_".
 *
 *	In the on-disk inode, DMAPI attribute names consist of the user-provided
 *	name with the DMATTR_PREFIXSTRING pre-pended.  This string must NEVER be
 *	changed.
 */

#define DMATTR_PREFIXLEN	8
#define DMATTR_PREFIXSTRING	"SGI_DMI_"

/* Defines for determining if an event message should be sent. */
#define DM_EVENT_ENABLED(vfsp, ip, event) ( \
	unlikely ((vfsp)->vfs_flag & VFS_DMI) && \
		( ((ip)->i_d.di_dmevmask & (1 << event)) || \
		  ((ip)->i_mount->m_dmevmask & (1 << event)) ) \
	)

#define DM_EVENT_ENABLED_IO(vfsp, io, event) ( \
	unlikely ((vfsp)->vfs_flag & VFS_DMI) && \
		( ((io)->io_dmevmask & (1 << event)) || \
		  ((io)->io_mount->m_dmevmask & (1 << event)) ) \
	)

/*
 *	Macros to turn caller specified delay/block flags into
 *	dm_send_xxxx_event flag DM_FLAGS_NDELAY.
 */

#define FILP_DELAY_FLAG(filp) ((filp->f_flags&(O_NDELAY|O_NONBLOCK)) ? \
			DM_FLAGS_NDELAY : 0)
#define AT_DELAY_FLAG(f) ((f&ATTR_NONBLOCK) ? DM_FLAGS_NDELAY : 0)



/* events valid in dm_set_eventlist() when called with a filesystem handle.
   These events are not persistent.
*/

#define DM_XFS_VALID_FS_EVENTS		( \
	(1 << DM_EVENT_PREUNMOUNT)	| \
	(1 << DM_EVENT_UNMOUNT)		| \
	(1 << DM_EVENT_NOSPACE)		| \
	(1 << DM_EVENT_DEBUT)		| \
	(1 << DM_EVENT_CREATE)		| \
	(1 << DM_EVENT_POSTCREATE)	| \
	(1 << DM_EVENT_REMOVE)		| \
	(1 << DM_EVENT_POSTREMOVE)	| \
	(1 << DM_EVENT_RENAME)		| \
	(1 << DM_EVENT_POSTRENAME)	| \
	(1 << DM_EVENT_LINK)		| \
	(1 << DM_EVENT_POSTLINK)	| \
	(1 << DM_EVENT_SYMLINK)		| \
	(1 << DM_EVENT_POSTSYMLINK)	| \
	(1 << DM_EVENT_ATTRIBUTE)	| \
	(1 << DM_EVENT_DESTROY)		)

/* Events valid in dm_set_eventlist() when called with a file handle for
   a regular file or a symlink.	 These events are persistent.
*/

#define DM_XFS_VALID_FILE_EVENTS	( \
	(1 << DM_EVENT_ATTRIBUTE)	| \
	(1 << DM_EVENT_DESTROY)		)

/* Events valid in dm_set_eventlist() when called with a file handle for
   a directory.	 These events are persistent.
*/

#define DM_XFS_VALID_DIRECTORY_EVENTS	( \
	(1 << DM_EVENT_CREATE)		| \
	(1 << DM_EVENT_POSTCREATE)	| \
	(1 << DM_EVENT_REMOVE)		| \
	(1 << DM_EVENT_POSTREMOVE)	| \
	(1 << DM_EVENT_RENAME)		| \
	(1 << DM_EVENT_POSTRENAME)	| \
	(1 << DM_EVENT_LINK)		| \
	(1 << DM_EVENT_POSTLINK)	| \
	(1 << DM_EVENT_SYMLINK)		| \
	(1 << DM_EVENT_POSTSYMLINK)	| \
	(1 << DM_EVENT_ATTRIBUTE)	| \
	(1 << DM_EVENT_DESTROY)		)


/* Events supported by the XFS filesystem. */
#define DM_XFS_SUPPORTED_EVENTS		( \
	(1 << DM_EVENT_MOUNT)		| \
	(1 << DM_EVENT_PREUNMOUNT)	| \
	(1 << DM_EVENT_UNMOUNT)		| \
	(1 << DM_EVENT_NOSPACE)		| \
	(1 << DM_EVENT_CREATE)		| \
	(1 << DM_EVENT_POSTCREATE)	| \
	(1 << DM_EVENT_REMOVE)		| \
	(1 << DM_EVENT_POSTREMOVE)	| \
	(1 << DM_EVENT_RENAME)		| \
	(1 << DM_EVENT_POSTRENAME)	| \
	(1 << DM_EVENT_LINK)		| \
	(1 << DM_EVENT_POSTLINK)	| \
	(1 << DM_EVENT_SYMLINK)		| \
	(1 << DM_EVENT_POSTSYMLINK)	| \
	(1 << DM_EVENT_READ)		| \
	(1 << DM_EVENT_WRITE)		| \
	(1 << DM_EVENT_TRUNCATE)	| \
	(1 << DM_EVENT_ATTRIBUTE)	| \
	(1 << DM_EVENT_DESTROY)		)


extern int
xfs_dm_mount(
	vfs_t		*vfsp,
	char		*dir_name,
	char		*fsname);

extern int
xfs_dm_get_fsys_vector(
	bhv_desc_t	*bdp,
	dm_fcntl_vector_t *vecrq);

extern int
xfs_dm_send_data_event(
	dm_eventtype_t	event,
	bhv_desc_t	*bdp,
	xfs_off_t	offset,
	size_t		length,
	int		flags,
	vrwlock_t	*locktype);

extern int
xfs_dm_send_create_event(
	bhv_desc_t	*dir_bdp,
	char		*name,
	mode_t		new_mode,
	int		*good_event_sent);

extern int
xfs_dm_send_mmap_event(
	struct vm_area_struct *vma,
	unsigned int	wantflag);

#else /* CONFIG_XFS_DMAPI */

/*
 *	Flags needed to build with dmapi disabled.
 */

typedef enum {
	DM_EVENT_INVALID	= -1,
	DM_EVENT_CANCEL		= 0,		/* not supported */
	DM_EVENT_MOUNT		= 1,
	DM_EVENT_PREUNMOUNT	= 2,
	DM_EVENT_UNMOUNT	= 3,
	DM_EVENT_DEBUT		= 4,		/* not supported */
	DM_EVENT_CREATE		= 5,
	DM_EVENT_CLOSE		= 6,		/* not supported */
	DM_EVENT_POSTCREATE	= 7,
	DM_EVENT_REMOVE		= 8,
	DM_EVENT_POSTREMOVE	= 9,
	DM_EVENT_RENAME		= 10,
	DM_EVENT_POSTRENAME	= 11,
	DM_EVENT_LINK		= 12,
	DM_EVENT_POSTLINK	= 13,
	DM_EVENT_SYMLINK	= 14,
	DM_EVENT_POSTSYMLINK	= 15,
	DM_EVENT_READ		= 16,
	DM_EVENT_WRITE		= 17,
	DM_EVENT_TRUNCATE	= 18,
	DM_EVENT_ATTRIBUTE	= 19,
	DM_EVENT_DESTROY	= 20,
	DM_EVENT_NOSPACE	= 21,
	DM_EVENT_USER		= 22,
	DM_EVENT_MAX		= 23
} dm_eventtype_t;

typedef enum {
	DM_RIGHT_NULL,
	DM_RIGHT_SHARED,
	DM_RIGHT_EXCL
} dm_right_t;

/*
 *	Defines for determining if an event message should be sent.
 */
#define DM_EVENT_ENABLED(vfsp, ip, event)	0
#define DM_EVENT_ENABLED_IO(vfsp, io, event)	0

/*
 *	Stubbed out DMAPI delay macros.
 */

#define FILP_DELAY_FLAG(filp)			0
#define AT_DELAY_FLAG(f)			0

/*
 *	Events supported by the XFS filesystem.
 */

#define DM_XFS_VALID_FS_EVENTS			0
#define DM_XFS_VALID_FILE_EVENTS		0
#define DM_XFS_VALID_DIRECTORY_EVENTS		0
#define DM_XFS_SUPPORTED_EVENTS			0

/*
 *	Dummy definitions used for the flags field on dm_send_*_event().
 */

#define DM_FLAGS_NDELAY		0x001	/* return EAGAIN after dm_pending() */
#define DM_FLAGS_UNWANTED	0x002	/* event not in fsys dm_eventset_t */

/*
 *	Stubs for XFS DMAPI utility routines.
 */

static __inline int
xfs_dm_send_create_event(
	bhv_desc_t	*dir_bdp,
	char		*name,
	mode_t		new_mode,
	int		*good_event_sent)
{
	return 0;
}

static __inline int
xfs_dm_send_data_event(
	dm_eventtype_t	event,
	bhv_desc_t	*bdp,
	xfs_off_t	offset,
	size_t		length,
	int		flags,
	vrwlock_t	*locktype)
{
	return nopkg();
}

static __inline int
xfs_dm_send_mmap_event(
	struct vm_area_struct *vma,
	unsigned int	wantflag)
{
	return 0;
}

/*
 *	Stubs for routines needed for the X/Open version of DMAPI.
 */

static __inline int
dm_send_destroy_event(
	bhv_desc_t	*bdp,
	dm_right_t	vp_right)
{
	return nopkg();
}

static __inline int
dm_send_namesp_event(
	dm_eventtype_t	event,
	bhv_desc_t	*bdp1,
	dm_right_t	vp1_right,
	bhv_desc_t	*bdp2,
	dm_right_t	vp2_right,
	char		*name1,
	char		*name2,
	mode_t		mode,
	int		retcode,
	int		flags)
{
	return nopkg();
}

static __inline void
dm_send_unmount_event(
	vfs_t		*vfsp,
	vnode_t		*vp,
	dm_right_t	vfsp_right,
	mode_t		mode,
	int		retcode,
	int		flags)
{
}

#endif	/* CONFIG_XFS_DMAPI */
#endif	/* __XFS_DMAPI_H__ */
